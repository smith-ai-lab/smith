//! macOS on x86-64: a Mach-O executable that reaches the kernel through
//! `libSystem`. The executable is written by `macho`, and its instructions
//! by `x86_64`.

use super::Part;
use super::macho::{self, EINTR, ERROR, EXIT, MMAP, MMAP_REQUEST, Machine, WRITE};
use super::x86_64::{
    self, Asm, L, LE, Place, R8, R9, R12, RAX, RBX, RCX, RDI, RDX, RSI, Reg, S, System, Z,
};
use crate::scalar::{Program, Stream};

const MACHINE: Machine = Machine {
    cpu_type: 0x0100_0007, // x86-64
    cpu_subtype: 3,        // every x86-64 processor
    page: 0x1000,
    min_os: 0x000a_0f00, // 10.15
};

/// The executable for the program.
pub fn emit(program: &Program) -> (Vec<u8>, Vec<Part>) {
    let (asm, data, parts) = x86_64::code::<Macos>(program);
    let size = asm.len() + data.iter().map(|(_, d)| d.len()).sum::<usize>();
    let imports = macho::imports(program.allocates());
    let mut at = 0;
    let executable = macho::executable(&MACHINE, size as u64, imports, |code, got| {
        at = code;
        asm.finish(&data, code, got)
    });
    (executable, super::shifted(parts, at))
}

struct Macos;

impl System for Macos {
    /// Room for rbx and r12, which the write below keeps its place and its
    /// length in, and puts back.
    const RESERVED: u32 = 16;
    const TEXT: (Reg, Reg) = (RSI, RDX);

    /// Writes every byte, retrying a write that is cut short or
    /// interrupted, and jumps to `fail` if the system refuses it. The place
    /// and the length still to write stay in rbx and r12, which calls keep.
    fn write(asm: &mut Asm, stream: Stream, fail: usize) {
        asm.store(Place::Frame(0), RBX, 8);
        asm.store(Place::Frame(8), R12, 8);
        asm.mov(RBX, RSI);
        asm.mov(R12, RDX);
        let again = asm.label();
        let done = asm.label();
        let refused = asm.label();
        let interrupted = asm.label();
        asm.bind(again);
        asm.test(R12);
        asm.jcc(Z, done);
        asm.mov32(RDI, fd(stream));
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
        asm.load(RBX, 8, false, Place::Frame(0));
        asm.load(R12, 8, false, Place::Frame(8));
    }

    /// The system is not asked yet, so it has given no bytes, and the program
    /// that asked for them says so and ends.
    fn entropy(asm: &mut Asm) {
        asm.mov32(RAX, 1);
    }

    fn end(asm: &mut Asm) {
        asm.call_slot(EXIT);
    }

    /// Maps fresh pages that only this process sees. A failure gives -1.
    fn alloc(asm: &mut Asm, fail: usize) {
        let (prot, flags) = MMAP_REQUEST;
        asm.mov32(RDI, 0);
        asm.mov32(RDX, prot);
        asm.mov32(RCX, flags);
        asm.mov64(R8, u64::MAX);
        asm.mov32(R9, 0);
        asm.call_slot(MMAP);
        asm.test(RAX);
        asm.jcc(S, fail);
    }

    /// The loader starts the program as it starts a C program's `main`, with
    /// the number of words in rdi and the address of their addresses in rsi.
    fn arguments_on_entry(asm: &mut Asm) {
        asm.mov32(R8, 0);
    }

    fn arguments_by_call(_: &mut Asm) {}
}

fn fd(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => 1,
        Stream::Stderr => 2,
    }
}
