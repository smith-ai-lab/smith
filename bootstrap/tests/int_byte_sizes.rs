//! `Int` and `Byte` are in every file with no import, and `std.bits` holds the
//! names of sizes. Each is another name for the type the compiler has under
//! the old name, so a value of one is a value of the other.

use smith::compile;
use smith::target::Target;

use crate::Test;
use crate::targets::any;

pub const ONCE: &[Test] = &[
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
];

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
    let old = said("use std.bits (i64)\n\nfn f(a: i64) -> i64 { a }\nfn main() { f(\"x\") }\n");
    assert!(old.contains("`i64`") && !old.contains("`Int`"), "{old}");
    let byte = said("fn main() { b: Byte = 300 }\n");
    assert!(byte.contains("`Byte`") && !byte.contains("`u8`"), "{byte}");
    let old = said("use std.bits (u8)\n\nfn main() { b: u8 = 300 }\n");
    assert!(old.contains("`u8`") && !old.contains("`Byte`"), "{old}");
    let other = said("use std.bits (i32)\n\nfn main() { b: i32 = 3000000000 }\n");
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
    assert!(said.contains("has no name `u16`") && said.contains("use std.bits (u16)"), "{said}");
}
