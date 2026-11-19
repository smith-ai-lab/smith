//! `Int` and `Byte` are in every file with no import, and `std.bits` holds the
//! names of sizes. Each is another name for the type the compiler has under
//! the old name, so a file written with the new names builds to the bytes of
//! the file written with the old.

use smith::compile;
use smith::target::Target;

use crate::Test;
use crate::targets::{any, case_files};

pub const ONCE: &[Test] = &[
    (
        "every_case_written_with_int_byte_and_the_sizes_of_std_bits_builds_to_the_same_bytes_first_part",
        |  | cases_build_to_the_same_bytes(0),
    ),
    (
        "every_case_written_with_int_byte_and_the_sizes_of_std_bits_builds_to_the_same_bytes_second_part",
        | | cases_build_to_the_same_bytes(1),
    ),
    (
        "every_case_written_with_int_byte_and_the_sizes_of_std_bits_builds_to_the_same_bytes_third_part",
        | | cases_build_to_the_same_bytes(2),
    ),
    (
        "every_case_written_with_int_byte_and_the_sizes_of_std_bits_builds_to_the_same_bytes_fourth_part",
        | | cases_build_to_the_same_bytes(3),
    ),
    (
        "a_size_listed_from_std_bits_alone_or_with_the_others_is_accepted",
        a_size_listed_from_std_bits_alone_or_with_the_others_is_accepted,
    ),
    (
        "a_value_of_int_is_a_value_of_i64_and_a_value_of_byte_is_a_value_of_u8",
        a_value_of_int_is_a_value_of_i64_and_a_value_of_byte_is_a_value_of_u8,
    ),
    (
        "an_error_names_a_type_as_the_file_wrote_it",
        an_error_names_a_type_as_the_file_wrote_it,
    ),
    (
        "int_and_byte_are_not_imported_and_a_size_is_not_renamed",
        int_and_byte_are_not_imported_and_a_size_is_not_renamed,
    ),
    (
        "a_file_that_writes_i64_and_u8_with_no_import_still_builds",
        a_file_that_writes_i64_and_u8_with_no_import_still_builds,
    ),
];

/// How many parts the cases are built in, so that each test is short.
const PARTS: usize = 4;

/// The names of sizes other than the two that have a name of their own.
const SIZES: &[&str] = &["u16", "u32", "u64", "i8", "i16", "i32"];

fn is_word(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c == b'_'
}

/// The source with `i64` written `Int` and `u8` written `Byte` outside text
/// and comments, and the other sizes it writes, in the order of `SIZES`.
fn rewritten(source: &str) -> (String, Vec<&'static str>) {
    let bytes = source.as_bytes();
    let (mut out, mut sizes, mut i) = (String::new(), Vec::new(), 0);
    while i < bytes.len() {
        let c = bytes[i];
        if c == b'"' {
            let mut j = i + 1;
            while j < bytes.len() && bytes[j] != b'"' {
                j += if bytes[j] == b'\\' { 2 } else { 1 };
            }
            let end = (j + 1).min(bytes.len());
            out.push_str(&source[i..end]);
            i = end;
        } else if bytes[i..].starts_with(b"//") {
            let end = source[i..].find('\n').map_or(bytes.len(), |n| i + n);
            out.push_str(&source[i..end]);
            i = end;
        } else if is_word(c) {
            let end = (i..bytes.len()).find(|&j| !is_word(bytes[j])).unwrap_or(bytes.len());
            let word = &source[i..end];
            match word {
                "i64" => out.push_str("Int"),
                "u8" => out.push_str("Byte"),
                _ => {
                    if let Some(size) = SIZES.iter().find(|s| **s == word) {
                        sizes.push(*size);
                    }
                    out.push_str(word);
                }
            }
            i = end;
        } else {
            let end = (i + 1..bytes.len()).find(|&j| bytes[j] & 0xc0 != 0x80).unwrap_or(bytes.len());
            out.push_str(&source[i..end]);
            i = end;
        }
    }
    sizes.sort_by_key(|s| SIZES.iter().position(|t| t == s));
    sizes.dedup();
    (out, sizes)
}

/// The source with the sizes it writes imported from `std.bits`, in the place
/// the `use` lines are kept in order.
fn with_import(source: String, sizes: &[&str]) -> String {
    if sizes.is_empty() {
        return source;
    }
    let mut lines: Vec<String> = source.lines().map(str::to_string).collect();
    let list = sizes.join(", ");
    if let Some(line) = lines.iter_mut().find(|l| l.starts_with("use std.bits (")) {
        *line = line.replacen(" (", &format!(" ({list}, "), 1);
    } else {
        let at = lines
            .iter()
            .position(|l| l.starts_with("use ") && l.as_str() > "use std.bits")
            .or_else(|| lines.iter().position(|l| l.starts_with("use ")).map(|_| usize::MAX))
            .unwrap_or_else(|| {
                lines
                    .iter()
                    .position(|l| !l.trim().is_empty() && !l.starts_with("//"))
                    .unwrap_or(0)
            });
        let at = if at == usize::MAX {
            lines.iter().rposition(|l| l.starts_with("use ")).unwrap() + 1
        } else {
            at
        };
        lines.insert(at, format!("use std.bits ({list})"));
        if !lines[at + 1].starts_with("use ") {
            lines.insert(at + 1, String::new());
        }
    }
    lines.join("\n") + "\n"
}

