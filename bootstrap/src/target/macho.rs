//! The Mach-O executable of macOS, shared by every macOS target. It
//! reaches the kernel through `libSystem`, which the system's dynamic loader
//! links, and carries an ad hoc code signature, which Apple silicon
//! requires.
//!
//! From `libSystem` the program uses only thin wrappers over the kernel:
//! `write`, `_exit`, `__error`, which gives the thread's error number, and,
//! in a program that asks for memory, `mmap`.
//! Nothing from the system is needed to build: the load commands that ask
//! the loader for the library and its symbols, and the signature, are
//! written here.

/// Where `__TEXT` is loaded, above the 4 GiB `__PAGEZERO`.
const BASE: u64 = 0x1_0000_0000;
/// The page size the code signature hashes in.
const SIGNATURE_PAGE: usize = 0x1000;

/// The symbols a program may import from `libSystem`, in the order of their
/// slots in `__got`. A program that never asks for memory imports only the
/// first three.
const IMPORTS: [&str; 4] = ["_write", "___error", "__exit", "_mmap"];
/// `write`, which writes bytes to a file.
pub const WRITE: usize = 0;
/// `__error`, which gives the address of the thread's error number.
pub const ERROR: usize = 1;
/// `_exit`, which ends the process.
pub const EXIT: usize = 2;
/// `mmap`, which maps memory.
pub const MMAP: usize = 3;

/// The symbols a program imports: `mmap` only if it asks for memory.
pub fn imports(allocates: bool) -> &'static [&'static str] {
    if allocates { &IMPORTS } else { &IMPORTS[..3] }
}

/// What `mmap` is asked for: pages that are read and written, and fresh
/// pages that only this process sees.
pub const MMAP_REQUEST: (u32, u32) = (0x1 | 0x2, 0x0002 | 0x1000);

/// The error number of a call that a signal interrupted.
pub const EINTR: u32 = 4;

/// What differs between the machines macOS runs on.
pub struct Machine {
    /// The CPU type and subtype the header names.
    pub cpu_type: u32,
    pub cpu_subtype: u32,
    /// The page size. Every segment starts on a page.
    pub page: u64,
    /// The first macOS version the executable runs on, as `0xMMMMmmpp`.
    pub min_os: u32,
}

const MACH_HEADER_SIZE: u64 = 32;
const SEGMENT_SIZE: u32 = 72;
const SECTION_SIZE: u32 = 80;
const DYLINKER: &[u8] = b"/usr/lib/dyld";
const LIBSYSTEM: &[u8] = b"/usr/lib/libSystem.B.dylib";
/// The name the code signature gives the executable.
const IDENTIFIER: &[u8] = b"smith";

const LC_SEGMENT_64: u32 = 0x19;
const LC_SYMTAB: u32 = 0x2;
const LC_DYSYMTAB: u32 = 0xb;
const LC_LOAD_DYLINKER: u32 = 0xe;
const LC_LOAD_DYLIB: u32 = 0xc;
const LC_CODE_SIGNATURE: u32 = 0x1d;
const LC_BUILD_VERSION: u32 = 0x32;
const LC_DYLD_INFO_ONLY: u32 = 0x8000_0022;
const LC_MAIN: u32 = 0x8000_0028;

/// The size of the load commands, which `commands` writes.
const COMMANDS_SIZE: u32 = SEGMENT_SIZE // __PAGEZERO
    + SEGMENT_SIZE + SECTION_SIZE // __TEXT
    + SEGMENT_SIZE + SECTION_SIZE // __DATA
    + SEGMENT_SIZE // __LINKEDIT
    + 48 // LC_DYLD_INFO_ONLY
    + 24 // LC_SYMTAB
    + 80 // LC_DYSYMTAB
    + padded(12 + DYLINKER.len() as u32 + 1) // LC_LOAD_DYLINKER
    + 24 // LC_MAIN
    + padded(24 + LIBSYSTEM.len() as u32 + 1) // LC_LOAD_DYLIB
    + 24 // LC_BUILD_VERSION
    + 16; // LC_CODE_SIGNATURE
