//! A machine that runs the static ELF executables the compiler writes for
//! arm64, so that what they do is checked on a machine of any kind. It reads
//! the instructions the compiler writes and no others, and stops with a
//! message that names the word when it meets one it does not know, so a
//! new instruction is a failing test and not a wrong answer.
//!
//! The kernel's part is four calls: `write` to the standard output and the
//! standard error, `mmap` of fresh pages, `getrandom`, and `exit_group`.

const STACK_BASE: u64 = 0x7000_0000;
const STACK_SIZE: u64 = 1 << 20;
const HEAP_BASE: u64 = 0x1000_0000;

/// What the machine ran for, at most, before the program is taken never to
/// end.
const MOST_STEPS: u64 = 20_000_000;

const ELF_MACHINE_AT: usize = 18;
const AARCH64: u16 = 0xb7;

/// What a run did.
#[derive(Debug, PartialEq, Eq)]
pub struct Ran {
    pub stdout: Vec<u8>,
    pub stderr: Vec<u8>,
    pub status: i32,
    /// How many instructions it ran.
    pub steps: u64,
}

/// Whether the bytes are a static ELF executable for arm64.
pub fn is_executable(bytes: &[u8]) -> bool {
    bytes.starts_with(b"\x7fELF")
        && bytes.len() > ELF_MACHINE_AT + 2
        && u16::from_le_bytes([bytes[ELF_MACHINE_AT], bytes[ELF_MACHINE_AT + 1]]) == AARCH64
}

fn u16_at(bytes: &[u8], at: usize) -> u64 {
    u16::from_le_bytes(bytes[at..at + 2].try_into().unwrap()) as u64
}

fn u32_at(bytes: &[u8], at: usize) -> u64 {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()) as u64
}

fn u64_at(bytes: &[u8], at: usize) -> u64 {
    u64::from_le_bytes(bytes[at..at + 8].try_into().unwrap())
}

struct Machine {
    regions: Vec<(u64, Vec<u8>)>,
    x: [u64; 31],
    /// Each register of floats and vectors: a float of 32 or of 64 bits is in the
    /// low bits, with zeros above it, and a vector fills all 128.
    f: [u128; 32],
    sp: u64,
    pc: u64,
    /// The flags: negative, zero, carry, overflow.
    n: bool,
    z: bool,
    c: bool,
    v: bool,
    stdout: Vec<u8>,
    stderr: Vec<u8>,
    next_heap: u64,
    /// How the kernel answers `getrandom`, and what it has been asked.
    entropy: Entropy,
    asked: u64,
    random: u64,
}

/// How the kernel answers a request for random bytes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Entropy {
    /// Every byte asked for, never the same twice.
    Fresh,
    /// At most this many bytes a call, so a program that asks once is not
    /// given all it needs.
    ByPieces(u64),
    /// The first call is interrupted, and the next are as `Fresh` says.
    Interrupted,
    /// Every call fails, with an error that is not an interrupt.
    Refused,
}

/// Runs the executable, which must be one `is_executable` accepts.
pub fn run(bytes: &[u8]) -> Ran {
    run_with(bytes, Entropy::Fresh)
}

/// Runs the executable, with the kernel answering random bytes as said.
pub fn run_with(bytes: &[u8], entropy: Entropy) -> Ran {
    assert!(is_executable(bytes), "not an arm64 executable");
    let mut m = Machine {
        regions: Vec::new(),
        x: [0; 31],
        f: [0; 32],
        sp: STACK_BASE + STACK_SIZE - 256,
        pc: u64_at(bytes, 24),
        n: false,
        z: false,
        c: false,
        v: false,
        stdout: Vec::new(),
        stderr: Vec::new(),
        next_heap: HEAP_BASE,
        entropy,
        asked: 0,
        // What no two runs have the same: the time, and a number that
        // counts the runs.
        random: {
            static RUNS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_or(1, |d| d.as_nanos() as u64);
            (now ^ RUNS.fetch_add(1, std::sync::atomic::Ordering::Relaxed).wrapping_mul(0x9e37_79b9_7f4a_7c15)) | 1
        },
    };
    let headers = u64_at(bytes, 32) as usize;
    let size = u16_at(bytes, 54) as usize;
    for i in 0..u16_at(bytes, 56) as usize {
        let at = headers + i * size;
        if u32_at(bytes, at) != 1 {
            continue;
        }
        let (offset, address) = (u64_at(bytes, at + 8) as usize, u64_at(bytes, at + 16));
        let (in_file, in_memory) = (u64_at(bytes, at + 32) as usize, u64_at(bytes, at + 40));
        let mut segment = vec![0; in_memory as usize];
        segment[..in_file].copy_from_slice(&bytes[offset..offset + in_file]);
        m.regions.push((address, segment));
    }
    m.regions.push((STACK_BASE, vec![0; STACK_SIZE as usize]));
    // The kernel starts a program with the number of its words on the stack,
    // and then their addresses.
    m.write(m.sp, 8, 1);
    m.run()
}

