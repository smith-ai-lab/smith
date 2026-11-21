//! A program gets bytes nobody can predict from `entropy`, which the system
//! answers when no handler does: every byte is filled, a call that gives
//! fewer or is interrupted is asked again, and a system that gives none ends
//! the program with a message that says so, with no byte used that was not
//! filled.

use smith::compile;
use smith::target::Target;

use crate::Test;
use crate::aarch64::{self, Entropy};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_program_that_prints_32_bytes_of_entropy_prints_64_hex_digits_and_no_two_runs_print_the_same",
        a_program_that_prints_32_bytes_of_entropy_prints_64_hex_digits_and_no_two_runs_print_the_same,
    ),
    (
        "a_kernel_that_gives_a_few_bytes_a_call_is_asked_again_until_every_byte_is_filled",
        a_kernel_that_gives_a_few_bytes_a_call_is_asked_again_until_every_byte_is_filled,
    ),
    (
        "a_call_the_kernel_interrupts_is_asked_again",
        a_call_the_kernel_interrupts_is_asked_again,
    ),
    (
        "a_kernel_that_gives_no_bytes_ends_the_program_and_says_so_before_anything_is_used",
        a_kernel_that_gives_no_bytes_ends_the_program_and_says_so_before_anything_is_used,
    ),
];

/// A program that prints 32 bytes from `entropy` as hex, and what it prints
/// after them, which a refusal must not reach.
const DRAW: &str = "use std.console (console, print)\nuse std.entropy (entropy, fill)\nuse std.fmt (hex)\n\n\
fn main() ! console, entropy, alloc {\n  \
bytes: List[Byte] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]\n  \
var line = \"\"\n  loop b in fill(bytes) { line = \"{line}{hex(b, 2, false)}\" }\n  print(line)\n}\n";

/// What the executable for `target` prints, and how it ends, when run on a
/// machine that is this one or is an emulator of arm64.
fn ran(target: Target, entropy: Entropy) -> Option<aarch64::Ran> {
    let bytes = compile(DRAW.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
    if aarch64::is_executable(&bytes) {
        return Some(aarch64::run_with(&bytes, entropy));
    }
    if Target::host() != Some(target) {
        return None;
    }
    let ran = targets::run(&bytes);
    Some(aarch64::Ran {
        stdout: ran.stdout,
        stderr: Vec::new(),
        status: ran.status,
        steps: 0,
    })
}

/// The line the program prints, which is 64 hex digits.
fn line(ran: &aarch64::Ran) -> String {
    assert_eq!(ran.status, 0);
    let text = String::from_utf8(ran.stdout.clone()).unwrap();
    let digits = text.strip_suffix('\n').expect("a line");
    assert!(
        digits.len() == 64 && digits.bytes().all(|b| b.is_ascii_hexdigit()),
        "{digits:?}"
    );
    digits.to_string()
}

/// For every target that asks the system for bytes, and that this machine can
/// run or is arm64, 32 bytes are 64 hex digits and a line break, and two runs
/// do not print the same.
fn a_program_that_prints_32_bytes_of_entropy_prints_64_hex_digits_and_no_two_runs_print_the_same() {
    let mut run_any = false;
    for target in Target::ALL.iter().copied().filter(|t| t.answers_entropy()) {
        let (Some(first), Some(second)) = (ran(target, Entropy::Fresh), ran(target, Entropy::Fresh))
        else {
            continue;
        };
        run_any = true;
        assert_ne!(line(&first), line(&second), "{}", target.name());
    }
    assert!(run_any, "no target that answers entropy was run");
}

/// Every byte of the 32 is filled, not those the first call gave alone: a
/// hex digit pair that stayed `00` in each of the last four bytes at once is
/// not one a fill leaves, and the first call here gives 7 bytes.
fn a_kernel_that_gives_a_few_bytes_a_call_is_asked_again_until_every_byte_is_filled() {
    for target in Target::ALL.iter().copied().filter(|t| t.answers_entropy()) {
        let Some(ran) = ran(target, Entropy::ByPieces(7)) else {
            continue;
        };
        if !aarch64::is_executable(&compile(DRAW.as_bytes(), target).unwrap()) {
            continue;
        }
        let digits = line(&ran);
        assert_ne!(&digits[56..], "00000000", "{}: {digits}", target.name());
        assert_ne!(&digits[..8], "00000000", "{}: {digits}", target.name());
    }
}

/// A call that is interrupted is made again, and the program goes on.
fn a_call_the_kernel_interrupts_is_asked_again() {
    for target in Target::ALL.iter().copied().filter(|t| t.answers_entropy()) {
        let bytes = compile(DRAW.as_bytes(), target).unwrap();
        if !aarch64::is_executable(&bytes) {
            continue;
        }
        let ran = aarch64::run_with(&bytes, Entropy::Interrupted);
        assert_ne!(&line(&ran)[..8], "00000000", "{}", target.name());
    }
}

/// With every call refused, the program says that the system gave no random
/// bytes, prints nothing, and ends with a failing status.
fn a_kernel_that_gives_no_bytes_ends_the_program_and_says_so_before_anything_is_used() {
    for target in Target::ALL.iter().copied().filter(|t| t.answers_entropy()) {
        let bytes = compile(DRAW.as_bytes(), target).unwrap();
        if !aarch64::is_executable(&bytes) {
            continue;
        }
        let ran = aarch64::run_with(&bytes, Entropy::Refused);
        assert_eq!(ran.status, 1, "{}", target.name());
        assert!(ran.stdout.is_empty(), "{}", target.name());
        assert_eq!(
            String::from_utf8(ran.stderr).unwrap(),
            "the system gave no random bytes\n",
            "{}",
            target.name()
        );
    }
}
