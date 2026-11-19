//! The check an executable for x86-64 makes of its chip, once, before `main`.
//! The executable is built for the level `x86-64-v3`, so a chip that lacks
//! anything the level has stops it with one line on the error output that
//! names what is missing, and the program runs nothing of its own.
//!
//! The level is what `cpuid` says of the chip, and what the system says of the
//! registers it saves: every feature below, and the system's leave to use the
//! registers of AVX. The checks cost the same instructions for every program,
//! and a program's own code is never reached when one fails.

use super::{Asm, NE, RAX, RCX, Reg, System, Z};
use crate::scalar::Stream;

/// A feature that `cpuid` reports in one bit of one register, and the line
/// that says it is missing.
struct Feature {
    bit: u32,
    missing: &'static str,
}

macro_rules! feature {
    ($bit:expr, $name:literal) => {
        Feature {
            bit: $bit,
            missing: concat!(
                "smith: this program needs the level x86-64-v3, and this chip has no ",
                $name,
                "\n"
            ),
        }
    };
}

/// In `ecx` of leaf 1.
const LEAF_1: [Feature; 5] = [
    feature!(12, "FMA"),
    feature!(22, "MOVBE"),
    feature!(29, "F16C"),
    feature!(28, "AVX"),
    feature!(27, "XSAVE, which the system must turn on to use AVX"),
];
/// In `ebx` of leaf 7.
const LEAF_7: [Feature; 3] = [
    feature!(3, "BMI1"),
    feature!(5, "AVX2"),
    feature!(8, "BMI2"),
];
/// The place of AVX2 in `LEAF_7`, which a chip without leaf 7 lacks.
const AVX2: usize = 1;
/// In `ecx` of leaf `0x8000_0001`.
const EXTENDED: Feature = feature!(5, "LZCNT");
/// The registers of AVX, which the system saves when it has turned them on:
/// bit 1 of `xcr0` is the registers of SSE and bit 2 the upper halves of AVX.
const OS_MISSING: &str =
    "smith: this program needs the level x86-64-v3, and this system has not turned on AVX\n";

impl Asm {
    /// `cpuid` with `eax` and `ecx` as given; it changes `eax`, `ebx`, `ecx`,
    /// and `edx`.
    fn cpuid(&mut self, leaf: u32, sub: u32) {
        self.mov32(RAX, leaf);
        self.mov32(RCX, sub);
        self.bytes(&[0x0f, 0xa2]);
    }

    /// `test r32, 1 << bit` for `ecx` or `ebx`.
    fn test_bit(&mut self, r: Reg, bit: u32) {
        self.bytes(&[0xf7, 0xc0 | (r.0 & 7)]);
        self.bytes(&(1u32 << bit).to_le_bytes());
    }
}

/// Writes the check, which leaves rdi, rsi, and r8, and the stack, as they
/// were, and changes rax, rbx, rcx, and rdx. It ends in the data of the lines
/// it may write, which it returns with their labels.
pub fn check<S: System>(asm: &mut Asm, fail: usize) -> Vec<(usize, &'static [u8])> {
    let report = asm.label();
    let mut lines: Vec<(usize, &'static str)> = Vec::new();
    let mut line = |asm: &mut Asm, text: &'static str| {
        let label = asm.label();
        lines.push((label, text));
        label
    };
    // Where each failing test goes: to a few instructions that name the line.
    let mut jumps: Vec<(usize, usize)> = Vec::new();

    asm.cpuid(1, 0);
    for f in &LEAF_1 {
        let to = asm.label();
        let text = line(asm, f.missing);
        asm.test_bit(RCX, f.bit);
        asm.jcc(Z, to);
        jumps.push((to, text));
    }
    // Leaf 7 is there only when the highest leaf is 7 or more.
    let no_leaf_7 = asm.label();
    let avx2 = line(asm, LEAF_7[AVX2].missing);
    asm.cpuid(0, 0);
    asm.cmp_imm8(RAX, 7);
    asm.jcc(super::B, no_leaf_7);
    jumps.push((no_leaf_7, avx2));
    asm.cpuid(7, 0);
    for (i, f) in LEAF_7.iter().enumerate() {
        let to = asm.label();
        let text = if i == AVX2 {
            avx2
        } else {
            line(asm, f.missing)
        };
        asm.test_bit(super::RBX, f.bit);
        asm.jcc(Z, to);
        jumps.push((to, text));
    }
    asm.cpuid(0x8000_0001, 0);
    let lzcnt = asm.label();
    let lzcnt_text = line(asm, EXTENDED.missing);
    asm.test_bit(RCX, EXTENDED.bit);
    asm.jcc(Z, lzcnt);
    jumps.push((lzcnt, lzcnt_text));
    // `xgetbv` with `ecx` 0 gives `xcr0`; the XSAVE bit above says it exists.
    asm.mov32(RCX, 0);
    asm.bytes(&[0x0f, 0x01, 0xd0]);
    // `and eax, 6`
    asm.bytes(&[0x83, 0xe0, 0x06]);
    asm.cmp_imm8(RAX, 6);
    let os = asm.label();
    let os_text = line(asm, OS_MISSING);
    asm.jcc(NE, os);
    jumps.push((os, os_text));
    let ok = asm.label();
    asm.jmp(ok);

    let (address, length) = S::TEXT;
    for (to, text) in jumps {
        asm.bind(to);
        asm.address(address, text);
        let len = lines
            .iter()
            .find(|(l, _)| *l == text)
            .expect("a line")
            .1
            .len();
        asm.mov32(length, len as u32);
        asm.jmp(report);
    }
    asm.bind(report);
    S::write(asm, Stream::Stderr, fail);
    S::exit(asm, 1);
    asm.bind(ok);
    lines.into_iter().map(|(l, t)| (l, t.as_bytes())).collect()
}