/// `a + b + carry` in `bits` bits, with the carry and the overflow it makes.
fn add_with_carry(a: u64, b: u64, carry: bool, bits: u32) -> (u64, bool, bool) {
    let mask = if bits == 64 { u64::MAX } else { (1 << bits) - 1 };
    let (a, b) = (a & mask, b & mask);
    let wide = a as u128 + b as u128 + carry as u128;
    let result = wide as u64 & mask;
    let top = 1u64 << (bits - 1);
    let overflow = (a & top == b & top) && (result & top != a & top);
    (result, wide > mask as u128, overflow)
}

impl Machine {
    fn region(&mut self, address: u64, len: u64) -> &mut [u8] {
        let found = self
            .regions
            .iter()
            .position(|(base, bytes)| address >= *base && address + len <= base + bytes.len() as u64);
        let Some(i) = found else {
            panic!("the program touched {address:#x}, which is not its memory");
        };
        let (base, bytes) = &mut self.regions[i];
        let from = (address - *base) as usize;
        &mut bytes[from..from + len as usize]
    }

    fn read(&mut self, address: u64, size: u64) -> u64 {
        let mut word = [0u8; 8];
        word[..size as usize].copy_from_slice(self.region(address, size));
        u64::from_le_bytes(word)
    }

    fn write(&mut self, address: u64, size: u64, value: u64) {
        self.region(address, size)
            .copy_from_slice(&value.to_le_bytes()[..size as usize]);
    }

    /// A register, where 31 is the zero register.
    fn get(&self, r: u32) -> u64 {
        if r == 31 { 0 } else { self.x[r as usize] }
    }

    /// A register, where 31 is the stack pointer.
    fn get_sp(&self, r: u32) -> u64 {
        if r == 31 { self.sp } else { self.x[r as usize] }
    }

    fn set(&mut self, r: u32, value: u64) {
        if r != 31 {
            self.x[r as usize] = value;
        }
    }

    fn set_sp(&mut self, r: u32, value: u64) {
        if r == 31 {
            self.sp = value;
        } else {
            self.x[r as usize] = value;
        }
    }

    fn holds(&self, cond: u32) -> bool {
        let base = match cond >> 1 {
            0 => self.z,
            1 => self.c,
            2 => self.n,
            3 => self.v,
            4 => self.c && !self.z,
            5 => self.n == self.v,
            6 => self.n == self.v && !self.z,
            _ => true,
        };
        if cond & 1 == 1 && cond != 15 { !base } else { base }
    }

    fn run(&mut self) -> Ran {
        let mut steps = 0;
        loop {
            steps += 1;
            assert!(steps <= MOST_STEPS, "the program never ends");
            let w = self.read(self.pc, 4) as u32;
            let at = self.pc;
            self.pc += 4;
            if let Some(status) = self.step(w, at) {
                return Ran {
                    stdout: std::mem::take(&mut self.stdout),
                    stderr: std::mem::take(&mut self.stderr),
                    status,
                    steps,
                };
            }
        }
    }