const COMMANDS: u32 = 12;

const fn padded(size: u32) -> u32 {
    size.next_multiple_of(8)
}

/// Where each part of the file is, in the file and in memory.
struct Layout {
    /// How many symbols are imported.
    imports: u32,
    text_offset: u64,
    text_size: u64,
    text_segment_size: u64,
    data_offset: u64,
    linkedit_offset: u64,
    bind_size: u64,
    symbols_offset: u64,
    indirect_offset: u64,
    strings_offset: u64,
    strings_size: u64,
    signature_offset: u64,
    signature_size: u64,
}

impl Layout {
    fn code_address(&self) -> u64 {
        BASE + self.text_offset
    }

    fn data_address(&self) -> u64 {
        BASE + self.data_offset
    }

    fn linkedit_size(&self) -> u64 {
        self.signature_offset + self.signature_size - self.linkedit_offset
    }
}

/// The executable whose `__text` holds `code_size` bytes, which imports
/// `imports`. `code` gives those bytes, for code placed at its first
/// argument, where the slot `i` of `__got`, which the loader fills with the
/// address of import `i`, is at its second argument plus `8 * i`.
pub fn executable(
    machine: &Machine,
    code_size: u64,
    imports: &[&str],
    code: impl FnOnce(u64, u64) -> Vec<u8>,
) -> Vec<u8> {
    let page = machine.page;
    let bind = bind_info(imports);
    let strings = string_table(imports);
    let text_offset = MACH_HEADER_SIZE + COMMANDS_SIZE as u64;
    let text_size = code_size;
    let text_segment_size = (text_offset + text_size).next_multiple_of(page);
    let data_offset = text_segment_size;
    let linkedit_offset = data_offset + page;
    let symbols_offset = linkedit_offset + bind.len() as u64;
    let indirect_offset = symbols_offset + 16 * imports.len() as u64;
    let strings_offset = indirect_offset + 4 * imports.len() as u64;
    let signature_offset = (strings_offset + strings.len() as u64).next_multiple_of(16);
    let layout = Layout {
        imports: imports.len() as u32,
        text_offset,
        text_size,
        text_segment_size,
        data_offset,
        linkedit_offset,
        bind_size: bind.len() as u64,
        symbols_offset,
        indirect_offset,
        strings_offset,
        strings_size: strings.len() as u64,
        signature_offset,
        signature_size: signature_size(signature_offset as usize) as u64,
    };

    let mut out = Vec::with_capacity((signature_offset + layout.signature_size) as usize);
    header(&mut out, machine);
    commands(&mut out, machine, &layout);
    assert_eq!(out.len() as u64, text_offset);
    let code = code(layout.code_address(), layout.data_address());
    assert_eq!(code.len() as u64, code_size);
    out.extend_from_slice(&code);
    // The rest of `__TEXT`, and `__DATA`, whose `__got` the loader fills.
    out.resize(linkedit_offset as usize, 0);
    out.extend_from_slice(&bind);
    for i in 0..imports.len() {
        symbol(&mut out, string_index(imports, i));
    }
    for i in 0..imports.len() as u32 {
        out.extend_from_slice(&i.to_le_bytes());
    }
    out.extend_from_slice(&strings);
    out.resize(signature_offset as usize, 0);
    let signature = signature(&out, text_segment_size);
    assert_eq!(signature.len() as u64, layout.signature_size);
    out.extend_from_slice(&signature);
    out
}

fn header(out: &mut Vec<u8>, machine: &Machine) {
    put32(out, 0xfeed_facf); // 64-bit Mach-O
    put32(out, machine.cpu_type);
    put32(out, machine.cpu_subtype);
    put32(out, 2); // an executable
    put32(out, COMMANDS);
    put32(out, COMMANDS_SIZE);
    // No undefined symbols left for a static link, linked by the loader,
    // two-level names, and loaded at a random address.
    put32(out, 0x1 | 0x4 | 0x80 | 0x20_0000);
    put32(out, 0); // reserved
}

