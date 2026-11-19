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

/// Whether `qemu-x86_64`, which can make a chip lack a feature, is installed.
pub fn emulator() -> bool {
    std::process::Command::new("qemu-x86_64")
        .arg("--version")
        .output()
        .is_ok()
}

/// A chip that the emulator makes, and the feature the check names first for
/// it. `Nehalem` and `SandyBridge` have no FMA and no AVX2, as FMA is checked
/// first, and `Haswell` with one feature off lacks only that one.
const LACKING: [(&str, &str); 5] = [
    ("Nehalem", "FMA"),
    ("SandyBridge", "FMA"),
    ("Haswell,-avx2", "AVX2"),
    ("Haswell,-bmi2", "BMI2"),
    ("Haswell,-abm", "LZCNT"),
];

fn an_executable_started_on_a_chip_without_the_level_names_what_is_missing_and_runs_no_main() {
    let target = linux_x86_64();
    let bytes = build(HELLO, target).unwrap_or_else(|e| panic!("{e:?}"));
    record(target, &[&bytes]);
    if crate::targets::fingerprinting() {
        return;
    }
    if !emulator() {
        // The `linux` job of `ci` installs the emulator, and a run there that
        // finds it missing has tested nothing of what this is for. Other
        // machines do not install it, and the run says it was not run.
        let installs = cfg!(all(target_os = "linux", target_arch = "x86_64"));
        assert!(
            !(installs && std::env::var("CI").as_deref() == Ok("true")),
            "qemu-x86_64 is not installed, so no chip is made to lack the level"
        );
        return;
    }
    let dir = crate::scratch::path("level");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("program");
    crate::executable::write(&path, &bytes);
    let started = |cpu: &str| {
        crate::clock::output_within(
            std::process::Command::new("qemu-x86_64")
                .args(["-cpu", cpu])
                .arg(&path),
            true,
        )
        .unwrap()
    };
    // On a chip with the level the program runs.
    let output = started("Haswell");
    assert_eq!(output.status.code(), Some(0));
    assert!(String::from_utf8_lossy(&output.stdout).contains("hello"));
    for (cpu, feature) in LACKING {
        let output = started(cpu);
        let said = String::from_utf8_lossy(&output.stderr);
        assert_ne!(output.status.code(), Some(0), "{cpu}");
        assert!(output.stdout.is_empty(), "{cpu} ran main");
        assert!(said.contains("x86-64-v3"), "{cpu}: the level is named: {said}");
        assert!(
            said.contains(&format!("no {feature}")),
            "{cpu}: {feature} is named: {said}"
        );
    }
    std::fs::remove_dir_all(&dir).unwrap();
}

/// The target whose executable runs under `qemu-x86_64`: Linux on x86-64.
fn linux_x86_64() -> Target {
    (Target::ALL.iter().copied())
        .find(|t| t.name().starts_with("linux") && t.name().ends_with("x64"))
        .expect("a target for Linux on x86-64")
}
