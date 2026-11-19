//! The map of an executable, which `smith build --map` writes beside it: a
//! `fn` line for each function of its code, by address, size, frame, and name.

use std::path::Path;

use smith::scalar::{self, Program};
use smith::target::{Named, Part, Target};

use crate::Test;
use crate::cli::{folder, smith, stderr};
use crate::generate;
use crate::machine_code;
use crate::programs;
use crate::targets::buildable;

pub const ONCE: &[Test] = &[
    (
        "build_map_writes_the_map_beside_the_executable_with_a_line_for_main",
        build_map_writes_the_map_beside_the_executable_with_a_line_for_main,
    ),
    (
        "an_executable_built_with_a_map_is_the_same_to_the_byte_for_every_target",
        an_executable_built_with_a_map_is_the_same_to_the_byte_for_every_target,
    ),
    (
        "the_fn_lines_are_in_the_order_of_their_addresses_and_cover_the_code_for_every_target",
        the_fn_lines_are_in_the_order_of_their_addresses_and_cover_the_code_for_every_target,
    ),
    (
        "every_line_of_a_machine_code_executable_begins_and_ends_at_an_instruction",
        every_line_of_a_machine_code_executable_begins_and_ends_at_an_instruction,
    ),
    (
        "a_function_made_for_a_type_has_the_type_in_its_name_and_the_runtime_has_names_in_angle_brackets",
        a_function_made_for_a_type_has_the_type_in_its_name_and_the_runtime_has_names_in_angle_brackets,
    ),
    (
        "run_and_check_take_no_map",
        run_and_check_take_no_map,
    ),
];

const HELLO: &str = "use std.console (console, print)\n\nfn main() ! console { print(\"hello world\") }\n";

/// What a line of a map says: the address, the size, the frame, and the name.
fn parse(line: &str) -> (u64, u64, u32, &str) {
    let words: Vec<&str> = line.split(' ').collect();
    assert_eq!(words.len(), 5, "{line}");
    assert_eq!(words[0], "fn", "{line}");
    let address = u64::from_str_radix(words[1].strip_prefix("0x").expect("hex"), 16).unwrap();
    (address, words[2].parse().unwrap(), words[3].parse().unwrap(), words[4])
}

fn build_map_writes_the_map_beside_the_executable_with_a_line_for_main() {
    let dir = folder("map_hello");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    for target in buildable() {
        let built = smith(&dir, &["build", "--map", "--target", target.name(), "hello.smith"]);
        assert!(built.status.success(), "{}: {}", target.name(), stderr(&built));
        assert!(dir.join(target.executable_name("hello")).is_file());
        let map = std::fs::read_to_string(dir.join("hello.map")).unwrap();
        let names: Vec<&str> = map.lines().map(|l| parse(l).3).collect();
        assert!(names.contains(&"hello.main"), "{}: {names:?}", target.name());
        // Nothing but `fn` lines is there yet.
        assert!(map.lines().all(|l| l.starts_with("fn ")), "{}", target.name());
    }
}

/// The folder holds one file for each target: copies of one source.
fn an_executable_built_with_a_map_is_the_same_to_the_byte_for_every_target() {
    for source in crate::cli::cases("out").into_iter().step_by(9) {
        let name = source.file_stem().unwrap().to_str().unwrap().to_string();
        let file = format!("{name}.smith");
        for target in buildable() {
            let (plain, mapped) = (folder("map_plain"), folder("map_mapped"));
            for dir in [&plain, &mapped] {
                std::fs::copy(&source, dir.join(&file)).unwrap();
            }
            let without = smith(&plain, &["build", "--target", target.name(), &file]);
            let with = smith(&mapped, &["build", "--map", "--target", target.name(), &file]);
            assert!(without.status.success(), "{file}: {}", stderr(&without));
            assert!(with.status.success(), "{file}: {}", stderr(&with));
            let executable = target.executable_name(&name);
            assert_eq!(
                std::fs::read(plain.join(&executable)).unwrap(),
                std::fs::read(mapped.join(&executable)).unwrap(),
                "{file} for {}",
                target.name()
            );
            assert!(!plain.join(format!("{name}.map")).exists(), "{file}");
            assert!(mapped.join(format!("{name}.map")).is_file(), "{file}");
        }
    }
}

/// A program the generator makes, as the scalar form after the passes.
fn passed(source: &[u8]) -> Program {
    scalar::passes(&smith::lower(source).expect("a program that builds"))
}

fn the_fn_lines_are_in_the_order_of_their_addresses_and_cover_the_code_for_every_target() {
    generate::check(9201, 10, programs::program, |case| {
        if case.mistake.is_some() {
            return;
        }
        let program = passed(&case.source);
        for target in buildable() {
            let (executable, parts) = target.emit_parts(&program);
            assert!(!parts.is_empty(), "{}", target.name());
            // The first line is where the executable's own header says it starts.
            if let Some(entry) = entry_of(&executable) {
                assert_eq!(parts[0].address, entry, "{}", target.name());
            }
            for pair in parts.windows(2) {
                assert_eq!(
                    pair[0].address + pair[0].size,
                    pair[1].address,
                    "a gap or an overlap in {}",
                    target.name()
                );
            }
            // The functions come in the order of the program, each once.
            let functions: Vec<usize> = parts
                .iter()
                .filter_map(|p| match p.named {
                    Named::Function(k) => Some(k),
                    Named::Piece(_) => None,
                })
                .collect();
            let wanted = (0..program.functions.len()).collect::<Vec<_>>();
            assert_eq!(functions, wanted[..functions.len()].to_vec(), "{}", target.name());
            let last = parts.last().unwrap();
            assert!(last.address + last.size <= address_of_end(&executable, target, &parts), "{}", target.name());
            // The text of the map is the lines, one for each part.
            let names: Vec<String> = program.functions.iter().map(|f| f.name.clone()).collect();
            assert_eq!(smith::target::map(&names, &parts).lines().count(), parts.len());
        }
    });
}