/// Every case that builds builds to the same bytes when `Int`, `Byte`, and
/// the names of `std.bits` are written, for one part of the cases.
fn cases_build_to_the_same_bytes(part: usize) {
    let target = any();
    let (mut built, mut rewrote) = (0, 0);
    for (n, path) in case_files().iter().enumerate() {
        if n % PARTS != part {
            continue;
        }
        let source = std::fs::read_to_string(path).unwrap();
        let Ok(old) = compile(source.as_bytes(), target) else {
            continue;
        };
        let (new, sizes) = rewritten(&source);
        rewrote += usize::from(new != source);
        let new = with_import(new, &sizes);
        let said = compile(new.as_bytes(), target)
            .unwrap_or_else(|e| panic!("{}: {e:?}\n{new}", path.display()));
        assert!(said == old, "{} builds to other bytes\n{new}", path.display());
        built += 1;
    }
    assert!(built > 0 && rewrote > 0, "part {part} built {built} and rewrote {rewrote}");
}

fn builds(source: &str) -> Result<Vec<u8>, String> {
    compile(source.as_bytes(), Target::host().unwrap_or_else(any)).map_err(|e| format!("{e:?}"))
}

fn errors(source: &str) -> String {
    match smith::check(source.as_bytes()) {
        Ok(e) | Err(e) => e.iter().map(|e| format!("{e:?}")).collect::<Vec<_>>().join("\n"),
    }
}

fn a_size_listed_from_std_bits_alone_or_with_the_others_is_accepted() {
    let all = ["u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"];
    let body = |names: &[&str]| {
        let uses: String = names.iter().map(|n| format!("  v_{n}: {n} = 1\n")).collect();
        format!("fn main() {{\n{uses}}}\n")
    };
    for n in all {
        let source = format!("use std.bits ({n})\n\n{}", body(&[n]));
        assert!(builds(&source).is_ok(), "{n}: {}", errors(&source));
    }
    let source = format!("use std.bits ({})\n\n{}", all.join(", "), body(&all));
    assert!(builds(&source).is_ok(), "{}", errors(&source));
    let source = "use std.bits (u32, bit_and)\n\nfn f(x: u32) -> u32 { bit_and(x, 255) }\nfn main() { f(300) }\n";
    assert!(builds(source).is_ok(), "{}", errors(source));
}

fn a_value_of_int_is_a_value_of_i64_and_a_value_of_byte_is_a_value_of_u8() {
    let source = "use std.bits (u8, i64)

fn id(a: i64, b: u8) -> (Int, Byte) { (a, b) }
fn back(a: Int, b: Byte) -> (i64, u8) { id(a, b) }
fn main() {
  x: Int = 5
  y: i64 = x
  z: Byte = 7
  w: u8 = z
  r: (i64, u8) = back(y, w)
}
";
    assert!(builds(source).is_ok(), "{}", errors(source));
}

fn an_error_names_a_type_as_the_file_wrote_it() {
    let said = |source: &str| errors(source);
    let int = said("fn f(a: Int) -> Int { a }\nfn main() { f(\"x\") }\n");
    assert!(int.contains("`Int`") && !int.contains("`i64`"), "{int}");
    let old = said("fn f(a: i64) -> i64 { a }\nfn main() { f(\"x\") }\n");
    assert!(old.contains("`i64`") && !old.contains("`Int`"), "{old}");
    let byte = said("fn main() { b: Byte = 300 }\n");
    assert!(byte.contains("`Byte`") && !byte.contains("`u8`"), "{byte}");
    let old = said("fn main() { b: u8 = 300 }\n");
    assert!(old.contains("`u8`") && !old.contains("`Byte`"), "{old}");
    let other = said("fn main() { b: i32 = 3000000000 }\n");
    assert!(other.contains("`i32`"), "{other}");
}

fn int_and_byte_are_not_imported_and_a_size_is_not_renamed() {
    for name in ["Int", "Byte"] {
        let said = errors(&format!("use std.bits ({name})\nfn main() {{ }}\n"));
        assert!(said.contains("is in the prelude"), "{name}: {said}");
    }
    let said = errors("use std.bits (u16 as short)\nfn main() { }\n");
    assert!(said.contains("`as`"), "{said}");
    let said = errors("use std.list (u16)\nfn main() { }\n");
    assert!(said.contains("is in the prelude"), "{said}");
}

fn a_file_that_writes_i64_and_u8_with_no_import_still_builds() {
    let source = "fn f(a: i64, b: u8, c: u32) -> i64 { a }\nfn main() { f(1, 2, 3) }\n";
    assert!(builds(source).is_ok(), "{}", errors(source));
}
