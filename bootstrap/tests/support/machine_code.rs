//! Reads the instructions of an executable the compiler wrote, for the tests
//! that count them. It knows the instructions the targets write and no
//! others, so a target that writes a new one fails the test that reads it,
//! naming the byte, and the reader is taught it then.
//!
//! Only an ELF file is read, which the machine in its header names. The code
//! of a program ends where the first byte comes that no instruction starts
//! with, as the texts the program holds follow its code.

/// What an instruction does, as far as the tests need.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    /// Gives a value that is 1 or 0 by what the last comparison found.
    SetCondition,
    Call,
    Return,
    /// Goes to the place, whatever was compared.
    Jump(usize),
    /// Goes to the place when what was compared holds, and otherwise to the
    /// next instruction.
    Branch(usize),
    Other,
}

#[derive(Clone, Copy, Debug)]
pub struct Instruction {
    pub at: usize,
    pub len: usize,
    pub kind: Kind,
    /// The 32-bit number the instruction holds in itself, if it does.
    pub immediate: Option<i64>,
}

/// The machines a file names that this reads.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Machine {
    X86_64,
    Arm64,
}

/// The instructions of the code of an executable, or none when it is not an
/// ELF file for a machine this reads.
pub fn instructions(file: &[u8]) -> Option<(Machine, Vec<Instruction>)> {
    if file.len() < 0x78 || &file[..4] != b"\x7fELF" {
        return None;
    }
    let machine = match u16_at(file, 0x12) {
        0x3e => Machine::X86_64,
        0xb7 => Machine::Arm64,
        _ => return None,
    };
    // The entry point, in the file, which is where the code starts.
    let entry = u64_at(file, 0x18);
    let headers = u64_at(file, 0x20) as usize;
    let offset = u64_at(file, headers + 8);
    let address = u64_at(file, headers + 16);
    let start = (entry - address + offset) as usize;
    let found = match machine {
        Machine::X86_64 => x86_64(file, start),
        Machine::Arm64 => arm64(file, start),
    };
    Some((machine, found))
}

fn u16_at(bytes: &[u8], at: usize) -> u16 {
    u16::from_le_bytes(bytes[at..at + 2].try_into().unwrap())
}

fn u64_at(bytes: &[u8], at: usize) -> u64 {
    u64::from_le_bytes(bytes[at..at + 8].try_into().unwrap())
}

fn i32_at(bytes: &[u8], at: usize) -> i64 {
    i64::from(i32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()))
}

/// The instructions of a function's code, with the code starting at `start`
/// and ending where an unknown byte begins.
fn x86_64(file: &[u8], start: usize) -> Vec<Instruction> {
    let mut found = Vec::new();
    let mut at = start;
    while at < file.len() {
        let Some(instruction) = x86_64_instruction(file, at) else {
            break;
        };
        at += instruction.len;
        found.push(instruction);
    }
    found
}

/// The length of the ModRM byte at `at`, with what it takes after it.
fn modrm_len(file: &[u8], at: usize) -> usize {
    let (mode, rm) = (file[at] >> 6, file[at] & 7);
    let mut len = 1;
    if mode != 3 && rm == 4 {
        let sib = file[at + 1];
        len += 1;
        if mode == 0 && sib & 7 == 5 {
            len += 4;
        }
    }
    len + match (mode, rm) {
        (0, 5) | (2, _) => 4,
        (1, _) => 1,
        _ => 0,
    }
}

