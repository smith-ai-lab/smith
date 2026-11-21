//! macOS on arm64: a Mach-O executable that reaches the kernel through
//! `libSystem`, and that carries the ad hoc code signature Apple silicon
//! requires. The executable is written by `macho`.

use super::Part;
use super::arm64::{self, Asm, EQ, LE, LT, Place, System, X0, X1, X2, X3, X4, X5, X19, X20};
use super::macho::{self, EINTR, ERROR, EXIT, MMAP, MMAP_REQUEST, Machine, WRITE};
use crate::scalar::{Program, Stream};

const MACHINE: Machine = Machine {
    cpu_type: 0x0100_000c, // arm64
    cpu_subtype: 0,        // every arm64 processor
    page: 0x4000,
    min_os: 0x000b_0000, // 11.0, the first for Apple silicon
};

/// The executable for the program.
pub fn emit(program: &Program) -> (Vec<u8>, Vec<Part>) {
    let (asm, data, parts) = arm64::code::<Macos>(program);
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
    /// Room for x19 and x20, which the write below keeps its place and its
    /// length in, and puts back.
    const RESERVED: u32 = 16;

    /// Writes every byte, retrying a write that is cut short or
    /// interrupted, and branches to `fail` if the system refuses it. The
    /// place and the length still to write stay in x19 and x20, which
    /// calls keep.
    fn write(asm: &mut Asm, stream: Stream, fail: usize) {
        asm.store(Place::Frame(0), X19, 8);
        asm.store(Place::Frame(8), X20, 8);
        asm.mov(X19, X1);
        asm.mov(X20, X2);
        let again = asm.label();
        let done = asm.label();
        let refused = asm.label();
        let interrupted = asm.label();
        asm.bind(again);
        asm.cbz(X20, done);
        asm.mov_w(X0, fd(stream));
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
        asm.load(X19, 8, false, Place::Frame(0));
        asm.load(X20, 8, false, Place::Frame(8));
    }

    /// The system is not asked yet, and `compile` refuses a program that asks
    /// for bytes for this target, so this is not reached.
    fn entropy(asm: &mut Asm) {
        asm.mov_w(X0, 1);
    }

    fn end(asm: &mut Asm) {
        asm.call(EXIT);
    }

    /// Maps fresh pages that only this process sees. A failure gives -1.
    fn alloc(asm: &mut Asm, fail: usize) {
        let (prot, flags) = MMAP_REQUEST;
        asm.mov_w(X0, 0);
        asm.mov_w(X2, prot as u16);
        asm.mov_w(X3, flags as u16);
        asm.mov64(X4, u64::MAX);
        asm.mov_w(X5, 0);
        asm.call(MMAP);
        // cmp x0, #0; b.lt fail
        asm.word(0xf100_001f);
        asm.cond(LT, fail);
    }

    /// The loader starts the program as it starts a C program's `main`, with
    /// the number of words in x0 and the address of their addresses in x1.
    fn arguments_on_entry(asm: &mut Asm) {
        asm.mov_w(X2, 0);
    }

    fn arguments_by_call(_: &mut Asm) {}
}

fn fd(stream: Stream) -> u16 {
    match stream {
        Stream::Stdout => 1,
        Stream::Stderr => 2,
    }
}
