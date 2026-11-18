//! A machine that runs the static ELF executables the compiler writes for
//! arm64, so that what they do is checked on a machine of any kind. It reads
//! the instructions the compiler writes and no others, and stops with a
//! message that names the word when it meets one it does not know, so a
//! new instruction is a failing test and not a wrong answer.
//!
//! The kernel's part is three calls: `write` to the standard output, `mmap`
//! of fresh pages, and `exit_group`.

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
    sp: u64,
    pc: u64,
    /// The flags: negative, zero, carry, overflow.
    n: bool,
    z: bool,
    c: bool,
    v: bool,
    stdout: Vec<u8>,
    next_heap: u64,
}

/// Runs the executable, which must be one `is_executable` accepts.
pub fn run(bytes: &[u8]) -> Ran {
    assert!(is_executable(bytes), "not an arm64 executable");
    let mut m = Machine {
        regions: Vec::new(),
        x: [0; 31],
        sp: STACK_BASE + STACK_SIZE - 256,
        pc: u64_at(bytes, 24),
        n: false,
        z: false,
        c: false,
        v: false,
        stdout: Vec::new(),
        next_heap: HEAP_BASE,
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
            assert!(w >> 10 & 0x3f == 0 && w >> 22 & 3 == 0, "unknown instruction {w:#010x}");
            let (a, b) = (self.get(rn), self.get(rm));
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
            assert!(imms >= immr && imms < bits, "unknown instruction {w:#010x}");
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
            let result = if taken {
                a
            } else if w >> 10 & 1 == 1 {
                b.wrapping_add(1) & mask
            } else {
                b
            };
            assert!(w >> 30 & 1 == 0 && w >> 29 & 1 == 0, "unknown instruction {w:#010x}");
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
        } else if w & 0xffe0_fc00 == 0x9b00_7c00 {
            self.set(rd, self.get(rn).wrapping_mul(self.get(rm)));
        } else if w & 0xffe0_fc00 == 0x9bc0_7c00 {
            let product = self.get(rn) as u128 * self.get(rm) as u128;
            self.set(rd, (product >> 64) as u64);
        } else if w & 0xffe0_fc00 == 0x9b40_7c00 {
            let product = self.get(rn) as i64 as i128 * self.get(rm) as i64 as i128;
            self.set(rd, (product >> 64) as u64);
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
                if fd == 1 {
                    self.stdout.extend_from_slice(&bytes);
                }
                self.x[0] = len;
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
