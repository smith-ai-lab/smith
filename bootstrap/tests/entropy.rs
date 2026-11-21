//! A program gets bytes nobody can predict from `entropy`, which the system
//! answers when no handler does: every byte is filled, a call that gives
//! fewer or is interrupted is asked again, and a system that gives none ends
//! the program with a message that says so, with no byte used that was not
//! filled.

use std::process::Command;

use smith::target::Target;
use smith::{compile, lower, scalar};

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
        "a_host_that_gives_no_bytes_ends_the_module_and_says_so_before_anything_is_used",
        a_host_that_gives_no_bytes_ends_the_module_and_says_so_before_anything_is_used,
    ),
    (
        "a_list_of_more_than_one_piece_is_filled_to_its_last_byte",
        a_list_of_more_than_one_piece_is_filled_to_its_last_byte,
    ),
    (
        "a_kernel_that_gives_no_bytes_ends_the_program_and_says_so_before_anything_is_used",
        a_kernel_that_gives_no_bytes_ends_the_program_and_says_so_before_anything_is_used,
    ),
];

/// The tests whose result depends on the kind of machine they run on: an
/// executable that asks the system for bytes is run by the machine it is for,
/// so a change to how a target does it is tested on that machine.
pub const MACHINE: &[Test] = &[(
    "probe_what_each_size_of_fill_does_on_this_machine",
    probe_what_each_size_of_fill_does_on_this_machine,
),
(
    "every_executable_for_this_machine_that_asks_for_entropy_prints_64_hex_digits_and_no_two_runs_print_the_same",
    every_executable_for_this_machine_that_asks_for_entropy_prints_64_hex_digits_and_no_two_runs_print_the_same,
)];