    /// Runs one instruction, and gives the status when it ends the program.
    fn step(&mut self, w: u32, at: u64) -> Option<i32> {
        let (rd, rn, rm) = (w & 31, (w >> 5) & 31, (w >> 16) & 31);
        let sf = w >> 31 == 1;
        let bits = if sf { 64 } else { 32 };
        let mask = if sf { u64::MAX } else { u32::MAX as u64 };
        let signed = |value: u32, width: u32| -> i64 {
            let shift = 64 - width;
            ((value as u64) << shift) as i64 >> shift
        };
        if w == 0xd65f_03c0 {
            self.pc = self.x[30];
        } else if w & 0xffff_fc1f == 0xd61f_0000 {
            // br xn
            self.pc = self.get(rn);
        } else if w & 0xffe0_fc00 == 0xb8a0_7800 {
            // ldrsw xt, [xn, xm, lsl #2]
            let value = self.read(self.get_sp(rn) + 4 * self.get(rm), 4);
            self.set(rd, value as u32 as i32 as i64 as u64);
        } else if w & 0xff00_0000 == 0x1f00_0000 {
            self.fused_step(w);
        } else if self.vector_step(w) {
        } else if w & 0x7f20_0000 == 0x1e20_0000 {
            self.float_step(w);
        } else if w & 0xffc0_0000 == 0x3dc0_0000 || w & 0xffc0_0000 == 0x3d80_0000 {
            // ldr or str of a register of 16 bytes, with an offset that is a
            // multiple of 16.
            let address = self.get_sp(rn) + (((w >> 10) & 0xfff) as u64) * 16;
            if w >> 22 & 1 == 1 {
                let low = self.read(address, 8);
                let high = self.read(address + 8, 8);
                self.f[rd as usize] = u128::from(low) | (u128::from(high) << 64);
            } else {
                let value = self.f[rd as usize];
                self.write(address, 8, value as u64);
                self.write(address + 8, 8, (value >> 64) as u64);
            }
        } else if w & 0x3f00_0000 == 0x3d00_0000 {
            // ldr or str of a float, with an offset that is a multiple of its size.
            let size = 1u64 << (w >> 30);
            let address = self.get_sp(rn) + (((w >> 10) & 0xfff) as u64) * size;
            if w >> 22 & 3 == 1 {
                self.f[rd as usize] = u128::from(self.read(address, size));
            } else {
                assert!(w >> 22 & 3 == 0, "unknown instruction {w:#010x}");
                let value = self.f[rd as usize] as u64;
                self.write(address, size, value);
            }
        } else if w & 0x3fe0_ec00 == 0x3c20_6800 {
            // str of a float, at an address that is a register and another,
            // shifted left by log2 of its size or not.
            let size = 1u64 << (w >> 30);
            let by = if w >> 12 & 1 == 1 { size.trailing_zeros() } else { 0 };
            let address = self.get_sp(rn) + (self.get(rm) << by);
            let value = self.f[rd as usize] as u64;
            self.write(address, size, value);
        } else if w == 0xd400_0001 {
            return self.system_call();
        } else if w & 0x7c00_0000 == 0x1400_0000 {
            if sf {
                self.x[30] = self.pc;
            }
            self.pc = at.wrapping_add((signed(w & 0x3ff_ffff, 26) * 4) as u64);
        } else if w & 0xff00_0010 == 0x5400_0000 {
            if self.holds(w & 15) {
                self.pc = at.wrapping_add((signed((w >> 5) & 0x7_ffff, 19) * 4) as u64);
            }
        } else if w & 0x7e00_0000 == 0x3400_0000 {
            let zero = self.get(rd) & mask == 0;
            if zero != (w >> 24 & 1 == 1) {
                self.pc = at.wrapping_add((signed((w >> 5) & 0x7_ffff, 19) * 4) as u64);
            }
        } else if w & 0x9f00_0000 == 0x9000_0000 {
            let pages = signed((w >> 5 & 0x7_ffff) << 2 | (w >> 29 & 3), 21);
            self.set(rd, (at & !0xfff).wrapping_add((pages << 12) as u64));
        } else if w & 0x1f00_0000 == 0x1100_0000 {
            let imm = ((w >> 10) & 0xfff) as u64;
            let imm = if w >> 22 & 1 == 1 { imm << 12 } else { imm };
            let subtract = w >> 30 & 1 == 1;
            let a = self.get_sp(rn);
            let (b, carry) = if subtract { (!imm, true) } else { (imm, false) };
            let (result, c, v) = add_with_carry(a, b, carry, bits);
            if w >> 29 & 1 == 1 {
                self.flags(result, c, v, bits);
                self.set(rd, result);
            } else {
                self.set_sp(rd, result);
            }
        } else if w & 0x1f20_0000 == 0x0b00_0000 {
            // The second register may be shifted left, or right as an unsigned
            // or as a signed number, by a number of bits the instruction holds.
            assert!(w >> 22 & 3 != 3 && bits == 64 || w >> 10 & 0x3f == 0, "unknown instruction {w:#010x}");
            let (a, by) = (self.get(rn), w >> 10 & 0x3f);
            let b = match w >> 22 & 3 {
                0 => self.get(rm) << by,
                1 => self.get(rm) >> by,
                _ => ((self.get(rm) as i64) >> by) as u64,
            };
            let subtract = w >> 30 & 1 == 1;
            let (b, carry) = if subtract { (!b, true) } else { (b, false) };
            let (result, c, v) = add_with_carry(a, b, carry, bits);
            if w >> 29 & 1 == 1 {
                self.flags(result, c, v, bits);
            }
            self.set(rd, result);
        } else if w & 0x1f00_0000 == 0x0a00_0000 {
            assert!(
                w >> 10 & 0x3f == 0 && w >> 22 & 3 == 0 && w >> 21 & 1 == 0,
                "unknown instruction {w:#010x}"
            );
            let (a, b) = (self.get(rn) & mask, self.get(rm) & mask);
            let result = match w >> 29 & 3 {
                0 | 3 => a & b,
                1 => a | b,
                _ => a ^ b,
            };
            if w >> 29 & 3 == 3 {
                self.flags(result, false, false, bits);
            }
            self.set(rd, result);
        } else if w & 0x1f80_0000 == 0x1280_0000 {
            let imm = (w >> 5 & 0xffff) as u64;
            let shift = (w >> 21 & 3) * 16;
            match w >> 29 & 3 {
                0 => self.set(rd, !(imm << shift) & mask),
                2 => self.set(rd, imm << shift),
                3 => {
                    let kept = self.get(rd) & !(0xffff << shift);
                    self.set(rd, kept | imm << shift);
                }
                _ => panic!("unknown instruction {w:#010x}"),
            }
        } else if w & 0x1f80_0000 == 0x1300_0000 {
            let (immr, imms) = (w >> 16 & 63, w >> 10 & 63);
            assert!(imms < bits, "unknown instruction {w:#010x}");
            if imms < immr {
                // `lsl`: the low `imms + 1` bits, placed `bits - immr` up.
                assert!(w >> 29 & 3 == 2, "unknown instruction {w:#010x}");
                let field = self.get(rn) & (u64::MAX >> (64 - (imms + 1)));
                self.set(rd, field << (bits - immr) & mask);
                return None;
            }
            let width = imms - immr + 1;
            let field = (self.get(rn) & mask) >> immr & (u64::MAX >> (64 - width));
            let result = match w >> 29 & 3 {
                2 => field,
                0 => (((field << (64 - width)) as i64) >> (64 - width)) as u64 & mask,
                _ => panic!("unknown instruction {w:#010x}"),
            };
            self.set(rd, result);
        } else if w & 0x1fe0_0800 == 0x1a80_0000 {
            let (a, b) = (self.get(rn) & mask, self.get(rm) & mask);
            let taken = self.holds(w >> 12 & 15);
            // csel, csinc, and csinv, which `csetm` is.
            let result = if taken {
                a
            } else if w >> 30 & 1 == 1 && w >> 10 & 1 == 0 {
                !b & mask
            } else if w >> 10 & 1 == 1 {
                b.wrapping_add(1) & mask
            } else {
                b
            };
            assert!(
                w >> 29 & 1 == 0 && (w >> 30 & 1 == 0 || w >> 10 & 1 == 0),
                "unknown instruction {w:#010x}"
            );
            self.set(rd, result);
        } else if w & 0x7fe0_f000 == 0x1ac0_2000 {
            let (a, count) = (self.get(rn) & mask, self.get(rm) & (bits as u64 - 1));
            let result = match w >> 10 & 3 {
                0 => a << count,
                1 => a >> count,
                2 => (((a << (64 - bits)) as i64) >> (64 - bits) >> count) as u64,
                _ => panic!("unknown instruction {w:#010x}"),
            };
            self.set(rd, result & mask);
        } else if w & 0xffff_fc00 == 0xdac0_0000 {
            // rbit xd, xn
            self.set(rd, self.get(rn).reverse_bits());
        } else if w & 0xffff_fc00 == 0xdac0_1000 {
            // clz xd, xn
            self.set(rd, u64::from(self.get(rn).leading_zeros()));
        } else if w & 0xffe0_fc00 == 0x9b00_7c00 {
            self.set(rd, self.get(rn).wrapping_mul(self.get(rm)));
        } else if w & 0xffe0_f800 == 0x9ac0_0800 {
            // udiv and sdiv of 64 bits, which do not trap: a divisor of 0
            // gives 0, and the least number by -1 gives the least number.
            let (a, b) = (self.get(rn), self.get(rm));
            let result = match (w >> 10 & 1 == 1, b) {
                (_, 0) => 0,
                (false, _) => a / b,
                (true, _) => (a as i64).wrapping_div(b as i64) as u64,
            };
            self.set(rd, result);
        } else if w & 0xffe0_8000 == 0x9b00_8000 {
            // msub xd, xn, xm, xa: xa - xn * xm.
            let ra = w >> 10 & 31;
            let product = self.get(rn).wrapping_mul(self.get(rm));
            self.set(rd, self.get(ra).wrapping_sub(product));
        } else if w & 0xffe0_fc00 == 0x9bc0_7c00 {
            let product = self.get(rn) as u128 * self.get(rm) as u128;
            self.set(rd, (product >> 64) as u64);
        } else if w & 0xffe0_fc00 == 0x9b40_7c00 {
            let product = self.get(rn) as i64 as i128 * self.get(rm) as i64 as i128;
            self.set(rd, (product >> 64) as u64);
        } else if w & 0x3fe0_ec00 == 0x3820_6800 {
            // str xt, [xn, xm, lsl #log2(size)], or with no shift.
            let size = 1u64 << (w >> 30);
            let by = if w >> 12 & 1 == 1 { size.trailing_zeros() } else { 0 };
            let address = self.get_sp(rn) + (self.get(rm) << by);
            let value = self.get(rd);
            self.write(address, size, value);
        } else if w & 0x3f00_0000 == 0x3900_0000 {
            let size = 1u64 << (w >> 30);
            let address = self.get_sp(rn) + (((w >> 10) & 0xfff) as u64) * size;
            match w >> 22 & 3 {
                0 => {
                    let value = self.get(rd);
                    self.write(address, size, value);
                }
                1 => {
                    let value = self.read(address, size);
                    self.set(rd, value);
                }
                2 => {
                    let value = self.read(address, size);
                    let shift = 64 - 8 * size;
                    self.set(rd, ((value << shift) as i64 >> shift) as u64);
                }
                _ => panic!("unknown instruction {w:#010x}"),
            }
        } else {
            panic!("unknown instruction {w:#010x} at {at:#x}");
        }
        None
    }

