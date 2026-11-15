//! Linux on x86-64: a static ELF executable that talks to the kernel
//! directly through system calls.

use crate::ir::{Program, Step, Stream};

/// Where the executable is loaded.
const BASE: u64 = 0x40_0000;
const ELF_HEADER_SIZE: usize = 64;
const PROGRAM_HEADER_SIZE: usize = 56;
const HEADERS_SIZE: usize = ELF_HEADER_SIZE + PROGRAM_HEADER_SIZE;

const SYS_WRITE: u32 = 1;
const SYS_EXIT_GROUP: u32 = 231;
const EINTR: u8 = 4;

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    let image = code(program);
    let mut out = Vec::with_capacity(HEADERS_SIZE + image.len());
    elf_header(&mut out);
    program_header(&mut out, (HEADERS_SIZE + image.len()) as u64);
    out.extend_from_slice(&image);
    out
}

fn elf_header(out: &mut Vec<u8>) {
    out.extend_from_slice(&[0x7f, b'E', b'L', b'F']);
    // 64-bit, little-endian, ELF version 1, System V ABI, and padding.
    out.extend_from_slice(&[2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    out.extend_from_slice(&2u16.to_le_bytes()); // an executable
    out.extend_from_slice(&0x3eu16.to_le_bytes()); // x86-64
    out.extend_from_slice(&1u32.to_le_bytes()); // version
    out.extend_from_slice(&(BASE + HEADERS_SIZE as u64).to_le_bytes()); // entry
    out.extend_from_slice(&(ELF_HEADER_SIZE as u64).to_le_bytes()); // program headers
    out.extend_from_slice(&0u64.to_le_bytes()); // no section headers
    out.extend_from_slice(&0u32.to_le_bytes()); // flags
    out.extend_from_slice(&(ELF_HEADER_SIZE as u16).to_le_bytes());
    out.extend_from_slice(&(PROGRAM_HEADER_SIZE as u16).to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes()); // one program header
    out.extend_from_slice(&64u16.to_le_bytes()); // section header size
    out.extend_from_slice(&0u16.to_le_bytes()); // no sections
    out.extend_from_slice(&0u16.to_le_bytes()); // no section names
}

/// One segment, readable and executable, that maps the whole file.
fn program_header(out: &mut Vec<u8>, size: u64) {
    out.extend_from_slice(&1u32.to_le_bytes()); // a loadable segment
    out.extend_from_slice(&5u32.to_le_bytes()); // read and execute
    out.extend_from_slice(&0u64.to_le_bytes()); // file offset
    out.extend_from_slice(&BASE.to_le_bytes()); // virtual address
    out.extend_from_slice(&BASE.to_le_bytes()); // physical address
    out.extend_from_slice(&size.to_le_bytes()); // size in the file
    out.extend_from_slice(&size.to_le_bytes()); // size in memory
    out.extend_from_slice(&0x1000u64.to_le_bytes()); // alignment
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
    // xor edi, edi: the status is 0.
    asm.bytes(&[0x31, 0xff]);
    asm.bind(exit);
    asm.mov_eax(SYS_EXIT_GROUP);
    asm.syscall();
    asm.bind(fail);
    asm.mov_edi(1);
    asm.jump(&[0xe9], exit);
    asm.finish(&data)
}

fn fd(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => 1,
    }
}

/// Writes every byte at `text`, retrying a write that is cut short or
/// interrupted, and jumps to `fail` if the kernel refuses it.
fn write(asm: &mut Asm, fd: u32, text: usize, len: u64, fail: usize) {
    let again = asm.label();
    let done = asm.label();
    let refused = asm.label();
    // lea rsi, [rip + text]
    asm.jump(&[0x48, 0x8d, 0x35], text);
    // mov rdx, len
    asm.bytes(&[0x48, 0xba]);
    asm.bytes(&len.to_le_bytes());
    asm.bind(again);
    // test rdx, rdx; jz done
    asm.bytes(&[0x48, 0x85, 0xd2]);
    asm.jump(&[0x0f, 0x84], done);
    asm.mov_eax(SYS_WRITE);
    asm.mov_edi(fd);
    asm.syscall();
    // test rax, rax; jle refused
    asm.bytes(&[0x48, 0x85, 0xc0]);
    asm.jump(&[0x0f, 0x8e], refused);
    // add rsi, rax; sub rdx, rax; jmp again
    asm.bytes(&[0x48, 0x01, 0xc6, 0x48, 0x29, 0xc2]);
    asm.jump(&[0xe9], again);
    asm.bind(refused);
    // cmp rax, -EINTR; je again; jmp fail
    asm.bytes(&[0x48, 0x83, 0xf8, EINTR.wrapping_neg()]);
    asm.jump(&[0x0f, 0x84], again);
    asm.jump(&[0xe9], fail);
    asm.bind(done);
}

/// Machine code with labels, each resolved once the code and its data are
/// laid out.
#[derive(Default)]
struct Asm {
    code: Vec<u8>,
    labels: Vec<Option<usize>>,
    /// Places holding a 32-bit offset, from the end of those four bytes, to
    /// a label.
    fixups: Vec<(usize, usize)>,
}

impl Asm {
    fn label(&mut self) -> usize {
        self.labels.push(None);
        self.labels.len() - 1
    }

    fn bind(&mut self, label: usize) {
        self.labels[label] = Some(self.code.len());
    }

    fn bytes(&mut self, bytes: &[u8]) {
        self.code.extend_from_slice(bytes);
    }

    /// An instruction that ends in a 32-bit offset to a label.
    fn jump(&mut self, opcode: &[u8], label: usize) {
        self.bytes(opcode);
        self.fixups.push((self.code.len(), label));
        self.bytes(&[0; 4]);
    }

    fn mov_eax(&mut self, value: u32) {
        self.bytes(&[0xb8]);
        self.bytes(&value.to_le_bytes());
    }

    fn mov_edi(&mut self, value: u32) {
        self.bytes(&[0xbf]);
        self.bytes(&value.to_le_bytes());
    }

    fn syscall(&mut self) {
        self.bytes(&[0x0f, 0x05]);
    }

    /// The code with each piece of data placed after it under its label.
    fn finish(mut self, data: &[(usize, &[u8])]) -> Vec<u8> {
        for (label, bytes) in data {
            self.bind(*label);
            self.code.extend_from_slice(bytes);
        }
        for (at, label) in &self.fixups {
            let target = self.labels[*label].expect("every label is bound") as i64;
            let offset = (target - (*at as i64 + 4)) as i32;
            self.code[*at..*at + 4].copy_from_slice(&offset.to_le_bytes());
        }
        self.code
    }
}
