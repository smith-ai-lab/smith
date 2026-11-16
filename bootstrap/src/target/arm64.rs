//! The arm64 instructions the arm64 targets share, with labels resolved
//! once the code and its data are laid out.

pub const X0: u32 = 0;
pub const X1: u32 = 1;
pub const X2: u32 = 2;
/// Holds the number of a Linux system call.
pub const X8: u32 = 8;
/// Kept across calls, so they hold the place and the length still to write.
pub const X19: u32 = 19;
pub const X20: u32 = 20;
pub const LE: u32 = 0xd;
pub const EQ: u32 = 0x0;
pub const LT: u32 = 0xb;

/// A place in the code that refers to something placed later.
enum Fixup {
    /// `b`, with a 26-bit word offset.
    Branch { at: usize, label: usize },
    /// `b.cond` or `cbz`, with a 19-bit word offset.
    Conditional { at: usize, label: usize },
    /// `adrp` then `add`, which form the address of a label.
    Address { at: usize, label: usize },
    /// `adrp` then `ldr`, which load a slot of the table of addresses,
    /// such as `__got`.
    Slot { at: usize, slot: usize },
}

/// Machine code with labels, each resolved once the code and its data are
/// laid out.
#[derive(Default)]
pub struct Asm {
    code: Vec<u8>,
    labels: Vec<Option<usize>>,
    fixups: Vec<Fixup>,
}

impl Asm {
    pub fn label(&mut self) -> usize {
        self.labels.push(None);
        self.labels.len() - 1
    }

    pub fn bind(&mut self, label: usize) {
        self.labels[label] = Some(self.code.len());
    }

    pub fn word(&mut self, word: u32) {
        self.code.extend_from_slice(&word.to_le_bytes());
    }

    pub fn branch(&mut self, label: usize) {
        self.fixups.push(Fixup::Branch {
            at: self.code.len(),
            label,
        });
        self.word(0x1400_0000);
    }

    pub fn cond(&mut self, cond: u32, label: usize) {
        self.fixups.push(Fixup::Conditional {
            at: self.code.len(),
            label,
        });
        self.word(0x5400_0000 | cond);
    }

    pub fn cbz(&mut self, reg: u32, label: usize) {
        self.fixups.push(Fixup::Conditional {
            at: self.code.len(),
            label,
        });
        self.word(0xb400_0000 | reg);
    }

    /// `mov xd, xn`.
    pub fn mov(&mut self, rd: u32, rn: u32) {
        self.word(0xaa00_03e0 | rn << 16 | rd);
    }

    /// `movz wd, #value`.
    pub fn mov_w(&mut self, rd: u32, value: u16) {
        self.word(0x5280_0000 | (value as u32) << 5 | rd);
    }

    /// `movz` then a `movk` for each further nonzero half-word.
    pub fn mov64(&mut self, rd: u32, value: u64) {
        self.word(0xd280_0000 | ((value & 0xffff) as u32) << 5 | rd);
        for hw in 1..4u32 {
            let part = ((value >> (16 * hw)) & 0xffff) as u32;
            if part != 0 {
                self.word(0xf280_0000 | hw << 21 | part << 5 | rd);
            }
        }
    }

    /// The address of a label into `rd`.
    pub fn address(&mut self, rd: u32, label: usize) {
        self.fixups.push(Fixup::Address {
            at: self.code.len(),
            label,
        });
        self.word(0x9000_0000 | rd);
        self.word(0x9100_0000 | rd << 5 | rd);
    }

    /// `svc #0`, which makes a system call.
    pub fn svc(&mut self) {
        self.word(0xd400_0001);
    }

    /// Calls the address in a slot of the table of addresses, through x16.
    pub fn call(&mut self, slot: usize) {
        const X16: u32 = 16;
        self.fixups.push(Fixup::Slot {
            at: self.code.len(),
            slot,
        });
        self.word(0x9000_0000 | X16);
        self.word(0xf940_0000 | X16 << 5 | X16);
        self.word(0xd63f_0000 | X16 << 5); // blr x16
    }

    fn patch(&mut self, at: usize, bits: u32) {
        let word = u32::from_le_bytes(self.code[at..at + 4].try_into().unwrap()) | bits;
        self.code[at..at + 4].copy_from_slice(&word.to_le_bytes());
    }

    /// The size of the code so far.
    pub fn len(&self) -> usize {
        self.code.len()
    }

    /// The code at `code_address` with each piece of data placed after it
    /// under its label, and the table of addresses at `slots_address`.
    pub fn finish(
        mut self,
        data: &[(usize, &[u8])],
        code_address: u64,
        slots_address: u64,
    ) -> Vec<u8> {
        for (label, bytes) in data {
            self.bind(*label);
            self.code.extend_from_slice(bytes);
        }
        let fixups = std::mem::take(&mut self.fixups);
        for fixup in fixups {
            match fixup {
                Fixup::Branch { at, label } => {
                    let words = self.offset(at, label) / 4;
                    self.patch(at, words as u32 & 0x3ff_ffff);
                }
                Fixup::Conditional { at, label } => {
                    let words = self.offset(at, label) / 4;
                    self.patch(at, (words as u32 & 0x7_ffff) << 5);
                }
                Fixup::Address { at, label } => {
                    let target = code_address + self.place(label) as u64;
                    self.page_and_low(at, code_address + at as u64, target, 0);
                }
                Fixup::Slot { at, slot } => {
                    let target = slots_address + 8 * slot as u64;
                    self.page_and_low(at, code_address + at as u64, target, 3);
                }
            }
        }
        self.code
    }

    fn place(&self, label: usize) -> usize {
        self.labels[label].expect("every label is bound")
    }

    fn offset(&self, at: usize, label: usize) -> i64 {
        self.place(label) as i64 - at as i64
    }

    /// Fills an `adrp` at `at` and the instruction after it, whose 12-bit
    /// offset is scaled down by `scale` bits, so that they reach `target`.
    fn page_and_low(&mut self, at: usize, pc: u64, target: u64, scale: u32) {
        let pages = ((target & !0xfff) as i64 - (pc & !0xfff) as i64) >> 12;
        let pages = pages as u32;
        self.patch(at, (pages & 0x3) << 29 | ((pages >> 2) & 0x7_ffff) << 5);
        let low = (target & 0xfff) as u32 >> scale;
        self.patch(at + 4, low << 10);
    }
}