    /// Runs `fmadd`, `fmsub`, `fnmadd`, or `fnmsub`, which work out `a * b` and
    /// the addend exactly and round once, as the host's `mul_add` does.
    fn fused_step(&mut self, w: u32) {
        let (rd, rn, rm, ra) = (
            (w & 31) as usize,
            ((w >> 5) & 31) as usize,
            ((w >> 16) & 31) as usize,
            ((w >> 10) & 31) as usize,
        );
        // The bit 21 negates the addend, and the product is negated when the bit 15
        // and the bit 21 differ: `fnmadd` has the first only, and `fnmsub` both.
        let (o0, o1) = (w >> 15 & 1 == 1, w >> 21 & 1 == 1);
        let (negate_product, negate_addend) = (o0 != o1, o1);
        let (n, m, c) = (self.f[rn] as u64, self.f[rm] as u64, self.f[ra] as u64);
        // `fmadd` is `a + n * m`, `fmsub` `a - n * m`, `fnmadd` `-a - n * m`, and
        // `fnmsub` `-a + n * m`.
        self.f[rd] = match w >> 22 & 3 {
            0 => {
                let (x, y, z) = (f32::from_bits(n as u32), f32::from_bits(m as u32), f32::from_bits(c as u32));
                let x = if negate_product { -x } else { x };
                let z = if negate_addend { -z } else { z };
                u128::from(x.mul_add(y, z).to_bits())
            }
            1 => {
                let (x, y, z) = (f64::from_bits(n), f64::from_bits(m), f64::from_bits(c));
                let x = if negate_product { -x } else { x };
                let z = if negate_addend { -z } else { z };
                u128::from(x.mul_add(y, z).to_bits())
            }
            _ => panic!("unknown instruction {w:#010x}"),
        };
    }

