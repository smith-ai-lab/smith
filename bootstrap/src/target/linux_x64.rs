//! Linux on x86-64: a static ELF executable that talks to the kernel
//! directly through system calls, written by the shared ELF writer with
//! the shared x86-64 instructions.

use super::elf::{self, Machine};
use super::x86_64::{Asm, LE, RAX, RDI, RDX, RSI, Z};
use crate::ir::{Program, Step, Stream};

const SYS_WRITE: u32 = 1;
const SYS_EXIT_GROUP: u32 = 231;
const EINTR: i8 = 4;

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    elf::executable(Machine::X86_64, &code(program))
}

/// The machine code followed by the data it reads.
fn code(program: &Program) -> Vec<u8> {
    let mut asm = Asm::default();
    let exit = asm.label();
    let fail = asm.label();
    let mut data = Vec::new();
    for step in &program.steps {
        match step {
            Step::Write { stream, bytes } => {
                let text = asm.label();
                data.push((text, bytes.as_slice()));
                write(&mut asm, fd(*stream), text, bytes.len() as u64, fail);
            }
        }
    }
    asm.zero(RDI); // the status is 0
    asm.bind(exit);
    asm.mov32(RAX, SYS_EXIT_GROUP);
    asm.syscall();
    asm.bind(fail);
    asm.mov32(RDI, 1);
    asm.jmp(exit);
    // Nothing refers to a slot, and every offset is relative.
    asm.finish(&data, 0, 0)
}

fn fd(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => 1,
    }
}

/// Writes every byte at `text`, retrying a write that is cut short or
/// interrupted, and jumps to `fail` if the kernel refuses it.
fn write(asm: &mut Asm, fd: u32, text: usize, len: u64, fail: usize) {
    let again = asm.label();
    let done = asm.label();
    let refused = asm.label();
    asm.address(RSI, text);
    asm.mov64(RDX, len);
    asm.bind(again);
    asm.test(RDX);
    asm.jcc(Z, done);
    asm.mov32(RAX, SYS_WRITE);
    asm.mov32(RDI, fd);
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
