//! Windows on x86-64: a PE executable for the console that reaches the
//! kernel through `KERNEL32.dll`, which the system's loader links. The
//! executable is written by `pe`.

use super::Part;
use super::pe::{
    self, EXIT_PROCESS, GET_COMMAND_LINE, GET_STD_HANDLE, MEM_COMMIT_RESERVE, MOST_PER_WRITE,
    Machine, PAGE_READWRITE, STD_ERROR_HANDLE, STD_OUTPUT_HANDLE, VIRTUAL_ALLOC, WRITE_FILE,
};
use super::x86_64::{self, Asm, Place, R8, R9, RAX, RBX, RCX, RDI, RDX, RSI, Reg, System, Z};
use crate::scalar::{Program, Stream};

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
/// argument of `WriteFile`, the count it writes back, and rbx, which the
/// write keeps the handle in and puts back.
const RESERVED: u32 = 64;
const FIFTH_ARGUMENT: i8 = 32;
const WRITTEN: i8 = 40;
const RBX_AT: u32 = 48;

/// The executable for the program.
pub fn emit(program: &Program) -> (Vec<u8>, Vec<Part>) {
    let (asm, data, parts) = x86_64::code::<Windows>(program);
    let size = asm.len() + data.iter().map(|(_, d)| d.len()).sum::<usize>();
    let imports = pe::imports(program.allocates(), program.takes_arguments());
    let mut at = 0;
    let executable = pe::executable(&MACHINE, size, imports, |code, iat| {
        at = code;
        asm.finish(&data, code, iat)
    });
    (executable, super::shifted(parts, at))
}

struct Windows;

impl System for Windows {
    const RESERVED: u32 = RESERVED;
    const STACK_BY_PAGE: bool = true;
    /// rsi and rdi, which calls preserve.
    const TEXT: (Reg, Reg) = (RSI, RDI);

    /// Writes every byte, retrying a write that is cut short, and jumps to
    /// `fail` if the system refuses it or writes nothing. A call goes
    /// through the import address table, slot by slot, and the handle is
    /// kept in rbx, which calls preserve.
    fn write(asm: &mut Asm, stream: Stream, fail: usize) {
        let again = asm.label();
        let done = asm.label();
        asm.store(Place::Frame(RBX_AT), RBX, 8);
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
        asm.load(RBX, 8, false, Place::Frame(RBX_AT));
    }

    fn exit(asm: &mut Asm, status: u32) {
        asm.mov32(RCX, status);
        asm.call_slot(EXIT_PROCESS);
    }

    fn end(asm: &mut Asm) {
        asm.mov(RCX, RDI);
        asm.call_slot(EXIT_PROCESS);
    }

    /// Reserves and commits fresh pages. A failure gives no address.
    fn alloc(asm: &mut Asm, fail: usize) {
        asm.mov32(RCX, 0);
        asm.mov(RDX, RSI);
        asm.mov32(R8, MEM_COMMIT_RESERVE);
        asm.mov32(R9, PAGE_READWRITE);
        asm.call_slot(VIRTUAL_ALLOC);
        asm.test(RAX);
        asm.jcc(Z, fail);
    }

    fn arguments_on_entry(_: &mut Asm) {}

    /// The system gives one command line and no words, which a count of all
    /// ones says.
    fn arguments_by_call(asm: &mut Asm) {
        asm.call_slot(GET_COMMAND_LINE);
        asm.mov(R8, RAX);
        asm.mov64(RDI, u64::MAX);
        asm.mov32(RSI, 0);
    }
}

fn handle(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => STD_OUTPUT_HANDLE,
        Stream::Stderr => STD_ERROR_HANDLE,
    }
}