fn commands(out: &mut Vec<u8>, machine: &Machine, l: &Layout) {
    let page = machine.page;
    let start = out.len();
    segment(out, b"__PAGEZERO", 0, 0, BASE, 0, 0, 0);
    segment(
        out,
        b"__TEXT",
        1,
        BASE,
        l.text_segment_size,
        0,
        l.text_segment_size,
        5,
    );
    section(
        out,
        b"__text",
        b"__TEXT",
        BASE + l.text_offset,
        l.text_size,
        l.text_offset as u32,
        2,
        // Holds only instructions, and some of it is instructions.
        0x8000_0400,
        0,
    );
    segment(
        out,
        b"__DATA",
        1,
        l.data_address(),
        page,
        l.data_offset,
        page,
        3,
    );
    section(
        out,
        b"__got",
        b"__DATA",
        l.data_address(),
        8 * u64::from(l.imports),
        l.data_offset as u32,
        3,
        0x6, // pointers the loader binds before the program starts
        0,   // whose symbols start at the first indirect symbol
    );
    let linkedit_size = l.linkedit_size();
    segment(
        out,
        b"__LINKEDIT",
        0,
        BASE + l.linkedit_offset,
        linkedit_size.next_multiple_of(page),
        l.linkedit_offset,
        linkedit_size,
        1,
    );

    put32(out, LC_DYLD_INFO_ONLY);
    put32(out, 48);
    put32(out, 0); // nothing to rebase
    put32(out, 0);
    put32(out, l.linkedit_offset as u32);
    put32(out, l.bind_size as u32);
    for _ in 0..6 {
        put32(out, 0); // no weak or lazy binds, and no exports
    }

    put32(out, LC_SYMTAB);
    put32(out, 24);
    put32(out, l.symbols_offset as u32);
    put32(out, l.imports);
    put32(out, l.strings_offset as u32);
    put32(out, l.strings_size as u32);

    put32(out, LC_DYSYMTAB);
    put32(out, 80);
    put32(out, 0); // no local symbols
    put32(out, 0);
    put32(out, 0); // no defined external symbols
    put32(out, 0);
    put32(out, 0); // every symbol is undefined
    put32(out, l.imports);
    for _ in 0..6 {
        put32(out, 0); // no table of contents, modules, or references
    }
    put32(out, l.indirect_offset as u32);
    put32(out, l.imports);
    for _ in 0..4 {
        put32(out, 0); // no relocations
    }

    path_command(out, LC_LOAD_DYLINKER, 12, DYLINKER, &[]);

    put32(out, LC_MAIN);
    put32(out, 24);
    out.extend_from_slice(&l.text_offset.to_le_bytes()); // entry
    out.extend_from_slice(&0u64.to_le_bytes()); // the default stack

    let mut versions = Vec::new();
    put32(&mut versions, 2); // a timestamp, as the linker writes it
    put32(&mut versions, 0x0001_0000); // current version 1.0.0
    put32(&mut versions, 0x0001_0000); // compatible from version 1.0.0
    path_command(out, LC_LOAD_DYLIB, 24, LIBSYSTEM, &versions);

    put32(out, LC_BUILD_VERSION);
    put32(out, 24);
    put32(out, 1); // macOS
    put32(out, machine.min_os); // the first version it runs on
    put32(out, machine.min_os); // built against that version
    put32(out, 0); // no tools listed

    put32(out, LC_CODE_SIGNATURE);
    put32(out, 16);
    put32(out, l.signature_offset as u32);
    put32(out, l.signature_size as u32);

    assert_eq!(out.len() - start, COMMANDS_SIZE as usize);
}