    /// Runs one instruction of floats, the arithmetic and the conversions, which
    /// the host's own floats do as the chip does: round to the nearest, ties to
    /// even, and a conversion to an integer that saturates and gives 0 for a
    /// number that is not one.
    fn float_step(&mut self, w: u32) {
        let (rd, rn, rm) = ((w & 31) as usize, ((w >> 5) & 31) as usize, ((w >> 16) & 31) as usize);
        let double = w >> 22 & 3 == 1;
        let (a, b) = (self.f[rn] as u64, self.f[rm] as u64);
        let f32s = |n: u64| f32::from_bits(n as u32);
        if w & 0xff20_0c00 == 0x1e20_0800 {
            let value = if double {
                let (x, y) = (f64::from_bits(a), f64::from_bits(b));
                match w >> 12 & 15 {
                    0 => x * y,
                    1 => x / y,
                    2 => x + y,
                    3 => x - y,
                    _ => panic!("unknown instruction {w:#010x}"),
                }
                .to_bits()
            } else {
                let (x, y) = (f32s(a), f32s(b));
                u64::from(
                    match w >> 12 & 15 {
                        0 => x * y,
                        1 => x / y,
                        2 => x + y,
                        3 => x - y,
                        _ => panic!("unknown instruction {w:#010x}"),
                    }
                    .to_bits(),
                )
            };
            self.f[rd] = u128::from(value);
        } else if w & 0xff3f_fc00 == 0x1e21_4000 {
            // fneg
            self.f[rd] = u128::from(a ^ if double { 1 << 63 } else { 1 << 31 });
        } else if w & 0xff3f_fc00 == 0x1e21_c000 {
            // fsqrt
            self.f[rd] = if double {
                u128::from(f64::from_bits(a).sqrt().to_bits())
            } else {
                u128::from(f32s(a).sqrt().to_bits())
            };
        } else if w & 0xffff_fc00 == 0x1e60_4000 {
            // fmov dd, dn
            self.f[rd] = u128::from(a);
        } else if w & 0xffff_fc00 == 0x1e22_c000 {
            // fcvt dd, sn
            self.f[rd] = u128::from(f64::from(f32s(a)).to_bits());
        } else if w & 0xffff_fc00 == 0x1e62_4000 {
            // fcvt sd, dn
            self.f[rd] = u128::from((f64::from_bits(a) as f32).to_bits());
        } else if w & 0xff20_fc1f == 0x1e20_2000 {
            // fcmp fn, fm
            let order = if double {
                f64::from_bits(a).partial_cmp(&f64::from_bits(b))
            } else {
                f32s(a).partial_cmp(&f32s(b))
            };
            (self.n, self.z, self.c, self.v) = match order {
                Some(std::cmp::Ordering::Less) => (true, false, false, false),
                Some(std::cmp::Ordering::Equal) => (false, true, true, false),
                Some(std::cmp::Ordering::Greater) => (false, false, true, false),
                None => (false, false, true, true),
            };
        } else if w & 0xff20_0c00 == 0x1e20_0c00 {
            // fcsel fd, fn, fm, cond
            self.f[rd] = u128::from(if self.holds(w >> 12 & 15) { a } else { b });
        } else if w & 0x7f20_fc00 == 0x1e20_0000 {
            let sf = w >> 31 == 1;
            let opcode = (w >> 16) & 7;
            let rmode = (w >> 19) & 3;
            match (rmode, opcode) {
                (0, 2) | (0, 3) => {
                    assert!(sf, "unknown instruction {w:#010x}");
                    let n = self.get(rn as u32);
                    let signed = opcode == 2;
                    self.f[rd] = u128::from(match (double, signed) {
                        (true, true) => (n as i64 as f64).to_bits(),
                        (true, false) => (n as f64).to_bits(),
                        (false, true) => u64::from((n as i64 as f32).to_bits()),
                        (false, false) => u64::from((n as f32).to_bits()),
                    });
                }
                (3, 0) | (3, 1) => {
                    assert!(sf, "unknown instruction {w:#010x}");
                    let x = if double { f64::from_bits(a) } else { f64::from(f32s(a)) };
                    let n = if opcode == 0 { x as i64 as u64 } else { x as u64 };
                    self.set(rd as u32, n);
                }
                (0, 6) => {
                    let n = if sf { a } else { a & 0xffff_ffff };
                    self.set(rd as u32, n);
                }
                (0, 7) => {
                    let n = self.get(rn as u32);
                    self.f[rd] = u128::from(if sf { n } else { n & 0xffff_ffff });
                }
                _ => panic!("unknown instruction {w:#010x}"),
            }
        } else {
            panic!("unknown instruction {w:#010x}");
        }
    }

