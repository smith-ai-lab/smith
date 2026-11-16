//! Windows on arm64: a PE executable for the console that reaches the
//! kernel through `KERNEL32.dll`, which the system's loader links. Its
//! instructions are the arm64 ones the other arm64 targets use, and its
//! file is written by the PE writer `windows_x64` uses.

use super::arm64::{self, Asm, LS, System, X0, X1, X2, X3, X4, X19, X20, X21};
use super::pe::{
    self, EXIT_PROCESS, GET_STD_HANDLE, MOST_PER_WRITE, Machine, STD_OUTPUT_HANDLE, WRITE_FILE,
};
use crate::ir::{Program, Stream};

const MACHINE: Machine = Machine {
    machine: 0xaa64, // arm64
    // An executable image that can use addresses above 2 GiB. Windows on
    // arm64 loads an image at a place of its choosing, so the image does
    // not say its relocations are stripped. It needs none: the code
    // reaches its data and the import address table relative to itself.
    characteristics: 0x0002 | 0x0020,
    // Able to be loaded anywhere in a 64-bit address space, compatible
    // with data execution prevention, and aware of Terminal Server.
    dll_characteristics: 0x0020 | 0x0040 | 0x0100 | 0x8000,
    version: (6, 2), // Windows 8, the oldest a linker writes for arm64
};

/// The bytes at the bottom of each frame: the count `WriteFile` writes
/// back, in a space that keeps the stack on 16 bytes.
const RESERVED: u32 = 16;
const WRITTEN: u32 = 0;

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    let (asm, data) = arm64::code::<Windows>(program);
    let size = asm.len() + data.iter().map(|(_, d)| d.len()).sum::<usize>();
    pe::executable(&MACHINE, size, |code, iat| asm.finish(&data, code, iat))
}

struct Windows;

impl System for Windows {
    const RESERVED: u32 = RESERVED;

    /// Writes every byte, retrying a write that is cut short, and branches
    /// to `fail` if the system refuses it or writes nothing. A call goes
    /// through the import address table, slot by slot. The place, the
    /// length still to write, and the handle are kept in x19, x20, and x21,
    /// which calls preserve.
    fn write(asm: &mut Asm, stream: Stream, fail: usize) {
        let again = asm.label();
        let done = asm.label();
        asm.mov64(X0, u64::from(handle(stream)));
        asm.call(GET_STD_HANDLE);
        asm.mov(X21, X0);
        asm.bind(again);
        asm.cbz(X20, done);
        // WriteFile(handle, text, at most MOST_PER_WRITE bytes, &written, NULL)
        asm.mov64(X2, u64::from(MOST_PER_WRITE));
        asm.cmp(X20, X2);
        asm.csel(X2, X20, X2, LS);
        asm.mov(X0, X21);
        asm.mov(X1, X19);
        asm.stack_address(X3, WRITTEN);
        asm.mov_w(X4, 0);
        asm.call(WRITE_FILE);
        // It gives a BOOL, which is 32 bits.
        asm.cbz_w(X0, fail);
        asm.load32_stack(X0, WRITTEN);
        asm.cbz_w(X0, fail);
        // add x19, x19, x0; sub x20, x20, x0; b again
        asm.word(0x8b00_0000 | X0 << 16 | X19 << 5 | X19);
        asm.word(0xcb00_0000 | X0 << 16 | X20 << 5 | X20);
        asm.branch(again);
        asm.bind(done);
    }

    fn exit(asm: &mut Asm, status: u16) {
        asm.mov_w(X0, status);
        asm.call(EXIT_PROCESS);
    }
}

fn handle(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => STD_OUTPUT_HANDLE,
    }
}