#[allow(clippy::too_many_arguments)]
fn segment(
    out: &mut Vec<u8>,
    name: &[u8],
    sections: u32,
    address: u64,
    memory_size: u64,
    offset: u64,
    file_size: u64,
    protection: u32,
) {
    put32(out, LC_SEGMENT_64);
    put32(out, SEGMENT_SIZE + sections * SECTION_SIZE);
    name16(out, name);
    out.extend_from_slice(&address.to_le_bytes());
    out.extend_from_slice(&memory_size.to_le_bytes());
    out.extend_from_slice(&offset.to_le_bytes());
    out.extend_from_slice(&file_size.to_le_bytes());
    put32(out, protection); // the most it may ever have
    put32(out, protection); // what it starts with
    put32(out, sections);
    put32(out, 0); // flags
}

#[allow(clippy::too_many_arguments)]
fn section(
    out: &mut Vec<u8>,
    name: &[u8],
    segment: &[u8],
    address: u64,
    size: u64,
    offset: u32,
    align: u32,
    flags: u32,
    first_indirect: u32,
) {
    name16(out, name);
    name16(out, segment);
    out.extend_from_slice(&address.to_le_bytes());
    out.extend_from_slice(&size.to_le_bytes());
    put32(out, offset);
    put32(out, align); // as a power of two
    put32(out, 0); // no relocations
    put32(out, 0);
    put32(out, flags);
    put32(out, first_indirect);
    put32(out, 0);
    put32(out, 0);
}

/// A load command that ends in a path, after `fields`.
fn path_command(out: &mut Vec<u8>, cmd: u32, path_at: u32, path: &[u8], fields: &[u8]) {
    let size = padded(path_at + path.len() as u32 + 1);
    put32(out, cmd);
    put32(out, size);
    put32(out, path_at);
    out.extend_from_slice(fields);
    out.extend_from_slice(path);
    out.resize(out.len() + (size - path_at) as usize - path.len(), 0);
}

fn name16(out: &mut Vec<u8>, name: &[u8]) {
    let mut field = [0u8; 16];
    field[..name.len()].copy_from_slice(name);
    out.extend_from_slice(&field);
}

fn put32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

/// The loader's instructions for filling `__got`: each slot gets its
/// symbol's address in `libSystem`, the first library loaded.
fn bind_info(imports: &[&str]) -> Vec<u8> {
    const SET_DYLIB_ORDINAL: u8 = 0x10;
    const SET_SYMBOL: u8 = 0x40;
    const SET_TYPE_POINTER: u8 = 0x51;
    const SET_SEGMENT_AND_OFFSET: u8 = 0x70;
    const DO_BIND: u8 = 0x90;
    /// `__DATA`, after `__PAGEZERO` and `__TEXT`.
    const DATA_SEGMENT: u8 = 2;
    let mut out = vec![SET_DYLIB_ORDINAL | 1, SET_TYPE_POINTER];
    for (i, name) in imports.iter().enumerate() {
        out.push(SET_SYMBOL);
        out.extend_from_slice(name.as_bytes());
        out.push(0);
        out.push(SET_SEGMENT_AND_OFFSET | DATA_SEGMENT);
        out.push((8 * i) as u8); // as a ULEB128, which is one byte here
        out.push(DO_BIND);
    }
    out.push(0); // done
    out.resize(out.len().next_multiple_of(8), 0);
    out
}

/// The names of the imports, after the empty name at index 1.
fn string_table(imports: &[&str]) -> Vec<u8> {
    let mut out = b" \0".to_vec();
    for name in imports {
        out.extend_from_slice(name.as_bytes());
        out.push(0);
    }
    out.resize(out.len().next_multiple_of(8), 0);
    out
}

fn string_index(imports: &[&str], import: usize) -> u32 {
    2 + imports[..import]
        .iter()
        .map(|n| n.len() as u32 + 1)
        .sum::<u32>()
}

/// An undefined external symbol, found in the first library.
fn symbol(out: &mut Vec<u8>, name: u32) {
    put32(out, name);
    out.push(0x01); // undefined and external
    out.push(0); // in no section
    out.extend_from_slice(&0x0100u16.to_le_bytes()); // from library 1
    out.extend_from_slice(&0u64.to_le_bytes());
}

