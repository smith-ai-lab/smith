//! macOS on arm64: a Mach-O executable that reaches the kernel through
//! `libSystem`, and that carries the ad hoc code signature Apple silicon
//! requires. The executable is written by `macho`.

use super::arm64::{Asm, EQ, LE, LT, X0, X1, X2, X19, X20};
use super::macho::{self, EINTR, ERROR, EXIT, Machine, WRITE};
use crate::ir::{Program, Step, Stream};

const MACHINE: Machine = Machine {
    cpu_type: 0x0100_000c, // arm64
    cpu_subtype: 0,        // every arm64 processor
    page: 0x4000,
    min_os: 0x000b_0000, // 11.0, the first for Apple silicon
};

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    let (asm, data) = code(program);
    let size = asm.len() + data.iter().map(|(_, d)| d.len()).sum::<usize>();
    macho::executable(&MACHINE, size as u64, |code, got| {
        asm.finish(&data, code, got)
    })
}

/// The machine code, and the data it reads, each piece under its label.
fn code(program: &Program) -> (Asm, Vec<(usize, &[u8])>) {
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
    asm.mov_w(0, 0); // the status is 0
    asm.bind(exit);
    asm.call(EXIT);
    asm.bind(fail);
    asm.mov_w(0, 1);
    asm.branch(exit);
    (asm, data)
}

fn fd(stream: Stream) -> u16 {
    match stream {
        Stream::Stdout => 1,
    }
}

/// Writes every byte at `text`, retrying a write that is cut short or
/// interrupted, and branches to `fail` if the system refuses it.
fn write(asm: &mut Asm, fd: u16, text: usize, len: u64, fail: usize) {
    let again = asm.label();
    let done = asm.label();
    let refused = asm.label();
    let interrupted = asm.label();
    asm.address(X19, text);
    asm.mov64(X20, len);
    asm.bind(again);
    asm.cbz(X20, done);
    asm.mov_w(X0, fd);
    asm.mov(X1, X19);
    asm.mov(X2, X20);
    asm.call(WRITE);
    // cmp x0, #0; b.le refused
    asm.word(0xf100_001f);
    asm.cond(LE, refused);
    // add x19, x19, x0; sub x20, x20, x0; b again
    asm.word(0x8b00_0000 | X0 << 16 | X19 << 5 | X19);
    asm.word(0xcb00_0000 | X0 << 16 | X20 << 5 | X20);
    asm.branch(again);
    asm.bind(refused);
    // A write of nothing is refused; a failed one may be interrupted.
    asm.cond(LT, interrupted);
    asm.branch(fail);
    asm.bind(interrupted);
    asm.call(ERROR);
    // ldr w0, [x0]; cmp w0, #EINTR; b.eq again; b fail
    asm.word(0xb940_0000 | X0 << 5 | X0);
    asm.word(0x7100_001f | EINTR << 10 | X0 << 5);
    asm.cond(EQ, again);
    asm.branch(fail);
    asm.bind(done);
}
