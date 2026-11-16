//! The ELF writer the Linux targets share: a static executable of one
//! segment that maps the whole file, with the code right after the headers.

/// Where the executable is loaded.
const BASE: u64 = 0x40_0000;
const ELF_HEADER_SIZE: usize = 64;
const PROGRAM_HEADER_SIZE: usize = 56;
const HEADERS_SIZE: usize = ELF_HEADER_SIZE + PROGRAM_HEADER_SIZE;

/// The machine an executable is for, as the ELF header names it.
#[derive(Clone, Copy)]
pub enum Machine {
    X86_64,
    Aarch64,
}

impl Machine {
    fn number(self) -> u16 {
        match self {
            Machine::X86_64 => 0x3e,
            Machine::Aarch64 => 0xb7,
        }
    }

    /// The alignment of the segment: the largest page the machine's kernel
    /// may use, so that the file maps on each of them.
    fn alignment(self) -> u64 {
        match self {
            Machine::X86_64 => 0x1000,
            Machine::Aarch64 => 0x1_0000,
        }
    }
}

/// The address the code is loaded at, which is also the entry.
pub const CODE_ADDRESS: u64 = BASE + HEADERS_SIZE as u64;

/// The executable that runs `code`, loaded at `CODE_ADDRESS`.
pub fn executable(machine: Machine, code: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(HEADERS_SIZE + code.len());
    elf_header(&mut out, machine);
    program_header(&mut out, machine, (HEADERS_SIZE + code.len()) as u64);
    out.extend_from_slice(code);
    out
}

fn elf_header(out: &mut Vec<u8>, machine: Machine) {
    out.extend_from_slice(&[0x7f, b'E', b'L', b'F']);
    // 64-bit, little-endian, ELF version 1, System V ABI, and padding.
    out.extend_from_slice(&[2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    out.extend_from_slice(&2u16.to_le_bytes()); // an executable
    out.extend_from_slice(&machine.number().to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes()); // version
    out.extend_from_slice(&CODE_ADDRESS.to_le_bytes()); // entry
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
fn program_header(out: &mut Vec<u8>, machine: Machine, size: u64) {
    out.extend_from_slice(&1u32.to_le_bytes()); // a loadable segment
    out.extend_from_slice(&5u32.to_le_bytes()); // read and execute
    out.extend_from_slice(&0u64.to_le_bytes()); // file offset
    out.extend_from_slice(&BASE.to_le_bytes()); // virtual address
    out.extend_from_slice(&BASE.to_le_bytes()); // physical address
    out.extend_from_slice(&size.to_le_bytes()); // size in the file
    out.extend_from_slice(&size.to_le_bytes()); // size in memory
    out.extend_from_slice(&machine.alignment().to_le_bytes());
}
