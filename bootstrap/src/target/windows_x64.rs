//! Windows on x86-64: a PE executable for the console that reaches the
//! kernel through `KERNEL32.dll`, which the system's loader links.
//!
//! From `KERNEL32.dll` the program uses only thin wrappers over the kernel:
//! `GetStdHandle`, `WriteFile`, and `ExitProcess`. It uses no C runtime.
//! Nothing from the system is needed to build: the import tables that ask
//! the loader for the library and its symbols are written here.

use super::x86_64::{Asm, R8, R9, RAX, RBX, RCX, RDI, RDX, RSI, Z};
use crate::ir::{Program, Step, Stream};

/// Where the image asks to be loaded. It holds no relocations, so it is
/// loaded here.
const IMAGE_BASE: u64 = 0x1_4000_0000;
/// Sections start on this boundary in memory.
const SECTION_ALIGNMENT: u32 = 0x1000;
/// Sections start on this boundary in the file.
const FILE_ALIGNMENT: u32 = 0x200;

/// Where the PE signature is, just after the DOS header.
const PE_OFFSET: u32 = 0x40;
const COFF_HEADER_SIZE: u32 = 20;
const OPTIONAL_HEADER_SIZE: u32 = 240;
const SECTION_HEADER_SIZE: u32 = 40;
const SECTIONS: u32 = 2;
/// The headers, which fill the start of the file up to the first section.
const HEADERS_SIZE: u32 =
    (PE_OFFSET + 4 + COFF_HEADER_SIZE + OPTIONAL_HEADER_SIZE + SECTIONS * SECTION_HEADER_SIZE)
        .next_multiple_of(FILE_ALIGNMENT);

const DLL: &[u8] = b"KERNEL32.dll";
/// The symbols the program imports, in the order of their slots in the
/// import address table.
const IMPORTS: [&str; 3] = ["GetStdHandle", "WriteFile", "ExitProcess"];
const GET_STD_HANDLE: usize = 0;
const WRITE_FILE: usize = 1;
const EXIT_PROCESS: usize = 2;

/// `GetStdHandle`'s name for standard output.
const STD_OUTPUT_HANDLE: u32 = -11i32 as u32;
/// The most one `WriteFile` is asked to write.
const MOST_PER_WRITE: u32 = 1 << 30;

/// The stack frame: 32 bytes the callee may use, the fifth argument of
/// `WriteFile`, and the count it writes back. With the return address
/// above it, it keeps the stack on 16 bytes at each call.
const FRAME: i8 = 56;
const FIFTH_ARGUMENT: i8 = 32;
const WRITTEN: i8 = 40;

/// The import tables, laid out from the start of their section.
struct Imports {
    bytes: Vec<u8>,
    /// Where the import address table is, which the loader fills.
    iat: u32,
    iat_size: u32,
    directory_size: u32,
}