fn u32_at(bytes: &[u8], at: usize) -> u64 {
    u64::from(u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()))
}

fn u64_at(bytes: &[u8], at: usize) -> u64 {
    u64::from_le_bytes(bytes[at..at + 8].try_into().unwrap())
}

/// The address the file's own header says the program starts at, as the
/// system loads it, read from the bytes of the file, or none for a module,
/// which has no such address.
fn entry_of(file: &[u8]) -> Option<u64> {
    if file.starts_with(b"\x7fELF") {
        return Some(u64_at(file, 0x18));
    }
    if file.starts_with(b"MZ") {
        // The optional header follows the 4 bytes `PE\0\0` and the 20 of the
        // coff header, at the place the DOS header gives.
        let optional = u32_at(file, 0x3c) as usize + 24;
        return Some(u64_at(file, optional + 24) + u32_at(file, optional + 16));
    }
    if file.starts_with(&[0xcf, 0xfa, 0xed, 0xfe]) {
        // The commands follow the 32 bytes of the header: the entry is an offset
        // in the file, which `__TEXT`, loaded from its start, gives an address.
        let (mut at, mut entry, mut text) = (32, None, None);
        for _ in 0..u32_at(file, 16) {
            let (kind, size) = (u32_at(file, at), u32_at(file, at + 4) as usize);
            match kind {
                0x8000_0028 => entry = Some(u64_at(file, at + 8)),
                0x19 if file[at + 8..].starts_with(b"__TEXT\0") => {
                    text = Some(u64_at(file, at + 24));
                }
                _ => {}
            }
            at += size;
        }
        return Some(text? + entry?);
    }
    None
}

/// The address that the bytes of the file end at, which no part passes: the
/// file's length, moved to where the first part is, for the file that holds its
/// code from the first part's place in it.
fn address_of_end(executable: &[u8], target: Target, parts: &[Part]) -> u64 {
    let first = parts[0].address;
    if executable.starts_with(b"\0asm") {
        return executable.len() as u64;
    }
    let _ = target;
    // The code is in the file after the headers, and no part is past the end of
    // the file as it is loaded.
    first + executable.len() as u64
}

fn every_line_of_a_machine_code_executable_begins_and_ends_at_an_instruction() {
    generate::check(9202, 10, programs::program, |case| {
        if case.mistake.is_some() {
            return;
        }
        let program = passed(&case.source);
        for target in buildable() {
            let (executable, parts) = target.emit_parts(&program);
            let Some((_, found)) = machine_code::instructions(&executable) else {
                continue;
            };
            // Where the file is loaded, which its one segment says.
            let headers = u64::from_le_bytes(executable[0x20..0x28].try_into().unwrap()) as usize;
            let loaded =
                u64::from_le_bytes(executable[headers + 16..headers + 24].try_into().unwrap());
            let starts: Vec<usize> = found.iter().map(|i| i.at).collect();
            let ends: Vec<usize> = found.iter().map(|i| i.at + i.len).collect();
            let decoded = ends.last().copied().unwrap_or(0);
            let mut checked = 0;
            for part in &parts {
                let (from, to) = (
                    (part.address - loaded) as usize,
                    (part.address - loaded + part.size) as usize,
                );
                if to > decoded {
                    break;
                }
                assert!(starts.contains(&from), "{} begins inside an instruction", target.name());
                assert!(ends.contains(&to), "{} ends inside an instruction", target.name());
                checked += 1;
            }
            assert!(checked > 0, "{}", target.name());
        }
    });
}

fn a_function_made_for_a_type_has_the_type_in_its_name_and_the_runtime_has_names_in_angle_brackets()
{
    let source = "use std.console (console, print)\n\nfn same[T](x: take T) -> T { x }\n\nfn main() ! console, alloc {\n  print(same(\"a\"))\n  print(\"{same(7)}\")\n}\n";
    let program = smith::lower(source.as_bytes()).expect("builds");
    let names: Vec<&str> = program.functions.iter().map(|f| f.name.as_str()).collect();
    assert!(names.contains(&"main.main"), "{names:?}");
    assert!(names.iter().any(|n| n.starts_with("main.same[") && n.ends_with(']')), "{names:?}");
    for name in &names {
        let written = name.contains('.') && !name.starts_with('<');
        let added = name.starts_with('<') && name.ends_with('>');
        assert!(written || added, "{name} is not a name a map holds");
        assert!(!name.contains(char::is_whitespace), "{name}");
    }
    // A function of the prelude made for a type names the type.
    assert!(names.contains(&"prelude.fmt[i64]"), "{names:?}");
}

fn run_and_check_take_no_map() {
    let dir = folder("map_flag");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    for command in ["run", "check"] {
        let output = smith(&dir, &[command, "--map", "hello.smith"]);
        assert!(!output.status.success(), "{command}");
        let said = stderr(&output);
        assert!(said.contains("unknown flag `--map`"), "{command}: {said}");
        assert!(said.contains("usage: smith build"), "{command}: {said}");
        assert!(!Path::new(&dir.join("hello.map")).exists(), "{command}");
    }
}
