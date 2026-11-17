//! The PE executable that both Windows targets write: an image for the
//! console that reaches the kernel through `KERNEL32.dll`, which the
//! system's loader links.
//!
//! From `KERNEL32.dll` the program uses only thin wrappers over the kernel:
//! `GetStdHandle`, `WriteFile`, `ExitProcess`, and, in a program that asks
//! for memory, `VirtualAlloc`, and, in a program that takes the words it was
//! started with, `GetCommandLineW`. It uses no C runtime.
//! Nothing from the system is needed to build: the import tables that ask
//! the loader for the library and its symbols are written here.

/// Where the image asks to be loaded.
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
/// The symbols a program may import, in the order of their slots in the
/// import address table. A program that never asks for memory imports only
/// the first three, and one that asks for memory and takes no words imports
/// the first four.
const IMPORTS: [&str; 5] = [
    "GetStdHandle",
    "WriteFile",
    "ExitProcess",
    "VirtualAlloc",
    "GetCommandLineW",
];
pub const GET_STD_HANDLE: usize = 0;
pub const WRITE_FILE: usize = 1;
pub const EXIT_PROCESS: usize = 2;
pub const VIRTUAL_ALLOC: usize = 3;
pub const GET_COMMAND_LINE: usize = 4;

/// The symbols a program imports: `VirtualAlloc` only if it asks for memory,
/// and `GetCommandLineW` only if it takes the words it was started with,
/// which asks for memory too.
pub fn imports(allocates: bool, takes_arguments: bool) -> &'static [&'static str] {
    if takes_arguments {
        &IMPORTS
    } else if allocates {
        &IMPORTS[..4]
    } else {
        &IMPORTS[..3]
    }
}

/// `VirtualAlloc`'s request to reserve and commit pages at once, and its
/// protection for pages that are read and written.
pub const MEM_COMMIT_RESERVE: u32 = 0x1000 | 0x2000;
pub const PAGE_READWRITE: u32 = 0x04;

/// `GetStdHandle`'s names for standard output and standard error.
pub const STD_OUTPUT_HANDLE: u32 = -11i32 as u32;
pub const STD_ERROR_HANDLE: u32 = -12i32 as u32;
/// The most one `WriteFile` is asked to write.
pub const MOST_PER_WRITE: u32 = 1 << 30;

/// What differs between the machines a PE executable is for.
pub struct Machine {
    /// The machine field of the COFF header.
    pub machine: u16,
    /// The characteristics of the COFF header.
    pub characteristics: u16,
    /// The DLL characteristics of the optional header.
    pub dll_characteristics: u16,
    /// The oldest Windows it runs on, as a major and a minor version.
    pub version: (u16, u16),
}

/// The import tables, laid out from the start of their section.
struct Imports {
    bytes: Vec<u8>,
    /// Where the import address table is, which the loader fills.
    iat: u32,
    iat_size: u32,
    directory_size: u32,
}

/// The executable, whose code and data take `text_size` bytes, which
/// imports `names`. `text` is given where the code is and where the import
/// address table is, each as an offset from the image's base, and gives the
/// code and its data.
pub fn executable(
    machine: &Machine,
    text_size: usize,
    names: &[&str],
    text: impl FnOnce(u64, u64) -> Vec<u8>,
) -> Vec<u8> {
    let text_rva = SECTION_ALIGNMENT;
    let idata_rva = (text_rva + text_size as u32).next_multiple_of(SECTION_ALIGNMENT);
    let imports = import_tables(idata_rva, names);
    let iat_rva = idata_rva + imports.iat;
    let text = text(u64::from(text_rva), u64::from(iat_rva));
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
    coff_header(&mut out, machine);
    let optional = OptionalHeader {
        code_size: text_raw,
        data_size: idata_raw,
        entry: text_rva,
        image_size,
        imports: (idata_rva, imports.directory_size),
        iat: (iat_rva, imports.iat_size),
    };
    optional_header(&mut out, machine, &optional);
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

fn coff_header(out: &mut Vec<u8>, machine: &Machine) {
    put16(out, machine.machine);
    put16(out, SECTIONS as u16);
    put32(out, 0); // no time stamp, so the bytes are the same every build
    put32(out, 0); // no symbol table
    put32(out, 0); // no symbols
    put16(out, OPTIONAL_HEADER_SIZE as u16);
    put16(out, machine.characteristics);
}

struct OptionalHeader {
    code_size: u32,
    data_size: u32,
    entry: u32,
    image_size: u32,
    imports: (u32, u32),
    iat: (u32, u32),
}

fn optional_header(out: &mut Vec<u8>, machine: &Machine, header: &OptionalHeader) {
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
    let (major, minor) = machine.version;
    put16(out, major); // the operating system's version
    put16(out, minor);
    put16(out, 0); // image version
    put16(out, 0);
    put16(out, major); // subsystem version
    put16(out, minor);
    put32(out, 0); // reserved
    put32(out, header.image_size);
    put32(out, HEADERS_SIZE);
    put32(out, 0); // no checksum
    put16(out, 3); // the console subsystem
    put16(out, machine.dll_characteristics);
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
fn import_tables(rva: u32, symbols: &[&str]) -> Imports {
    let directory_size = 2 * 20;
    let table_size = 8 * (symbols.len() as u32 + 1);
    let lookup = directory_size;
    let iat = lookup + table_size;
    let mut names = Vec::new();
    let mut name_offsets = Vec::new();
    let names_start = iat + table_size;
    for name in symbols {
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