/// The executable for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    let text_rva = SECTION_ALIGNMENT;
    let (asm, data) = code(program);
    let text_size = asm.len() + data.iter().map(|(_, d)| d.len()).sum::<usize>();
    let idata_rva = (text_rva + text_size as u32).next_multiple_of(SECTION_ALIGNMENT);
    let imports = imports(idata_rva);
    let iat_rva = idata_rva + imports.iat;
    let text = asm.finish(&data, u64::from(text_rva), u64::from(iat_rva));
    assert_eq!(text.len(), text_size);

    let text_raw = (text.len() as u32).next_multiple_of(FILE_ALIGNMENT);
    let idata_raw = (imports.bytes.len() as u32).next_multiple_of(FILE_ALIGNMENT);
    let image_size = idata_rva + (imports.bytes.len() as u32).next_multiple_of(SECTION_ALIGNMENT);
    let sections = [
        Section {
            name: *b".text\0\0\0",
            size: text.len() as u32,
            rva: text_rva,
            raw_size: text_raw,
            raw_offset: HEADERS_SIZE,
            // Code, readable and executable.
            characteristics: 0x6000_0020,
        },
        Section {
            name: *b".idata\0\0",
            size: imports.bytes.len() as u32,
            rva: idata_rva,
            raw_size: idata_raw,
            raw_offset: HEADERS_SIZE + text_raw,
            // Initialized data, readable and writable.
            characteristics: 0xc000_0040,
        },
    ];

    let mut out = Vec::with_capacity((HEADERS_SIZE + text_raw + idata_raw) as usize);
    dos_header(&mut out);
    out.extend_from_slice(b"PE\0\0");
    coff_header(&mut out);
    let optional = OptionalHeader {
        code_size: text_raw,
        data_size: idata_raw,
        entry: text_rva,
        image_size,
        imports: (idata_rva, imports.directory_size),
        iat: (iat_rva, imports.iat_size),
    };
    optional_header(&mut out, &optional);
    for section in &sections {
        section_header(&mut out, section);
    }
    out.resize(HEADERS_SIZE as usize, 0);
    out.extend_from_slice(&text);
    out.resize((HEADERS_SIZE + text_raw) as usize, 0);
    out.extend_from_slice(&imports.bytes);
    out.resize((HEADERS_SIZE + text_raw + idata_raw) as usize, 0);
    out
}

