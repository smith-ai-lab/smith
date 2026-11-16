//! macOS on x86-64: a Mach-O executable that reaches the kernel through
//! `libSystem`. The executable is written by `macho`, and its instructions
//! by `x86_64`.

use super::macho::{self, EINTR, ERROR, EXIT, Machine, WRITE};
use super::x86_64::{Asm, L, LE, R12, RAX, RBX, RDI, RDX, RSI, Z};
use crate::ir::{Program, Step, Stream};

const MACHINE: Machine = Machine {
    cpu_type: 0x0100_0007, // x86-64
    cpu_subtype: 3,        // every x86-64 processor
    page: 0x1000,
    min_os: 0x000a_0f00, // 10.15
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
    // The entry is called, so the stack is 8 bytes short of the 16-byte
    // alignment every call needs. The program never returns.
    asm.reserve_stack(8);
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
    asm.call_slot(EXIT);
    asm.bind(fail);
    asm.mov32(RDI, 1);
    asm.jmp(exit);
    (asm, data)
}

fn fd(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => 1,
    }
}

/// Writes every byte at `text`, retrying a write that is cut short or
/// interrupted, and jumps to `fail` if the system refuses it. The place
/// and the length still to write are kept in rbx and r12, which calls
/// keep.
fn write(asm: &mut Asm, fd: u32, text: usize, len: u64, fail: usize) {
    let again = asm.label();
    let done = asm.label();
    let refused = asm.label();
    let interrupted = asm.label();
    asm.address(RBX, text);
    asm.mov64(R12, len);
    asm.bind(again);
    asm.test(R12);
    asm.jcc(Z, done);
    asm.mov32(RDI, fd);
    asm.mov(RSI, RBX);
    asm.mov(RDX, R12);
    asm.call_slot(WRITE);
    asm.test(RAX);
    asm.jcc(LE, refused);
    asm.add(RBX, RAX);
    asm.sub(R12, RAX);
    asm.jmp(again);
    asm.bind(refused);
    // A write of nothing is refused; a failed one may be interrupted.
    asm.jcc(L, interrupted);
    asm.jmp(fail);
    asm.bind(interrupted);
    asm.call_slot(ERROR);
    asm.cmp_load32_imm8(RAX, EINTR as i8);
    asm.jcc(Z, again);
    asm.jmp(fail);
    asm.bind(done);
}
