//! Windows on arm64: a PE executable for the console that reaches the
//! kernel through `KERNEL32.dll`, which the system's loader links. Its
//! instructions are the arm64 ones the other arm64 targets use, and its
//! file is written by the PE writer `windows_x64` uses.

use super::Part;
use super::arm64::{self, Asm, LS, Place, System, X0, X1, X2, X3, X4, X19, X20, X21};
use super::pe::{
    self, EXIT_PROCESS, GET_COMMAND_LINE, GET_STD_HANDLE, MEM_COMMIT_RESERVE, MOST_PER_WRITE,
    Machine, PAGE_READWRITE, STD_ERROR_HANDLE, STD_OUTPUT_HANDLE, VIRTUAL_ALLOC, WRITE_FILE,
};
use crate::scalar::{Program, Stream};

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
/// back, and x19, x20, and x21, which the write keeps what it needs in and
/// puts back.
const RESERVED: u32 = 32;
const WRITTEN: u32 = 0;
const SAVED: u32 = 8;

/// The executable for the program.
pub fn emit(program: &Program) -> (Vec<u8>, Vec<Part>) {
    let (asm, data, parts) = arm64::code::<Windows>(program);
    let size = asm.len() + data.iter().map(|(_, d)| d.len()).sum::<usize>();
    let imports = pe::imports(program.allocates(), program.takes_arguments());
    let mut at = 0;
    let executable = pe::executable(&MACHINE, size, imports, |code, iat| {
        at = code;
        asm.finish(&data, code, iat)
    });
    (executable, super::shifted(parts, pe::IMAGE_BASE + at))
}

struct Windows;

impl System for Windows {
    const RESERVED: u32 = RESERVED;
    const STACK_BY_PAGE: bool = true;

    /// Writes every byte, retrying a write that is cut short, and branches
    /// to `fail` if the system refuses it or writes nothing. A call goes
    /// through the import address table, slot by slot. The place, the
    /// length still to write, and the handle are kept in x19, x20, and x21,
    /// which calls preserve.
    fn write(asm: &mut Asm, stream: Stream, fail: usize) {
        let again = asm.label();
        let done = asm.label();
        for (i, r) in [X19, X20, X21].into_iter().enumerate() {
            asm.store(Place::Frame(SAVED + 8 * i as u32), r, 8);
        }
        asm.mov(X19, X1);
        asm.mov(X20, X2);
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
        asm.load(X0, 4, false, Place::Frame(WRITTEN));
        asm.cbz_w(X0, fail);
        // add x19, x19, x0; sub x20, x20, x0; b again
        asm.word(0x8b00_0000 | X0 << 16 | X19 << 5 | X19);
        asm.word(0xcb00_0000 | X0 << 16 | X20 << 5 | X20);
        asm.branch(again);
        asm.bind(done);
        for (i, r) in [X19, X20, X21].into_iter().enumerate() {
            asm.load(r, 8, false, Place::Frame(SAVED + 8 * i as u32));
        }
    }

    fn end(asm: &mut Asm) {
        asm.call(EXIT_PROCESS);
    }

    /// Reserves and commits fresh pages. A failure gives no address.
    fn alloc(asm: &mut Asm, fail: usize) {
        asm.mov_w(X0, 0);
        asm.mov64(X2, u64::from(MEM_COMMIT_RESERVE));
        asm.mov_w(X3, PAGE_READWRITE as u16);
        asm.call(VIRTUAL_ALLOC);
        asm.cbz(X0, fail);
    }

    fn arguments_on_entry(_: &mut Asm) {}

    /// The system gives one command line and no words, which a count of all
    /// ones says.
    fn arguments_by_call(asm: &mut Asm) {
        asm.call(GET_COMMAND_LINE);
        asm.mov(X2, X0);
        asm.mov64(X0, u64::MAX);
        asm.mov_w(X1, 0);
    }
}

fn handle(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => STD_OUTPUT_HANDLE,
        Stream::Stderr => STD_ERROR_HANDLE,
    }
}