fn put16(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn put32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn put64(out: &mut Vec<u8>, value: u64) {
    out.extend_from_slice(&value.to_le_bytes());
}

/// The DOS header, which names where the PE header is and holds no
/// program of its own.
fn dos_header(out: &mut Vec<u8>) {
    out.extend_from_slice(b"MZ");
    out.resize(0x3c, 0);
    put32(out, PE_OFFSET);
}

fn coff_header(out: &mut Vec<u8>) {
    put16(out, 0x8664); // x86-64
    put16(out, SECTIONS as u16);
    put32(out, 0); // no time stamp, so the bytes are the same every build
    put32(out, 0); // no symbol table
    put32(out, 0); // no symbols
    put16(out, OPTIONAL_HEADER_SIZE as u16);
    // An executable image with no relocations, that can use addresses
    // above 2 GiB.
    put16(out, 0x0001 | 0x0002 | 0x0020);
}

struct OptionalHeader {
    code_size: u32,
    data_size: u32,
    entry: u32,
    image_size: u32,
    imports: (u32, u32),
    iat: (u32, u32),
}

fn optional_header(out: &mut Vec<u8>, header: &OptionalHeader) {
    let start = out.len();
    put16(out, 0x20b); // PE32+
    out.extend_from_slice(&[0, 0]); // linker version
    put32(out, header.code_size);
    put32(out, header.data_size);
    put32(out, 0); // no uninitialized data
    put32(out, header.entry);
    put32(out, header.entry); // where the code starts
    put64(out, IMAGE_BASE);
    put32(out, SECTION_ALIGNMENT);
    put32(out, FILE_ALIGNMENT);
    put16(out, 6); // the oldest Windows it runs on: Vista
    put16(out, 0);
    put16(out, 0); // image version
    put16(out, 0);
    put16(out, 6); // subsystem version
    put16(out, 0);
    put32(out, 0); // reserved
    put32(out, header.image_size);
    put32(out, HEADERS_SIZE);
    put32(out, 0); // no checksum
    put16(out, 3); // the console subsystem
    // Compatible with data execution prevention, and aware of Terminal
    // Server.
    put16(out, 0x0100 | 0x8000);
    put64(out, 0x10_0000); // stack reserved
    put64(out, 0x1000); // stack committed
    put64(out, 0x10_0000); // heap reserved
    put64(out, 0x1000); // heap committed
    put32(out, 0); // loader flags
    put32(out, 16); // data directories
    for i in 0..16 {
        let (rva, size) = match i {
            1 => header.imports,
            12 => header.iat,
            _ => (0, 0),
        };
        put32(out, rva);
        put32(out, size);
    }
    assert_eq!(out.len() - start, OPTIONAL_HEADER_SIZE as usize);
}

struct Section {
    name: [u8; 8],
    size: u32,
    rva: u32,
    raw_size: u32,
    raw_offset: u32,
    characteristics: u32,
}

fn section_header(out: &mut Vec<u8>, section: &Section) {
    out.extend_from_slice(&section.name);
    put32(out, section.size);
    put32(out, section.rva);
    put32(out, section.raw_size);
    put32(out, section.raw_offset);
    put32(out, 0); // no relocations
    put32(out, 0); // no line numbers
    put16(out, 0);
    put16(out, 0);
    put32(out, section.characteristics);
}

/// The import directory, with one entry for `KERNEL32.dll` and one that
/// ends it; the lookup table and the address table, each a slot for every
/// symbol and one that ends it; then each symbol's hint and name, and the
/// library's name. `rva` is where the section is in memory.
fn imports(rva: u32) -> Imports {
    let directory_size = 2 * 20;
    let table_size = 8 * (IMPORTS.len() as u32 + 1);
    let lookup = directory_size;
    let iat = lookup + table_size;
    let mut names = Vec::new();
    let mut name_offsets = Vec::new();
    let names_start = iat + table_size;
    for name in IMPORTS {
        name_offsets.push(names_start + names.len() as u32);
        names.extend_from_slice(&[0, 0]); // no hint
        names.extend_from_slice(name.as_bytes());
        names.push(0);
        if names.len() % 2 == 1 {
            names.push(0);
        }
    }
    let dll = names_start + names.len() as u32;
    names.extend_from_slice(DLL);
    names.push(0);

    let mut bytes = Vec::new();
    put32(&mut bytes, rva + lookup);
    put32(&mut bytes, 0); // no time stamp
    put32(&mut bytes, 0); // no forwarders
    put32(&mut bytes, rva + dll);
    put32(&mut bytes, rva + iat);
    bytes.resize(directory_size as usize, 0);
    for _ in 0..2 {
        for offset in &name_offsets {
            put64(&mut bytes, u64::from(rva + offset));
        }
        put64(&mut bytes, 0);
    }
    bytes.extend_from_slice(&names);
    Imports {
        bytes,
        iat,
        iat_size: table_size,
        directory_size,
    }
}

/// The machine code, and the data it reads, each piece under its label.
/// A call goes through the import address table, slot by slot.
fn code(program: &Program) -> (Asm, Vec<(usize, &[u8])>) {
    let mut asm = Asm::default();
    let exit = asm.label();
    let fail = asm.label();
    let mut data = Vec::new();
    asm.reserve_stack(FRAME);
    for step in &program.steps {
        match step {
            Step::Write { stream, bytes } => {
                let text = asm.label();
                data.push((text, bytes.as_slice()));
                write(&mut asm, *stream, text, bytes.len() as u64, fail);
            }
        }
    }
    asm.zero(RCX); // the status is 0
    asm.bind(exit);
    asm.call_slot(EXIT_PROCESS);
    asm.bind(fail);
    asm.mov32(RCX, 1);
    asm.jmp(exit);
    (asm, data)
}

fn handle(stream: Stream) -> u32 {
    match stream {
        Stream::Stdout => STD_OUTPUT_HANDLE,
    }
}

/// Writes every byte at `text`, retrying a write that is cut short, and
/// jumps to `fail` if the system refuses it or writes nothing. The handle,
/// the place, and the length still to write are kept in rbx, rsi, and rdi,
/// which calls preserve.
fn write(asm: &mut Asm, stream: Stream, text: usize, len: u64, fail: usize) {
    let again = asm.label();
    let done = asm.label();
    asm.mov32(RCX, handle(stream));
    asm.call_slot(GET_STD_HANDLE);
    asm.mov(RBX, RAX);
    asm.address(RSI, text);
    asm.mov64(RDI, len);
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