    /// Runs one instruction of vectors, and says whether the word was one. Each is
    /// done lane by lane, with the host's own integers and floats, which round to
    /// the nearest, ties to even, as the chip does. A number that is not one is
    /// the host's, so a test takes any such number for any other.
    fn vector_step(&mut self, w: u32) -> bool {
        let (rd, rn, rm) = ((w & 31) as usize, ((w >> 5) & 31) as usize, ((w >> 16) & 31) as usize);
        let size = w >> 22 & 3;
        let (a, b) = (self.f[rn], self.f[rm]);
        // The number of bits of a lane of the size, and how many lanes there are.
        let bits = 8u32 << size;
        let lanes = 128 / bits;
        let lane = |v: u128, i: u32, bits: u32| -> u64 {
            let mask = if bits == 64 { u64::MAX as u128 } else { (1u128 << bits) - 1 };
            ((v >> (i * bits)) & mask) as u64
        };
        let make = |bits: u32, each: &dyn Fn(u32) -> u64| -> u128 {
            (0..128 / bits).fold(0u128, |v, i| {
                let mask = if bits == 64 { u64::MAX as u128 } else { (1u128 << bits) - 1 };
                v | ((u128::from(each(i)) & mask) << (i * bits))
            })
        };
        let signed = |n: u64, bits: u32| -> i64 { ((n << (64 - bits)) as i64) >> (64 - bits) };
        let ones = |bits: u32| -> u64 { if bits == 64 { u64::MAX } else { (1u64 << bits) - 1 } };
        if w & 0xffbf_fc00 == 0x6ea0_f800 {
            // fneg vd.4s or vd.2d, vn
            let flip = if size & 1 == 1 {
                (1u128 << 63) | (1u128 << 127)
            } else {
                (0..4).fold(0u128, |v, i| v | (1u128 << (32 * i + 31)))
            };
            self.f[rd] = a ^ flip;
            return true;
        }
        if w & 0x9f20_0400 == 0x0e20_0400 {
            // Three registers of the same arrangement.
            assert!(w >> 30 & 1 == 1, "unknown instruction {w:#010x}");
            let (u, opcode) = (w >> 29 & 1 == 1, w >> 11 & 31);
            let double = w >> 22 & 1 == 1;
            let float = |each: &dyn Fn(f64, f64) -> f64, each32: &dyn Fn(f32, f32) -> f32| -> u128 {
                if double {
                    make(64, &|i| each(f64::from_bits(lane(a, i, 64)), f64::from_bits(lane(b, i, 64))).to_bits())
                } else {
                    make(32, &|i| {
                        u64::from(each32(f32::from_bits(lane(a, i, 32) as u32), f32::from_bits(lane(b, i, 32) as u32)).to_bits())
                    })
                }
            };
            let compare = |holds: &dyn Fn(f64, f64) -> bool, holds32: &dyn Fn(f32, f32) -> bool| -> u128 {
                if double {
                    make(64, &|i| if holds(f64::from_bits(lane(a, i, 64)), f64::from_bits(lane(b, i, 64))) { u64::MAX } else { 0 })
                } else {
                    make(32, &|i| {
                        if holds32(f32::from_bits(lane(a, i, 32) as u32), f32::from_bits(lane(b, i, 32) as u32)) { u64::MAX } else { 0 }
                    })
                }
            };
            self.f[rd] = match (u, opcode) {
                (false, 0x10) => make(bits, &|i| lane(a, i, bits).wrapping_add(lane(b, i, bits))),
                (true, 0x10) => make(bits, &|i| lane(a, i, bits).wrapping_sub(lane(b, i, bits))),
                (false, 0x13) => {
                    assert!(size < 3, "unknown instruction {w:#010x}");
                    make(bits, &|i| lane(a, i, bits).wrapping_mul(lane(b, i, bits)))
                }
                (false, 0x03) => match size {
                    0 => a & b,
                    2 => a | b,
                    _ => panic!("unknown instruction {w:#010x}"),
                },
                (true, 0x03) => match size {
                    0 => a ^ b,
                    // bsl: the destination holds the mask.
                    1 => (self.f[rd] & a) | (!self.f[rd] & b),
                    _ => panic!("unknown instruction {w:#010x}"),
                },
                (true, 0x11) => make(bits, &|i| if lane(a, i, bits) == lane(b, i, bits) { ones(bits) } else { 0 }),
                (false, 0x06) => make(bits, &|i| {
                    if signed(lane(a, i, bits), bits) > signed(lane(b, i, bits), bits) { ones(bits) } else { 0 }
                }),
                (false, 0x07) => make(bits, &|i| {
                    if signed(lane(a, i, bits), bits) >= signed(lane(b, i, bits), bits) { ones(bits) } else { 0 }
                }),
                (true, 0x06) => make(bits, &|i| if lane(a, i, bits) > lane(b, i, bits) { ones(bits) } else { 0 }),
                (true, 0x07) => make(bits, &|i| if lane(a, i, bits) >= lane(b, i, bits) { ones(bits) } else { 0 }),
                (false, 0x19) => {
                    // fmla and fmls add the product to the destination, or take
                    // it from it, rounding once.
                    let (acc, take) = (self.f[rd], w >> 23 & 1 == 1);
                    if double {
                        make(64, &|i| {
                            let x = f64::from_bits(lane(a, i, 64));
                            let x = if take { -x } else { x };
                            x.mul_add(f64::from_bits(lane(b, i, 64)), f64::from_bits(lane(acc, i, 64))).to_bits()
                        })
                    } else {
                        make(32, &|i| {
                            let x = f32::from_bits(lane(a, i, 32) as u32);
                            let x = if take { -x } else { x };
                            u64::from(
                                x.mul_add(f32::from_bits(lane(b, i, 32) as u32), f32::from_bits(lane(acc, i, 32) as u32))
                                    .to_bits(),
                            )
                        })
                    }
                }
                (false, 0x1a) if w >> 23 & 1 == 0 => float(&|x, y| x + y, &|x, y| x + y),
                (false, 0x1a) => float(&|x, y| x - y, &|x, y| x - y),
                (true, 0x1b) if w >> 23 & 1 == 0 => float(&|x, y| x * y, &|x, y| x * y),
                (true, 0x1a) if w >> 23 & 1 == 0 => {
                    // faddp: the pairs of the first, then the pairs of the second.
                    let l = if double { 64 } else { 32 };
                    let n = 128 / l;
                    let both = |i: u32| -> u64 {
                        let (v, j) = if i < n / 2 { (a, i) } else { (b, i - n / 2) };
                        let (x, y) = (lane(v, 2 * j, l), lane(v, 2 * j + 1, l));
                        if double {
                            (f64::from_bits(x) + f64::from_bits(y)).to_bits()
                        } else {
                            u64::from((f32::from_bits(x as u32) + f32::from_bits(y as u32)).to_bits())
                        }
                    };
                    make(l, &both)
                }
                (false, 0x1c) if w >> 23 & 1 == 0 => compare(&|x, y| x == y, &|x, y| x == y),
                (true, 0x1c) if w >> 23 & 1 == 0 => compare(&|x, y| x >= y, &|x, y| x >= y),
                (true, 0x1c) => compare(&|x, y| x > y, &|x, y| x > y),
                _ => panic!("unknown instruction {w:#010x}"),
            };
            true
        } else if w & 0x9f3e_0c00 == 0x0e20_0800 {
            // Two registers: not, and a comparison with zero.
            assert!(w >> 30 & 1 == 1, "unknown instruction {w:#010x}");
            let (u, opcode) = (w >> 29 & 1 == 1, w >> 12 & 31);
            self.f[rd] = match (u, opcode) {
                (true, 0x05) => !a,
                (false, 0x0a) => make(bits, &|i| if signed(lane(a, i, bits), bits) < 0 { ones(bits) } else { 0 }),
                _ => panic!("unknown instruction {w:#010x}"),
            };
            true
        } else if w & 0x9f3e_0c00 == 0x0e30_0800 {
            // addv: the sum of every lane, in the first lane of the register.
            assert!(w >> 29 & 7 == 2 && w >> 12 & 31 == 0x1b && size < 3, "unknown instruction {w:#010x}");
            let sum = (0..lanes).fold(0u64, |t, i| t.wrapping_add(lane(a, i, bits)));
            self.f[rd] = u128::from(sum & ones(bits));
            true
        } else if w & 0x9fe0_8400 == 0x0e00_0400 {
            // dup, umov, and ins, from a register or a lane.
            assert!(w >> 30 & 1 == 1 || w >> 11 & 15 == 7, "unknown instruction {w:#010x}");
            let imm5 = w >> 16 & 31;
            let log = imm5.trailing_zeros();
            let lane_bits = 8u32 << log;
            let at = imm5 >> (log + 1);
            let imm4 = w >> 11 & 15;
            let element = w >> 29 & 1 == 1;
            match (element, imm4) {
                (false, 0x0) => {
                    // dup vd.T, vn.T[at]
                    let n = lane(a, at, lane_bits);
                    self.f[rd] = make(lane_bits, &|_| n);
                }
                (false, 0x1) => {
                    // dup vd.T, xn
                    let n = self.get(rn as u32);
                    self.f[rd] = make(lane_bits, &|_| n);
                }
                (false, 0x7) => {
                    // umov wd, vn.T[at] or xd
                    let n = lane(a, at, lane_bits);
                    self.set(rd as u32, n);
                }
                (false, 0x3) => {
                    // ins vd.T[at], xn
                    let n = u128::from(self.get(rn as u32) & ones(lane_bits));
                    let mask = (if lane_bits == 64 { u64::MAX as u128 } else { (1u128 << lane_bits) - 1 }) << (at * lane_bits);
                    self.f[rd] = (self.f[rd] & !mask) | (n << (at * lane_bits));
                }
                (true, _) => {
                    // ins vd.T[at], vn.T[imm4 >> log]
                    let from = imm4 >> log;
                    let n = u128::from(lane(a, from, lane_bits));
                    let mask = (if lane_bits == 64 { u64::MAX as u128 } else { (1u128 << lane_bits) - 1 }) << (at * lane_bits);
                    self.f[rd] = (self.f[rd] & !mask) | (n << (at * lane_bits));
                }
                _ => panic!("unknown instruction {w:#010x}"),
            }
            true
        } else if w & 0xdfe0_8400 == 0x5e00_0400 && w >> 11 & 15 == 0 {
            // dup sd or dd, vn.T[at]: a lane as a float.
            let imm5 = w >> 16 & 31;
            let log = imm5.trailing_zeros();
            let n = lane(a, imm5 >> (log + 1), 8 << log);
            self.f[rd] = u128::from(n);
            true
        } else if w & 0x9ff8_0400 == 0x0f00_0400 && w >> 19 & 15 == 0 {
            // movi vd.2d, #imm: each bit of the immediate is a byte.
            assert!(w >> 30 & 1 == 1 && w >> 29 & 1 == 1 && w >> 12 & 15 == 0xe, "unknown instruction {w:#010x}");
            let imm8 = (w >> 16 & 7) << 5 | (w >> 5 & 31);
            let half = (0..8).fold(0u64, |t, i| if imm8 >> i & 1 == 1 { t | 0xffu64 << (8 * i) } else { t });
            self.f[rd] = u128::from(half) | (u128::from(half) << 64);
            true
        } else if w & 0x9f80_0400 == 0x0f00_0400 {
            // shrn vd.Tb, vn.Ta, #shift
            assert!(w >> 29 & 7 == 0 && w >> 11 & 31 == 0x10, "unknown instruction {w:#010x}");
            let immhb = w >> 16 & 127;
            let (from, shift) = match w >> 19 & 15 {
                1 => (16, 16 - immhb),
                2 | 3 => (32, 32 - immhb),
                _ => (64, 64 - immhb),
            };
            let to = from / 2;
            // The lanes narrowed fill the low half of the register, and the high half
            // is zeros.
            self.f[rd] = (0..64 / to).fold(0u128, |v, i| {
                v | (u128::from(lane(a, i, from) >> shift & ones(to)) << (i * to))
            });
            true
        } else if w & 0xdf3e_0c00 == 0x5e30_0800 {
            // The scalar pairs: addp dd, vn.2d, and faddp sd or dd.
            let (u, opcode) = (w >> 29 & 1 == 1, w >> 12 & 31);
            self.f[rd] = match (u, opcode) {
                (false, 0x1b) => u128::from(lane(a, 0, 64).wrapping_add(lane(a, 1, 64))),
                (true, 0x0d) if w >> 22 & 1 == 1 => u128::from(
                    (f64::from_bits(lane(a, 0, 64)) + f64::from_bits(lane(a, 1, 64))).to_bits(),
                ),
                (true, 0x0d) => u128::from(
                    (f32::from_bits(lane(a, 0, 32) as u32) + f32::from_bits(lane(a, 1, 32) as u32)).to_bits(),
                ),
                _ => panic!("unknown instruction {w:#010x}"),
            };
            true
        } else {
            false
        }
    }

