//! Which changes need the jobs that build the compiler. `.github/changed.sh`
//! decides from the files two commits differ in, so it is tested here in a
//! repository made for each case, without GitHub.

use std::path::Path;
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_change_needs_the_compiler_jobs_exactly_when_it_touches_a_file_they_are_built_from",
        a_change_needs_the_compiler_jobs_exactly_when_it_touches_a_file_they_are_built_from,
    ),
    (
        "a_change_to_the_compiler_in_smith_needs_the_jobs_of_a_change_to_bootstrap",
        a_change_to_the_compiler_in_smith_needs_the_jobs_of_a_change_to_bootstrap,
    ),
    (
        "a_change_to_the_harness_in_measure_needs_the_jobs_of_a_change_to_bootstrap",
        a_change_to_the_harness_in_measure_needs_the_jobs_of_a_change_to_bootstrap,
    ),
];

/// A file a change may touch, and whether touching it needs the jobs.
const FILES: &[(&str, bool)] = &[
    ("bootstrap/src/lib.rs", true),
    ("bootstrap/tests/main.rs", true),
    ("compiler/lexer.smith", true),
    ("std/list.smith", true),
    ("measure/src/main.rs", true),
    ("measure/Cargo.toml", true),
    ("compiler/parser/tokens.smith", true),
    (".github/workflows/ci.yml", true),
    (".gitattributes", true),
    ("install.sh", true),
    ("knowledge/compiler.md", false),
    ("knowledge/language/index.md", false),
    (".agents/skills/ship-change/SKILL.md", false),
    ("README.md", false),
];

fn git(dir: &Path, args: &[&str]) {
    let output = Command::new("git")
        .args([
            "-c",
            "user.name=t",
            "-c",
            "user.email=t@t",
            "-c",
            "commit.gpgsign=false",
        ])
        .args(args)
        .current_dir(dir)
        .output()
        .unwrap();
    assert!(output.status.success(), "git {args:?}");
}

/// Whether `changed.sh` says that a commit that adds `files` needs the jobs.
fn says(files: &[&str]) -> bool {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    let id = NEXT.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!("smith-changed-{}-{id}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    std::fs::create_dir_all(&root).unwrap();
    git(&root, &["init", "--quiet"]);
    git(&root, &["commit", "--quiet", "--allow-empty", "-m", "base"]);
    for file in files {
        let path = root.join(file);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, "x\n").unwrap();
    }
    git(&root, &["add", "--all"]);
    git(&root, &["commit", "--quiet", "--allow-empty", "-m", "head"]);
    let script = format!("{}/../.github/changed.sh", env!("CARGO_MANIFEST_DIR"));
    let output = Command::new("sh")
        .args([script.as_str(), "HEAD~1", "HEAD"])
        .current_dir(&root)
        .output()
        .unwrap();
    let _ = std::fs::remove_dir_all(&root);
    assert!(output.status.success());
    match String::from_utf8(output.stdout).unwrap().trim() {
        "true" => true,
        "false" => false,
        other => panic!("changed.sh printed {other:?}"),
    }
}

fn files(g: &mut Gen) -> Vec<usize> {
    let mut chosen = Vec::new();
    for _ in 0..1 + g.below(3) {
        let file = g.below(FILES.len());
        if !chosen.contains(&file) {
            chosen.push(file);
        }
    }
    chosen
}

fn a_change_needs_the_compiler_jobs_exactly_when_it_touches_a_file_they_are_built_from() {
    generate::check(41, 25, files, |chosen| {
        let names: Vec<&str> = chosen.iter().map(|&i| FILES[i].0).collect();
        let wanted = chosen.iter().any(|&i| FILES[i].1);
        assert_eq!(says(&names), wanted, "for a change to {names:?}");
    });
}

fn a_change_to_the_compiler_in_smith_needs_the_jobs_of_a_change_to_bootstrap() {
    assert!(says(&["compiler/lexer.smith"]));
    assert!(says(&["compiler/lexer.smith", "knowledge/compiler.md"]));
    assert!(!says(&["knowledge/compiler.md"]));
}

fn a_change_to_the_harness_in_measure_needs_the_jobs_of_a_change_to_bootstrap() {
    assert!(says(&["measure/src/main.rs"]));
    assert!(says(&["measure/Cargo.lock", "knowledge/compiler.md"]));
}
