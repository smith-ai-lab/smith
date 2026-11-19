//! An executable for x86-64 started on a chip without the level it was built
//! for stops before `main`. This needs `qemu-x86_64`, which can hide what a
//! chip has, and is not run where it is not installed.

use smith::target::Target;

use crate::Test;
use crate::targets::{build, record};

pub const MACHINE: &[Test] = &[(
    "an_executable_started_on_a_chip_without_the_level_names_what_is_missing_and_runs_no_main",
    an_executable_started_on_a_chip_without_the_level_names_what_is_missing_and_runs_no_main,
)];

const HELLO: &[u8] = b"use std.console (console, print)\n\nfn main() ! console { print(\"hello world\") }\n";

fn an_executable_started_on_a_chip_without_the_level_names_what_is_missing_and_runs_no_main() {
    let target = linux_x86_64();
    let bytes = build(HELLO, target).unwrap_or_else(|e| panic!("{e:?}"));
    record(target, &[&bytes]);
    if crate::targets::fingerprinting() {
        return;
    }
    let qemu = std::process::Command::new("qemu-x86_64")
        .arg("--version")
        .output();
    if qemu.is_err() {
        eprintln!("skipped: qemu-x86_64 is not installed, so no chip is made to lack the level");
        return;
    }
    let dir = crate::scratch::path("level");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("program");
    crate::executable::write(&path, &bytes);
    // Nehalem has SSE4.2 and none of AVX, AVX2, or the fused multiply and add.
    let output = crate::clock::output_within(
        std::process::Command::new("qemu-x86_64")
            .args(["-cpu", "Nehalem"])
            .arg(&path),
        true,
    )
    .unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    assert_ne!(output.status.code(), Some(0));
    assert!(!String::from_utf8_lossy(&output.stdout).contains("hello"));
    assert!(
        String::from_utf8_lossy(&output.stderr).contains("x86-64-v3"),
        "the line on the error output names the level"
    );
}

/// The target whose executable runs under `qemu-x86_64`: Linux on x86-64.
fn linux_x86_64() -> Target {
    (Target::ALL.iter().copied())
        .find(|t| t.name().starts_with("linux") && t.name().ends_with("x64"))
        .expect("a target for Linux on x86-64")
}
