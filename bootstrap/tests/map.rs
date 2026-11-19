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
    (
        "the_at_lines_of_hello_name_its_file_and_the_line_of_the_print_call",
        the_at_lines_of_hello_name_its_file_and_the_line_of_the_print_call,
    ),
    (
        "the_at_lines_of_a_function_follow_its_line_in_order_and_cover_it_for_every_target",
        the_at_lines_of_a_function_follow_its_line_in_order_and_cover_it_for_every_target,
    ),
    (
        "a_function_built_in_place_keeps_its_own_name_on_the_at_lines_of_its_code",
        a_function_built_in_place_keeps_its_own_name_on_the_at_lines_of_its_code,
    ),
    (
        "no_pass_makes_a_place_the_program_did_not_have",
        no_pass_makes_a_place_the_program_did_not_have,
    ),
    (
        "an_instruction_a_pass_makes_in_place_of_others_holds_the_place_of_the_first",
        an_instruction_a_pass_makes_in_place_of_others_holds_the_place_of_the_first,
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
        let fns = map.lines().filter(|l| l.starts_with("fn "));
        let names: Vec<&str> = fns.map(|l| parse(l).3).collect();
        assert!(names.contains(&"hello.main"), "{}: {names:?}", target.name());
        assert!(map.lines().all(|l| l.starts_with("fn ") || l.starts_with("at ")), "{}", target.name());
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
            let text = smith::target::map(&names, &parts);
            assert_eq!(text.lines().filter(|l| l.starts_with("fn ")).count(), parts.len());
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

/// The lines of an `at` line: the address, the size, the file, the line, the
/// column, and the name.
fn parse_at(line: &str) -> (u64, u64, &str, usize, usize, &str) {
    let words: Vec<&str> = line.split(' ').collect();
    assert_eq!(words.len(), 5, "{line}");
    assert_eq!(words[0], "at", "{line}");
    let address = u64::from_str_radix(words[1].strip_prefix("0x").expect("hex"), 16).unwrap();
    let mut place = words[3].rsplitn(3, ':');
    let col = place.next().unwrap().parse().unwrap();
    let row = place.next().unwrap().parse().unwrap();
    (address, words[2].parse().unwrap(), place.next().unwrap(), row, col, words[4])
}

/// Each function of a map with the `at` lines after it.
fn functions(map: &str) -> Vec<(&str, Vec<&str>)> {
    let mut found: Vec<(&str, Vec<&str>)> = Vec::new();
    for line in map.lines() {
        if line.starts_with("fn ") {
            found.push((line, Vec::new()));
        } else {
            found.last_mut().expect("an at line follows a fn line").1.push(line);
        }
    }
    found
}

const SPREAD: &str = "use std.console (console, print)\n\nfn main() ! console {\n  print(\"one\")\n  print(\"two\")\n}\n";

fn the_at_lines_of_hello_name_its_file_and_the_line_of_the_print_call() {
    let dir = folder("map_at_hello");
    std::fs::write(dir.join("hello.smith"), SPREAD).unwrap();
    for target in buildable() {
        let built = smith(&dir, &["build", "--map", "--target", target.name(), "hello.smith"]);
        assert!(built.status.success(), "{}: {}", target.name(), stderr(&built));
        let map = std::fs::read_to_string(dir.join("hello.map")).unwrap();
        let parts = functions(&map);
        let (_, at) = parts
            .iter()
            .find(|(line, at)| !at.is_empty() && parse(line).3 == "hello.main")
            .unwrap_or_else(|| panic!("{}: main has no at lines\n{map}", target.name()));
        let lines: Vec<usize> = at.iter().map(|l| parse_at(l).3).collect();
        assert!(at.iter().all(|l| parse_at(l).2 == "hello.smith"), "{}", target.name());
        assert!(at.iter().all(|l| parse_at(l).5 == "hello.main"), "{}", target.name());
        // The two calls of `print` are on lines 4 and 5, and `main` on 3 to 6.
        assert!(lines.contains(&4) && lines.contains(&5), "{}: {lines:?}", target.name());
        assert!(lines.iter().all(|l| (3..=6).contains(l)), "{}: {lines:?}", target.name());
    }
}

fn the_at_lines_of_a_function_follow_its_line_in_order_and_cover_it_for_every_target() {
    generate::check(9203, 10, programs::program, |case| {
        if case.mistake.is_some() {
            return;
        }
        let program = passed(&case.source);
        let lines = String::from_utf8_lossy(&case.source).lines().count();
        for target in buildable() {
            let (_, parts) = target.emit_parts(&program);
            let names: Vec<String> = program.functions.iter().map(|f| f.name.clone()).collect();
            let map = smith::target::map(&names, &parts);
            for (line, at) in functions(&map) {
                let (address, size, _, _) = parse(line);
                let mut next = address;
                for run in &at {
                    let (from, length, file, row, col, name) = parse_at(run);
                    assert_eq!(from, next, "a gap or an overlap in {line} for {}", target.name());
                    assert!(length > 0, "{run}");
                    next = from + length;
                    assert!(row >= 1 && col >= 1, "{run}");
                    assert!(file.ends_with(".smith"), "{run}");
                    if file == "main.smith" {
                        assert!(row <= lines, "{run}: the file has {lines} lines");
                    }
                    assert!(!name.is_empty() && !name.contains(' '), "{run}");
                }
                if !at.is_empty() {
                    assert_eq!(next, address + size, "{line} is not covered for {}", target.name());
                }
            }
        }
    });
}

fn a_function_built_in_place_keeps_its_own_name_on_the_at_lines_of_its_code() {
    let source = "use std.console (console, print)\n\nfn greet() ! console {\n  print(\"hi\")\n}\n\nfn main() ! console {\n  greet()\n}\n";
    let program = passed(source.as_bytes());
    let names: Vec<String> = program.functions.iter().map(|f| f.name.clone()).collect();
    for target in buildable() {
        let (_, parts) = target.emit_parts(&program);
        let map = smith::target::map(&names, &parts);
        // `greet` is built in place of its call, so no line is its own, and the
        // lines of the function that holds its code name it.
        let own = functions(&map).into_iter().any(|(line, _)| parse(line).3 == "main.greet");
        let kept = functions(&map).into_iter().any(|(line, at)| {
            let holder = parse(line).3;
            holder != "main.greet" && at.iter().any(|l| parse_at(l).5 == "main.greet")
        });
        assert!(!own && kept, "{}\n{map}", target.name());
    }
}

/// Every place the instructions of a program hold.
fn places_of(program: &Program) -> std::collections::HashSet<(u32, u32, u32, u32)> {
    program
        .functions
        .iter()
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
        .map(|i| (i.at.file, i.at.function, i.at.line, i.at.col))
        .collect()
}

fn no_pass_makes_a_place_the_program_did_not_have() {
    type Pass = fn(&Program) -> Program;
    let passes: [(&str, Pass); 8] = [
        ("inline", scalar::inline),
        ("trim", scalar::trim),
        ("thread", scalar::thread),
        ("fold", scalar::fold),
        ("fuse", scalar::fuse),
        ("common", scalar::common),
        ("remove", scalar::remove),
        ("passes", scalar::passes),
    ];
    generate::check(9204, 10, programs::program, |case| {
        if case.mistake.is_some() {
            return;
        }
        let program = smith::lower(&case.source).expect("a program that builds");
        let before = places_of(&program);
        // A place was given, so the program has some.
        assert!(before.iter().any(|p| p.2 > 0));
        for (name, pass) in passes {
            let after = places_of(&pass(&program));
            // The code a pass adds has none, and the rest keeps one it had.
            assert!(
                after.iter().all(|p| p.2 == 0 || before.contains(p)),
                "{name} made a place"
            );
        }
    });
}

fn an_instruction_a_pass_makes_in_place_of_others_holds_the_place_of_the_first() {
    use smith::scalar::{Binary, Bits, Builder, Fused, Op, Place, Type};
    let at = |line| Place { file: 1, function: 1, line, col: 1 };
    let places = |program: &Program| -> Vec<(String, u32)> {
        program
            .functions
            .iter()
            .flat_map(|f| &f.blocks)
            .flat_map(|b| &b.instructions)
            .map(|i| (format!("{:?}", i.op), i.at.line))
            .collect()
    };
    // A sum of two constants is a constant, and is where the sum was.
    let mut b = Builder::new();
    let one = b.int(Bits::B64, 1);
    let two = b.int(Bits::B64, 2);
    let sum = b.binary(Binary::Add, one, two);
    b.ret(vec![sum]);
    let mut f = b.finish(&[Type::Int(Bits::B64)]);
    f.blocks[0].instructions[2].at = at(7);
    let mut main = Builder::new();
    main.ret(vec![]);
    let program = Program { data: Vec::new(), functions: vec![f, main.finish(&[])], main: 1 };
    let folded = scalar::fold(&program);
    let sum_at = places(&folded).into_iter().rev().find(|(op, _)| op.starts_with("Constant(Int(3"));
    assert_eq!(sum_at.map(|(_, line)| line), Some(7));
    // A product and the sum that uses it are one fused instruction, which is
    // where the product was.
    let ty = Type::Float(smith::scalar::FloatBits::F64);
    let mut b = Builder::new();
    let v = b.params(&[ty, ty, ty]);
    let p = b.float_binary(smith::scalar::FloatBinary::Mul, v[0], v[1]);
    let s = b.float_binary(smith::scalar::FloatBinary::Add, p, v[2]);
    b.ret(vec![s]);
    let mut f = b.finish(&[ty]);
    f.blocks[0].instructions[0].at = at(3);
    f.blocks[0].instructions[1].at = at(4);
    let mut main = Builder::new();
    main.ret(vec![]);
    let program = Program { data: Vec::new(), functions: vec![f, main.finish(&[])], main: 1 };
    let fused = scalar::fuse(&program);
    let found = fused.functions[0].blocks[0]
        .instructions
        .iter()
        .find(|i| matches!(i.op, Op::FloatFused { op: Fused::MulAdd, .. }))
        .expect("a fused instruction");
    assert_eq!(found.at.line, 3);
}
