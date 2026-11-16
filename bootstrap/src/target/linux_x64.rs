//! Linux on x86-64: a static ELF executable that talks to the kernel
//! directly through system calls, written by the shared ELF writer with
//! the shared x86-64 instructions.

use super::elf::{self, Machine};
use super::x86_64::{self, Asm, LE, R8, R9, R10, RAX, RDI, RDX, RSI, Reg, S, System, Z};
use crate::ir::{Program, Stream};

const SYS_WRITE: u32 = 1;
const SYS_MMAP: u32 = 9;
const SYS_EXIT_GROUP: u32 = 231;
const EINTR: i8 = 4;
const PROT_READ_WRITE: u32 = 0x1 | 0x2;
const MAP_PRIVATE_ANONYMOUS: u32 = 0x02 | 0x20;

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    let (asm, data) = x86_64::code::<Linux>(program);
    // Nothing refers to a slot, and every offset is relative.
    elf::executable(Machine::X86_64, &asm.finish(&data, 0, 0))
}

struct Linux;

impl System for Linux {
    const RESERVED: u32 = 0;
    const TEXT: (Reg, Reg) = (RSI, RDX);

    /// Writes every byte, retrying a write that is cut short or
    /// interrupted, and jumps to `fail` if the kernel refuses it.
    fn write(asm: &mut Asm, stream: Stream, fail: usize) {
        let again = asm.label();
        let done = asm.label();
        let refused = asm.label();
        asm.bind(again);
        asm.test(RDX);
        asm.jcc(Z, done);
        asm.mov32(RAX, SYS_WRITE);
        asm.mov32(RDI, fd(stream));
        asm.syscall();
        asm.test(RAX);
        asm.jcc(LE, refused);
        asm.add(RSI, RAX);
        asm.sub(RDX, RAX);
        asm.jmp(again);
        asm.bind(refused);
        asm.cmp_imm8(RAX, -EINTR);
        asm.jcc(Z, again);
        asm.jmp(fail);
        asm.bind(done);
    }

    fn exit(asm: &mut Asm, status: u32) {
        asm.mov32(RDI, status);
        asm.mov32(RAX, SYS_EXIT_GROUP);
        asm.syscall();
    }

    /// Maps fresh pages that only this process sees. The kernel gives a
    /// failure as minus its error number.
    fn alloc(asm: &mut Asm, fail: usize) {
        asm.mov32(RDI, 0);
        asm.mov32(RDX, PROT_READ_WRITE);
        asm.mov32(R10, MAP_PRIVATE_ANONYMOUS);
        asm.mov64(R8, u64::MAX);
        asm.mov32(R9, 0);
        asm.mov32(RAX, SYS_MMAP);
        asm.syscall();
        asm.test(RAX);
        asm.jcc(S, fail);
    }
}

fn fd(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => 1,
    }
}
