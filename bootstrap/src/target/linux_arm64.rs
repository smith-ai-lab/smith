//! Linux on arm64: a static ELF executable that talks to the kernel
//! directly through system calls. Its instructions are the arm64 ones
//! `macos_arm64` uses, and its file is written by the ELF writer
//! `linux_x64` uses.

use super::arm64::{Asm, EQ, LE, X0, X1, X2, X8, X19, X20};
use super::elf::{self, CODE_ADDRESS, Machine};
use crate::ir::{Program, Step, Stream};

const SYS_WRITE: u16 = 64;
const SYS_EXIT_GROUP: u16 = 94;
const EINTR: u32 = 4;

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    elf::executable(Machine::Aarch64, &code(program))
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
    asm.mov_w(X0, 0); // the status is 0
    asm.bind(exit);
    asm.mov_w(X8, SYS_EXIT_GROUP);
    asm.svc();
    asm.bind(fail);
    asm.mov_w(X0, 1);
    asm.branch(exit);
    // Nothing is called through a table of addresses, so there is none.
    asm.finish(&data, CODE_ADDRESS, 0)
}

fn fd(stream: Stream) -> u16 {
    match stream {
        Stream::Stdout => 1,
    }
}

/// Writes every byte at `text`, retrying a write that is cut short or
/// interrupted, and branches to `fail` if the kernel refuses it.
fn write(asm: &mut Asm, fd: u16, text: usize, len: u64, fail: usize) {
    let again = asm.label();
    let done = asm.label();
    let refused = asm.label();
    asm.address(X19, text);
    asm.mov64(X20, len);
    asm.bind(again);
    asm.cbz(X20, done);
    asm.mov_w(X0, fd);
    asm.mov(X1, X19);
    asm.mov(X2, X20);
    asm.mov_w(X8, SYS_WRITE);
    asm.svc();
    // cmp x0, #0; b.le refused
    asm.word(0xf100_001f);
    asm.cond(LE, refused);
    // add x19, x19, x0; sub x20, x20, x0; b again
    asm.word(0x8b00_0000 | X0 << 16 | X19 << 5 | X19);
    asm.word(0xcb00_0000 | X0 << 16 | X20 << 5 | X20);
    asm.branch(again);
    asm.bind(refused);
    // The kernel gives a failure as minus its error number.
    // cmn x0, #EINTR; b.eq again; b fail
    asm.word(0xb100_001f | EINTR << 10 | X0 << 5);
    asm.cond(EQ, again);
    asm.branch(fail);
    asm.bind(done);
}