/// The size of the signature of the file's first `covered` bytes.
fn signature_size(covered: usize) -> usize {
    let slots = covered.div_ceil(SIGNATURE_PAGE);
    SUPERBLOB_SIZE + CODE_DIRECTORY_SIZE + IDENTIFIER.len() + 1 + 32 * slots
}

const SUPERBLOB_SIZE: usize = 12 + 8;
const CODE_DIRECTORY_SIZE: usize = 88;

/// An ad hoc signature, as the linker writes it: a code directory that
/// holds the SHA-256 of each page of the file before the signature. Its
/// numbers are big-endian.
fn signature(file: &[u8], text_segment_size: u64) -> Vec<u8> {
    let slots = file.len().div_ceil(SIGNATURE_PAGE);
    let hashes_at = CODE_DIRECTORY_SIZE + IDENTIFIER.len() + 1;
    let directory_size = hashes_at + 32 * slots;
    let mut out = Vec::with_capacity(SUPERBLOB_SIZE + directory_size);
    let be32 = |out: &mut Vec<u8>, v: u32| out.extend_from_slice(&v.to_be_bytes());
    let be64 = |out: &mut Vec<u8>, v: u64| out.extend_from_slice(&v.to_be_bytes());

    be32(&mut out, 0xfade_0cc0); // a superblob
    be32(&mut out, (SUPERBLOB_SIZE + directory_size) as u32);
    be32(&mut out, 1); // of one blob
    be32(&mut out, 0); // the code directory
    be32(&mut out, SUPERBLOB_SIZE as u32);

    be32(&mut out, 0xfade_0c02); // a code directory
    be32(&mut out, directory_size as u32);
    be32(&mut out, 0x20400); // the version with the executable segment
    be32(&mut out, 0x2 | 0x2_0000); // ad hoc, and signed as the linker signs
    be32(&mut out, hashes_at as u32);
    be32(&mut out, CODE_DIRECTORY_SIZE as u32); // the identifier
    be32(&mut out, 0); // no special slots
    be32(&mut out, slots as u32);
    be32(&mut out, file.len() as u32); // what the hashes cover
    out.push(32); // the size of a hash
    out.push(2); // SHA-256
    out.push(0); // platform
    out.push(SIGNATURE_PAGE.trailing_zeros() as u8);
    be32(&mut out, 0); // spare
    be32(&mut out, 0); // no scatter
    be32(&mut out, 0); // no team
    be32(&mut out, 0); // spare
    be64(&mut out, 0); // what the hashes cover, when it does not fit 32 bits
    be64(&mut out, 0); // the executable segment's offset
    be64(&mut out, text_segment_size);
    be64(&mut out, 1); // the main executable
    out.extend_from_slice(IDENTIFIER);
    out.push(0);
    for page in file.chunks(SIGNATURE_PAGE) {
        out.extend_from_slice(&sha256(page));
    }
    out
}

/// The SHA-256 of the bytes, as FIPS 180-4 defines it.
fn sha256(bytes: &[u8]) -> [u8; 32] {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19,
    ];
    let mut message = bytes.to_vec();
    message.push(0x80);
    while message.len() % 64 != 56 {
        message.push(0);
    }
    message.extend_from_slice(&((bytes.len() as u64) * 8).to_be_bytes());
    for block in message.chunks(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes(block[4 * i..4 * i + 4].try_into().unwrap());
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }
        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut hh] = h;
        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ (!e & g);
            let t1 = hh
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            hh = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        for (x, y) in h.iter_mut().zip([a, b, c, d, e, f, g, hh]) {
            *x = x.wrapping_add(y);
        }
    }
    let mut out = [0u8; 32];
    for (i, word) in h.iter().enumerate() {
        out[4 * i..4 * i + 4].copy_from_slice(&word.to_be_bytes());
    }
    out
}
