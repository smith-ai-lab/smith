//! Windows on x86-64: a PE executable for the console that reaches the
//! kernel through `KERNEL32.dll`, which the system's loader links. The
//! executable is written by `pe`.

use super::pe::{
    self, EXIT_PROCESS, GET_STD_HANDLE, MOST_PER_WRITE, Machine, STD_OUTPUT_HANDLE, WRITE_FILE,
};
use super::x86_64::{self, Asm, R8, R9, RAX, RBX, RCX, RDI, RDX, RSI, Reg, System, Z};
use crate::ir::{Program, Stream};

const MACHINE: Machine = Machine {
    machine: 0x8664, // x86-64
    // An executable image with no relocations, that can use addresses
    // above 2 GiB.
    characteristics: 0x0001 | 0x0002 | 0x0020,
    // Compatible with data execution prevention, and aware of Terminal
    // Server.
    dll_characteristics: 0x0100 | 0x8000,
    version: (6, 0), // Vista
};

/// The bytes at the bottom of each frame: 32 the callee may use, the fifth
/// argument of `WriteFile`, and the count it writes back.
const RESERVED: u32 = 48;
const FIFTH_ARGUMENT: i8 = 32;
const WRITTEN: i8 = 40;

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    let (asm, data) = x86_64::code::<Windows>(program);
    let size = asm.len() + data.iter().map(|(_, d)| d.len()).sum::<usize>();
    pe::executable(&MACHINE, size, |code, iat| asm.finish(&data, code, iat))
}

struct Windows;

impl System for Windows {
    const RESERVED: u32 = RESERVED;
    /// rsi and rdi, which calls preserve.
    const TEXT: (Reg, Reg) = (RSI, RDI);

    /// Writes every byte, retrying a write that is cut short, and jumps to
    /// `fail` if the system refuses it or writes nothing. A call goes
    /// through the import address table, slot by slot, and the handle is
    /// kept in rbx, which calls preserve.
    fn write(asm: &mut Asm, stream: Stream, fail: usize) {
        let again = asm.label();
        let done = asm.label();
        asm.mov32(RCX, handle(stream));
        asm.call_slot(GET_STD_HANDLE);
        asm.mov(RBX, RAX);
        asm.bind(again);
        asm.test(RDI);
        asm.jcc(Z, done);
        // WriteFile(handle, text, at most MOST_PER_WRITE bytes, &written, NULL)
        asm.mov(R8, RDI);
        asm.mov32(RAX, MOST_PER_WRITE);
        asm.cmp(R8, RAX);
        asm.cmova(R8, RAX);
        asm.mov(RCX, RBX);
        asm.mov(RDX, RSI);
        asm.stack_address(R9, WRITTEN);
        asm.store_stack_imm32(FIFTH_ARGUMENT, 0);
        asm.call_slot(WRITE_FILE);
        asm.test32(RAX);
        asm.jcc(Z, fail);
        asm.load32_stack(RAX, WRITTEN);
        asm.test32(RAX);
        asm.jcc(Z, fail);
        asm.add(RSI, RAX);
        asm.sub(RDI, RAX);
        asm.jmp(again);
        asm.bind(done);
    }

    fn exit(asm: &mut Asm, status: u32) {
        asm.mov32(RCX, status);
        asm.call_slot(EXIT_PROCESS);
    }
}

fn handle(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => STD_OUTPUT_HANDLE,
    }
}
