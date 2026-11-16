//! Linux on arm64: a static ELF executable that talks to the kernel
//! directly through system calls. Its instructions are the arm64 ones
//! `macos_arm64` uses, and its file is written by the ELF writer
//! `linux_x64` uses.

use super::arm64::{self, Asm, EQ, LE, LT, System, X0, X1, X2, X3, X4, X5, X8, X19, X20};
use super::elf::{self, CODE_ADDRESS, Machine};
use crate::ir::{Program, Stream};

const SYS_WRITE: u16 = 64;
const SYS_MMAP: u16 = 222;
const PROT_READ_WRITE: u16 = 0x1 | 0x2;
const MAP_PRIVATE_ANONYMOUS: u16 = 0x02 | 0x20;
const SYS_EXIT_GROUP: u16 = 94;
const EINTR: u32 = 4;

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    let (asm, data) = arm64::code::<Linux>(program);
    // Nothing is called through a table of addresses, so there is none.
    elf::executable(Machine::Aarch64, &asm.finish(&data, CODE_ADDRESS, 0))
}

struct Linux;

impl System for Linux {
    const RESERVED: u32 = 0;

    /// Writes every byte, retrying a write that is cut short or
    /// interrupted, and branches to `fail` if the kernel refuses it.
    fn write(asm: &mut Asm, stream: Stream, fail: usize) {
        let again = asm.label();
        let done = asm.label();
        let refused = asm.label();
        asm.bind(again);
        asm.cbz(X20, done);
        asm.mov_w(X0, fd(stream));
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

    fn exit(asm: &mut Asm, status: u16) {
        asm.mov_w(X0, status);
        asm.mov_w(X8, SYS_EXIT_GROUP);
        asm.svc();
    }

    /// Maps fresh pages that only this process sees. The kernel gives a
    /// failure as minus its error number.
    fn alloc(asm: &mut Asm, fail: usize) {
        asm.mov_w(X0, 0);
        asm.mov_w(X2, PROT_READ_WRITE);
        asm.mov_w(X3, MAP_PRIVATE_ANONYMOUS);
        asm.mov64(X4, u64::MAX);
        asm.mov_w(X5, 0);
        asm.mov_w(X8, SYS_MMAP);
        asm.svc();
        // cmp x0, #0; b.lt fail
        asm.word(0xf100_001f);
        asm.cond(LT, fail);
    }
}

fn fd(stream: Stream) -> u16 {
    match stream {
        Stream::Stdout => 1,
    }
}
