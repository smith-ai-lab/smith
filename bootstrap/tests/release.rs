//! What a release is made of: the version worked out from commit messages,
//! the version the compiler reports, and who may write to the repository.

use std::io::Write;
use std::process::{Command, Stdio};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_next_version_follows_the_rules_for_every_list_of_commits",
        the_next_version_follows_the_rules_for_every_list_of_commits,
    ),
    (
        "the_mirror_pushes_every_tag_under_its_own_name",
        the_mirror_pushes_every_tag_under_its_own_name,
    ),
    (
        "a_build_that_is_not_a_release_reports_dev",
        a_build_that_is_not_a_release_reports_dev,
    ),
    (
        "the_release_workflow_runs_after_a_merge_and_only_its_publish_job_writes",
        the_release_workflow_runs_after_a_merge_and_only_its_publish_job_writes,
    ),
];

const TYPES: &[&str] = &[
    "docs", "test", "ci", "refactor", "build", "chore", "revert", "perf", "fix", "feat",
];

/// A list of commit subjects, the version of the last release if there is
/// one, and the version the rules give, which the generator works out as it
/// builds the list.
#[derive(Debug)]
struct Commits {
    last: Option<(u32, u32)>,
    subjects: Vec<String>,
    expected: Option<String>,
}

fn commits(g: &mut Gen) -> Commits {
    let last = if g.one_in(5) {
        None
    } else {
        Some((g.below(12) as u32, g.below(12) as u32))
    };
    let (mut feat, mut fix, mut breaking) = (false, false, false);
    let mut subjects = Vec::new();
    for _ in 0..g.below(8) {
        let kind = *g.pick(TYPES);
        let bang = g.one_in(4);
        let scope = if g.one_in(2) { "(bootstrap)" } else { "" };
        // The words of a summary are no part of the rule: a `docs` commit
        // may say it fixes or adds something.
        let summary = *g.pick(&["fix a typo", "feat: add a thing", "stop a crash", "add x!"]);
        subjects.push(format!(
            "{kind}{scope}{}: {summary}",
            if bang { "!" } else { "" }
        ));
        feat |= kind == "feat";
        fix |= kind == "fix" || kind == "perf";
        breaking |= bang;
    }
    let expected = if !(feat || fix) {
        None
    } else {
        Some(match last {
            None => "0.1.0".to_string(),
            Some((minor, _)) if feat || breaking => format!("0.{}.0", minor + 1),
            Some((minor, patch)) => format!("0.{minor}.{}", patch + 1),
        })
    };
    Commits {
        last,
        subjects,
        expected,
    }
}

fn script() -> String {
    format!("{}/../.github/next-version.sh", env!("CARGO_MANIFEST_DIR"))
}

fn the_next_version_follows_the_rules_for_every_list_of_commits() {
    generate::check(21, 80, commits, |case| {
        let last = case.last.map_or("none".to_string(), |(minor, patch)| {
            format!("0.{minor}.{patch}")
        });
        let mut child = Command::new("sh")
            .arg(script())
            .arg(&last)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .unwrap();
        let mut lines = case.subjects.join("\n");
        lines.push('\n');
        child
            .stdin
            .take()
            .unwrap()
            .write_all(lines.as_bytes())
            .unwrap();
        let output = child.wait_with_output().unwrap();
        assert!(output.status.success());
        let version = String::from_utf8(output.stdout).unwrap();
        assert_eq!(version.trim(), case.expected.as_deref().unwrap_or(""));
    });
}

fn a_build_that_is_not_a_release_reports_dev() {
    let output = Command::new(env!("CARGO_BIN_EXE_smith"))
        .arg("--version")
        .output()
        .unwrap();
    assert!(output.status.success());
    // A release is built with its version set, which this build does not
    // have, so the line is `smith` and a version or `dev`, and no more.
    let line = String::from_utf8(output.stdout).unwrap();
    let words: Vec<&str> = line.split_whitespace().collect();
    assert_eq!(words.len(), 2, "{line}");
    assert_eq!(words[0], "smith");
    if option_env!("SMITH_VERSION").is_none() {
        assert_eq!(words[1], "dev");
    }
}

fn the_release_workflow_runs_after_a_merge_and_only_its_publish_job_writes() {
    let path = format!(
        "{}/../.github/workflows/release.yml",
        env!("CARGO_MANIFEST_DIR")
    );
    let text = std::fs::read_to_string(path).unwrap();
    let on = text.split("\njobs:").next().unwrap();
    assert!(on.contains("push:") && on.contains("branches: [main]"));
    for other in ["pull_request", "merge_group"] {
        assert!(!on.contains(other), "the workflow must not run for {other}");
    }
    let writes: Vec<usize> = text
        .match_indices("contents: write")
        .map(|(i, _)| i)
        .collect();
    assert_eq!(writes.len(), 1, "one place may write");
    let publish = text.find("\n  publish:").expect("a job that publishes");
    assert!(
        writes[0] > publish,
        "the job that writes is the one that publishes"
    );
    for forbidden in [
        "--clobber",
        "gh release delete",
        "gh release edit",
        "--force",
    ] {
        assert!(
            !text.contains(forbidden),
            "a release is never changed: {forbidden}"
        );
    }
}

/// A list of tag names, each beginning with `v`.
fn tag_names(g: &mut Gen) -> Vec<String> {
    let mut names = Vec::new();
    for _ in 0..g.below(6) {
        let name = format!("v{}.{}.{}", g.below(12), g.below(12), g.below(12));
        if !names.contains(&name) {
            names.push(name);
        }
    }
    names
}

fn git(dir: &std::path::Path, args: &[&str]) -> String {
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
    String::from_utf8(output.stdout).unwrap()
}

fn the_mirror_pushes_every_tag_under_its_own_name() {
    static NEXT: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
    let script = format!("{}/../.github/mirror-tags.sh", env!("CARGO_MANIFEST_DIR"));
    generate::check(33, 8, tag_names, |names| {
        let id = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let root = std::env::temp_dir().join(format!("smith-mirror-{}-{id}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let origin = root.join("origin");
        let clone = root.join("clone");
        std::fs::create_dir_all(&origin).unwrap();
        git(&origin, &["init", "--quiet"]);
        git(&origin, &["commit", "--quiet", "--allow-empty", "-m", "x"]);
        for name in names {
            git(&origin, &["tag", name]);
        }
        // A tag that does not begin with `v` is not mirrored.
        git(&origin, &["tag", "other"]);
        std::fs::create_dir_all(&clone).unwrap();
        git(&clone, &["init", "--quiet"]);
        git(
            &clone,
            &["remote", "add", "origin", origin.to_str().unwrap()],
        );
        let output = Command::new("sh")
            .arg(&script)
            .current_dir(&clone)
            .output()
            .unwrap();
        assert!(output.status.success());
        let mut pushed: Vec<String> = String::from_utf8(output.stdout)
            .unwrap()
            .lines()
            .map(|line| {
                let (from, to) = line.split_once(':').unwrap();
                let name = to.strip_prefix("refs/tags/").unwrap();
                assert!(from.ends_with(name), "{line}");
                name.to_string()
            })
            .collect();
        pushed.sort();
        let mut expected = names.clone();
        expected.sort();
        assert_eq!(pushed, expected);
        let _ = std::fs::remove_dir_all(&root);
    });
}
