//! Linux on x86-64: a static ELF executable that talks to the kernel
//! directly through system calls, written by the shared ELF writer with
//! the shared x86-64 instructions.

use super::elf::{self, Machine};
use super::x86_64::{self, Asm, LE, RAX, RDI, RDX, RSI, Reg, System, Z};
use crate::ir::{Program, Stream};

const SYS_WRITE: u32 = 1;
const SYS_EXIT_GROUP: u32 = 231;
const EINTR: i8 = 4;

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
}

fn fd(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => 1,
    }
}