/// For every target this machine runs that asks the system for bytes, 32 bytes
/// are 64 hex digits and a line break, and two runs do not print the same.
fn every_executable_for_this_machine_that_asks_for_entropy_prints_64_hex_digits_and_no_two_runs_print_the_same(
) {
    for target in targets::runnable()
        .into_iter()
        .filter(|t| t.is_machine_code())
    {
        let bytes = targets::build(DRAW.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        let large = targets::build(LARGE.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        targets::record(target, &[&bytes]);
        targets::record(target, &[&large]);
        if targets::fingerprinting() {
            continue;
        }
        let lines: Vec<String> = (0..2)
            .map(|_| {
                let ran = targets::run(&bytes);
                line(&aarch64::Ran {
                    stdout: ran.stdout,
                    stderr: Vec::new(),
                    status: ran.status,
                    steps: 0,
                })
            })
            .collect();
        assert_ne!(lines[0], lines[1], "{}", target.name());
        // A list of more than one piece is filled to its last byte.
        let ran = targets::run(&large);
        large_is_filled(
            aarch64::Ran {
                stdout: ran.stdout,
                stderr: Vec::new(),
                status: ran.status,
                steps: 0,
            },
            target.name(),
        );
    }
}

/// A program that prints 32 bytes from `entropy` as hex, and what it prints
/// after them, which a refusal must not reach.
pub const DRAW: &str = "use std.console (console, print)\nuse std.entropy (entropy, fill)\nuse std.fmt (hex)\n\n\
fn main() ! console, entropy, alloc {\n  \
bytes: List[Byte] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]\n  \
var line = \"\"\n  loop b in fill(bytes) { line = \"{line}{hex(b, 2, false)}\" }\n  print(line)\n}\n";

/// What the executable for `target` prints, and how it ends, when run on a
/// machine that is this one or is an emulator of arm64.
fn ran(target: Target, entropy: Entropy) -> Option<aarch64::Ran> {
    if !target.is_machine_code() {
        return run_module(target, DRAW, None);
    }
    let bytes = compile(DRAW.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
    if aarch64::is_executable(&bytes) {
        return Some(aarch64::run_with(&bytes, entropy));
    }
    // An executable for this machine is run by the test of the machine, which
    // is the one a fingerprint holds.
    None
}

/// Whether `node` is installed. A test that needs it says so and is skipped
/// where it is not.
fn node() -> bool {
    let found = crate::clock::output_within(Command::new("node").arg("--version"), false)
        .is_ok_and(|o| o.status.success());
    if !found {
        eprintln!("skipped: node is not installed");
    }
    found
}

/// What `node` does with the module the program is built to and the file of
/// JavaScript beside it, which asks the host for random bytes, or nothing when
/// `node` is not installed. `before` is a file of JavaScript that `node`
/// runs first, which a test uses to make the host refuse.
fn run_module(target: Target, source: &str, before: Option<&str>) -> Option<aarch64::Ran> {
    if !node() {
        return None;
    }
    let program = scalar::passes(&lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}")));
    let dir = crate::scratch::path("entropy");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join(target.executable_name("program")), target.emit(&program)).unwrap();
    for (extension, bytes) in target.beside(&program) {
        std::fs::write(dir.join(format!("program.{extension}")), bytes).unwrap();
    }
    let mut command = Command::new("node");
    if let Some(script) = before {
        std::fs::write(dir.join("before.js"), script).unwrap();
        command.arg("--require").arg(dir.join("before.js"));
    }
    let output = crate::clock::output_within(
        command.arg(dir.join("program.js")).current_dir(&dir),
        true,
    )
    .unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    Some(aarch64::Ran {
        stdout: output.stdout,
        stderr: output.stderr,
        status: output.status.code().expect("ended by a signal"),
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
    for target in Target::ALL.iter().copied() {
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
    for target in Target::ALL.iter().copied() {
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
    for target in Target::ALL.iter().copied() {
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
    for target in Target::ALL.iter().copied() {
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


/// With the host refusing every call of `getRandomValues`, the module says
/// that the system gave no random bytes, prints nothing, and ends with a
/// failing status.
fn a_host_that_gives_no_bytes_ends_the_module_and_says_so_before_anything_is_used() {
    for target in Target::ALL.iter().copied().filter(|t| !t.is_machine_code()) {
        let refuse = "globalThis.crypto.getRandomValues = () => { throw new Error('no bytes'); };\n";
        let Some(ran) = run_module(target, DRAW, Some(refuse)) else {
            continue;
        };
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

/// A program that fills 70000 bytes, which is more than the 65536 that
/// `getRandomValues` takes at a time, and counts the bytes that are not 0, and
/// those among the last thousand, which only a second piece fills.
const LARGE: &str = "use std.bits (u64)\nuse std.console (console, print)\nuse std.entropy (entropy, fill)\nuse std.int (wrapping_add)\nuse std.list (push)\n\n\
fn main() ! console, entropy, alloc {\n  \
var bytes: List[Byte] = []\n  loop i in 0..70000 { bytes = push(bytes, 0) }\n  \
filled = fill(bytes)\n  var all: u64 = 0\n  var last: u64 = 0\n  \
loop i in 0..filled.size {\n    if filled[i] != 0 {\n      all = wrapping_add(all, 1)\n      if i >= 69000 { last = wrapping_add(last, 1) }\n    }\n  }\n  \
print(\"{all} {last}\")\n}\n";

/// The counts the program prints are those of a list filled whole: about 255
/// in 256 of the bytes are not 0, and so are those past the first piece.
fn large_is_filled(ran: aarch64::Ran, name: &str) {
    assert_eq!(ran.status, 0, "{name}");
    let text = String::from_utf8(ran.stdout).unwrap();
    let counts: Vec<u32> = text.split_whitespace().map(|n| n.parse().unwrap()).collect();
    assert!(counts[0] > 69_000 && counts[0] <= 70_000, "{name}: {text}");
    assert!(counts[1] > 900 && counts[1] <= 1_000, "{name}: {text}");
}

/// Every byte of a list larger than a piece is filled, to the last, in the
/// module a host answers. The executables for a machine are run by the test of
/// the machine.
fn a_list_of_more_than_one_piece_is_filled_to_its_last_byte() {
    for target in Target::ALL
        .iter()
        .copied()
        .filter(|t| !t.is_machine_code())
    {
        if let Some(ran) = run_module(target, LARGE, None) {
            large_is_filled(ran, target.name());
        }
    }
}

/// A probe: the status of a program that fills `n` bytes and prints their count.
fn probe_what_each_size_of_fill_does_on_this_machine() {
    let mut said = Vec::new();
    for n in [0usize, 1, 8, 32] {
        let zeros = vec!["0"; n].join(", ");
        let source = format!(
            "use std.console (console, print)\nuse std.entropy (entropy, fill)\n\nfn main() ! console, entropy, alloc {{\n  bytes: List[Byte] = [{zeros}]\n  filled = fill(bytes)\n  print(\"{{filled.size}}\")\n}}\n"
        );
        for target in targets::runnable().into_iter().filter(|t| t.is_machine_code()) {
            let bytes = targets::build(source.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
            targets::record(target, &[&bytes]);
            if targets::fingerprinting() {
                continue;
            }
            let ran = targets::run(&bytes);
            said.push(format!("{n} bytes: status {} printed {:?}", ran.status, String::from_utf8_lossy(&ran.stdout)));
        }
    }
    if !targets::fingerprinting() {
        assert!(said.iter().all(|s| s.contains("status 0")), "{said:#?}");
    }
}