fn x86_64_instruction(file: &[u8], start: usize) -> Option<Instruction> {
    let mut at = start;
    if file[at] == 0x66 {
        at += 1;
    }
    let wide = (0x40..0x50).contains(&file[at]) && file[at] & 8 != 0;
    if (0x40..0x50).contains(&file[at]) {
        at += 1;
    }
    let opcode = file[at];
    at += 1;
    // What follows the opcode: whether a ModRM byte, and how many bytes of
    // number after it.
    let (modrm, number, kind) = match opcode {
        0x0f => {
            let second = file[at];
            at += 1;
            match second {
                0x05 => (false, 0, Kind::Other),
                0x80..=0x8f => {
                    let to = (at + 4) as i64 + i32_at(file, at);
                    return Some(Instruction {
                        at: start,
                        len: at + 4 - start,
                        kind: Kind::Branch(to as usize),
                        immediate: None,
                    });
                }
                0x90..=0x9f => (true, 0, Kind::SetCondition),
                0x45 | 0x47 | 0xaf | 0xb6 | 0xb7 | 0xbe | 0xbf => (true, 0, Kind::Other),
                _ => return None,
            }
        }
        0xb8..=0xbf => (false, if wide { 8 } else { 4 }, Kind::Other),
        0xc3 => (false, 0, Kind::Return),
        0xe8 | 0xe9 => {
            let to = ((at + 4) as i64 + i32_at(file, at)) as usize;
            let kind = if opcode == 0xe8 {
                Kind::Call
            } else {
                Kind::Jump(to)
            };
            return Some(Instruction {
                at: start,
                len: at + 4 - start,
                kind,
                immediate: None,
            });
        }
        0x01 | 0x03 | 0x09 | 0x21 | 0x29 | 0x31 | 0x39 | 0x3b | 0x63 | 0x85 | 0x88 | 0x89
        | 0x8b | 0x8d | 0xd3 => (true, 0, Kind::Other),
        0x69 | 0x81 | 0xc7 => (true, 4, Kind::Other),
        0x83 | 0xc1 => (true, 1, Kind::Other),
        0xf7 => (true, if (file[at] >> 3) & 7 < 2 { 4 } else { 0 }, Kind::Other),
        0xff => (true, 0, if file[at] == 0x15 { Kind::Call } else { Kind::Other }),
        _ => return None,
    };
    let after_modrm = if modrm { at + modrm_len(file, at) } else { at };
    let immediate = match number {
        1 => Some(i64::from(file[after_modrm] as i8)),
        4 => Some(i32_at(file, after_modrm)),
        _ => None,
    };
    Some(Instruction {
        at: start,
        len: after_modrm + number - start,
        kind,
        immediate,
    })
}

fn arm64(file: &[u8], start: usize) -> Vec<Instruction> {
    let word = |at: usize| u32::from_le_bytes(file[at..at + 4].try_into().unwrap());
    let signed = |value: u32, bits: u32| -> i64 {
        let shift = 64 - bits;
        (i64::from(value) << shift) >> shift
    };
    (start..file.len() - 3)
        .step_by(4)
        .map(|at| {
            let w = word(at);
            let kind = if w & 0xfc00_0000 == 0x1400_0000 {
                Kind::Jump((at as i64 + 4 * signed(w & 0x3ff_ffff, 26)) as usize)
            } else if w & 0xfc00_0000 == 0x9400_0000 {
                Kind::Call
            } else if w & 0xff00_0010 == 0x5400_0000 || w & 0x7e00_0000 == 0x3400_0000 {
                Kind::Branch((at as i64 + 4 * signed((w >> 5) & 0x7_ffff, 19)) as usize)
            } else if w == 0xd65f_03c0 {
                Kind::Return
            } else if w & 0xffff_0fe0 == 0x9a9f_07e0 {
                Kind::SetCondition
            } else {
                Kind::Other
            };
            Instruction {
                at,
                len: 4,
                kind,
                immediate: None,
            }
        })
        .collect()
}

/// How many instructions the shortest round of the loop takes, for the loop
/// whose instruction holds `number`: the least count of instructions, from
/// that one back to it, along any path that follows each jump and each branch
/// either way and goes through no return.
pub fn shortest_round(found: &[Instruction], number: i64) -> usize {
    let at: std::collections::HashMap<usize, usize> =
        found.iter().enumerate().map(|(i, x)| (x.at, i)).collect();
    let holders: Vec<usize> = (0..found.len())
        .filter(|&i| found[i].immediate == Some(number))
        .collect();
    assert_eq!(holders.len(), 1, "one instruction holds {number}: {holders:?}");
    let head = holders[0];
    let mut distance = vec![usize::MAX; found.len()];
    let mut queue = std::collections::VecDeque::new();
    distance[head] = 1;
    queue.push_back(head);
    let mut best = usize::MAX;
    while let Some(i) = queue.pop_front() {
        let next = match found[i].kind {
            Kind::Return => Vec::new(),
            Kind::Jump(to) => vec![at[&to]],
            Kind::Branch(to) => vec![at[&to], i + 1],
            _ => vec![i + 1],
        };
        for j in next {
            if j == head {
                best = best.min(distance[i]);
            } else if j < found.len() && distance[j] == usize::MAX {
                distance[j] = distance[i] + 1;
                queue.push_back(j);
            }
        }
    }
    assert_ne!(best, usize::MAX, "the instruction that holds {number} is in no loop");
    best
}
