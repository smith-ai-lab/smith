//! How a pull request tells what it changes in generated code.
//! `.github/hash-cases.sh` hashes the executable of every case for every
//! target, and `.github/check-generated.sh` compares what two compilers give,
//! so both are tested here without GitHub.

use std::path::{Path, PathBuf};
use std::process::Command;

use smith::compile;
use smith::target::Target;

use crate::Test;
use crate::cli;
use crate::sha256;

pub const ONCE: &[Test] = &[
    (
        "the_hashes_of_a_case_are_those_of_its_executable_for_every_target",
        the_hashes_of_a_case_are_those_of_its_executable_for_every_target,
    ),
    (
        "a_case_that_does_not_build_has_no_hash",
        a_case_that_does_not_build_has_no_hash,
    ),
    (
        "cases_that_build_to_the_same_bytes_need_no_word_from_the_description",
        cases_that_build_to_the_same_bytes_need_no_word_from_the_description,
    ),
    (
        "a_case_that_builds_to_other_bytes_fails_a_description_that_does_not_say_so",
        a_case_that_builds_to_other_bytes_fails_a_description_that_does_not_say_so,
    ),
    (
        "a_description_that_says_it_changes_generated_code_passes_and_the_summary_lists_the_cases",
        a_description_that_says_it_changes_generated_code_passes_and_the_summary_lists_the_cases,
    ),
    (
        "a_case_only_one_compiler_builds_is_not_compared",
        a_case_only_one_compiler_builds_is_not_compared,
    ),
];

fn github() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../.github")
}

fn folder() -> PathBuf {
    let dir = crate::scratch::path("generated");
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

const HELLO: &str = "use std.console (console, print)\n\nfn main() ! console { print(\"hello\") }\n";

/// What `hash-cases.sh` prints for a folder of cases, as the files in it.
fn hashed(cases: &[(&str, &str)]) -> Vec<String> {
    let dir = folder();
    for (file, source) in cases {
        std::fs::write(dir.join(file), source).unwrap();
    }
    let output = crate::clock::output_within(
        Command::new("sh")
            .arg(github().join("hash-cases.sh"))
            .arg(env!("CARGO_BIN_EXE_smith"))
            .arg(&dir),
        true,
    )
    .unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    assert!(output.status.success(), "{}", cli::stderr(&output));
    String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect()
}

fn the_hashes_of_a_case_are_those_of_its_executable_for_every_target() {
    let lines = hashed(&[("hello.smith", HELLO)]);
    assert_eq!(lines.len(), Target::ALL.len());
    for target in Target::ALL {
        let bytes = compile(HELLO.as_bytes(), *target).unwrap();
        let want = format!("{} hello.smith {}", target.name(), sha256::hex(&bytes));
        assert!(lines.contains(&want), "{want} is not in {lines:?}");
    }
}

fn a_case_that_does_not_build_has_no_hash() {
    let lines = hashed(&[("hello.smith", HELLO), ("broken.smith", "fn main( {\n")]);
    assert_eq!(lines.len(), Target::ALL.len());
    assert!(lines.iter().all(|l| l.contains(" hello.smith ")));
}

/// What `check-generated.sh` says and whether it passes, for what two
/// compilers hash and the description of a change.
fn checked(main: &str, change: &str, description: &str) -> (bool, String) {
    let dir = folder();
    std::fs::write(dir.join("main"), main).unwrap();
    std::fs::write(dir.join("change"), change).unwrap();
    std::fs::write(dir.join("description"), description).unwrap();
    let output = crate::clock::output_within(
        Command::new("sh")
            .arg(github().join("check-generated.sh"))
            .arg(dir.join("main"))
            .arg(dir.join("change"))
            .arg(dir.join("description")),
        false,
    )
    .unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    (
        output.status.success(),
        String::from_utf8(output.stdout).unwrap(),
    )
}

const MAIN: &str = "first a.smith 1\nfirst b.smith 2\nsecond a.smith 3\n";

fn cases_that_build_to_the_same_bytes_need_no_word_from_the_description() {
    let (passed, said) = checked(MAIN, MAIN, "Moves a function.\n");
    assert!(passed, "{said}");
    assert!(said.starts_with("No case builds to other bytes"), "{said}");
}

fn a_case_that_builds_to_other_bytes_fails_a_description_that_does_not_say_so() {
    let change = "first a.smith 1\nfirst b.smith 9\nsecond a.smith 3\n";
    // Saying it in other words, or inside a line, is not saying it.
    for description in [
        "",
        "Moves a function.\n",
        "This change changes generated code.\n",
        "Changes generated code, they say.\n",
    ] {
        let (passed, said) = checked(MAIN, change, description);
        assert!(!passed, "{description:?}: {said}");
        assert!(said.contains("- first (1): b.smith"), "{said}");
        assert!(!said.contains("second"), "{said}");
    }
}

fn a_description_that_says_it_changes_generated_code_passes_and_the_summary_lists_the_cases() {
    let change = "first a.smith 7\nfirst b.smith 9\nsecond a.smith 8\n";
    for description in [
        "Changes generated code.\n",
        "Moves a function.\r\n\r\nChanges generated code.\r\n\r\nCloses #1\n",
    ] {
        let (passed, said) = checked(MAIN, change, description);
        assert!(passed, "{description:?}: {said}");
        assert!(said.contains("- first (2): a.smith, b.smith"), "{said}");
        assert!(said.contains("- second (1): a.smith"), "{said}");
    }
}

fn a_case_only_one_compiler_builds_is_not_compared() {
    // `c.smith` is new, `b.smith` no longer builds, and neither is a change
    // of an executable that was there.
    let change = "first a.smith 1\nfirst c.smith 5\nsecond a.smith 3\n";
    let (passed, said) = checked(MAIN, change, "");
    assert!(passed, "{said}");
}