    fn flags(&mut self, result: u64, c: bool, v: bool, bits: u32) {
        let mask = if bits == 64 { u64::MAX } else { (1 << bits) - 1 };
        self.n = result >> (bits - 1) & 1 == 1;
        self.z = result & mask == 0;
        self.c = c;
        self.v = v;
    }

    /// A call to the kernel, which is told the number in x8.
    fn system_call(&mut self) -> Option<i32> {
        match self.x[8] {
            64 => {
                let (fd, address, len) = (self.x[0], self.x[1], self.x[2]);
                let bytes = self.region(address, len).to_vec();
                match fd {
                    1 => self.stdout.extend_from_slice(&bytes),
                    2 => self.stderr.extend_from_slice(&bytes),
                    _ => {}
                }
                self.x[0] = len;
                None
            }
            // `getrandom(buffer, count, flags)`: gives the number of bytes it
            // filled, or minus the error number.
            278 => {
                let (address, len, flags) = (self.x[0], self.x[1], self.x[2]);
                assert_eq!(flags, 0, "getrandom with flags");
                self.asked += 1;
                let most = match self.entropy {
                    Entropy::Refused => {
                        self.x[0] = (-5i64) as u64;
                        return None;
                    }
                    Entropy::Interrupted if self.asked == 1 => {
                        self.x[0] = (-4i64) as u64;
                        return None;
                    }
                    Entropy::ByPieces(n) => n,
                    _ => len,
                };
                let count = len.min(most);
                for k in 0..count {
                    self.random ^= self.random << 13;
                    self.random ^= self.random >> 7;
                    self.random ^= self.random << 17;
                    self.write(address + k, 1, self.random >> 24 & 0xff);
                }
                self.x[0] = count;
                None
            }
            94 => Some(self.x[0] as u8 as i32),
            222 => {
                let len = self.x[1].div_ceil(4096) * 4096;
                self.regions.push((self.next_heap, vec![0; len as usize]));
                self.x[0] = self.next_heap;
                self.next_heap += len + 4096;
                None
            }
            other => panic!("the program asked the kernel for call {other}"),
        }
    }
}
