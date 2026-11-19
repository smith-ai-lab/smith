//! The check an executable for x86-64 makes of its chip before `main`: each
//! of the three x86-64 targets has it, and no other target does.

use crate::Test;
use crate::targets::{build, buildable};

pub const ONCE: &[Test] = &[(
    "an_executable_checks_its_chip_for_the_level_it_is_built_for_on_x86_64_and_nowhere_else",
    an_executable_checks_its_chip_for_the_level_it_is_built_for_on_x86_64_and_nowhere_else,
)];

const HELLO: &[u8] = b"use std.console (console, print)\n\nfn main() ! console { print(\"hello world\") }\n";

/// What the check's message says, which every one of them has.
pub const LEVEL: &[u8] = b"x86-64-v3";

fn contains(bytes: &[u8], what: &[u8]) -> bool {
    bytes.windows(what.len()).any(|w| w == what)
}

fn an_executable_checks_its_chip_for_the_level_it_is_built_for_on_x86_64_and_nowhere_else() {
    for target in buildable() {
        let bytes = build(HELLO, target).unwrap_or_else(|e| panic!("{e:?}"));
        // The name of a target ends with its chip: `x64` is x86-64.
        let x86_64 = target.name().ends_with("x64");
        assert_eq!(
            contains(&bytes, LEVEL),
            x86_64,
            "the check of the chip in {}",
            target.name()
        );
    }
}
