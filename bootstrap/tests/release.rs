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
        "the_files_uploaded_are_those_github_has_and_gitlab_lacks",
        the_files_uploaded_are_those_github_has_and_gitlab_lacks,
    ),
    (
        "a_file_gitlab_holds_with_another_hash_fails_the_run_and_is_named",
        a_file_gitlab_holds_with_another_hash_fails_the_run_and_is_named,
    ),
    (
        "the_script_uploads_what_gitlab_lacks_for_every_number_of_releases_and_files",
        the_script_uploads_what_gitlab_lacks_for_every_number_of_releases_and_files,
    ),
    (
        "a_list_that_cannot_be_read_whole_fails_the_run_before_any_upload",
        a_list_that_cannot_be_read_whole_fails_the_run_before_any_upload,
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

/// The files GitHub holds, and the part of them GitLab holds already, as
/// lines `<version> <file> <sha256>`.
fn holdings(g: &mut Gen) -> (Vec<String>, Vec<String>) {
    let (mut github, mut gitlab) = (Vec::new(), Vec::new());
    for version in 0..g.below(5) {
        for file in 0..g.below(4) {
            let line = format!("0.{version}.0 smith-{file} {:064x}", g.below(1 << 30));
            if g.one_in(2) {
                gitlab.push(line.clone());
            }
            github.push(line);
        }
    }
    // GitLab holds nothing GitHub lacks that matters to the plan, but a
    // file of a version GitHub no longer lists must not disturb it.
    gitlab.push("9.9.9 smith-x 00".to_string());
    (github, gitlab)
}

fn plan(github: &[String], gitlab: &[String], tag: &str) -> std::process::Output {
    let dir = std::env::temp_dir().join(format!(
        "smith-plan-{}-{tag}-{:?}",
        std::process::id(),
        std::thread::current().id()
    ));
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("github"), github.join("\n") + "\n").unwrap();
    std::fs::write(dir.join("gitlab"), gitlab.join("\n") + "\n").unwrap();
    let script = format!("{}/../.github/plan-uploads.sh", env!("CARGO_MANIFEST_DIR"));
    let output = Command::new("sh")
        .arg(script)
        .arg(dir.join("github"))
        .arg(dir.join("gitlab"))
        .output()
        .unwrap();
    let _ = std::fs::remove_dir_all(&dir);
    output
}

fn the_files_uploaded_are_those_github_has_and_gitlab_lacks() {
    generate::check(41, 60, holdings, |(github, gitlab)| {
        let output = plan(github, gitlab, "lacks");
        assert!(output.status.success());
        let planned: Vec<String> = String::from_utf8(output.stdout)
            .unwrap()
            .lines()
            .map(str::to_string)
            .collect();
        let expected: Vec<String> = github
            .iter()
            .filter(|line| !gitlab.contains(line))
            .cloned()
            .collect();
        assert_eq!(planned, expected);
    });
}

fn a_file_gitlab_holds_with_another_hash_fails_the_run_and_is_named() {
    generate::check(42, 60, holdings, |(github, gitlab)| {
        let Some(line) = gitlab.iter().find(|line| github.contains(line)) else {
            return;
        };
        let mut changed = gitlab.clone();
        let at = changed.iter().position(|l| l == line).unwrap();
        let words: Vec<&str> = line.split(' ').collect();
        changed[at] = format!("{} {} {:064x}", words[0], words[1], 1u64 << 40);
        let output = plan(github, &changed, "differs");
        assert!(!output.status.success());
        assert!(output.stdout.is_empty());
        let message = String::from_utf8(output.stderr).unwrap();
        assert!(
            message.contains(words[1]) && message.contains(words[0]),
            "{message}"
        );
    });
}

/// Releases as `(version, [(file, content)])`, and for each file whether
/// GitLab holds it already.
#[derive(Debug)]
struct World {
    releases: Vec<(String, Vec<(String, String)>)>,
    held: Vec<bool>,
}

/// Many releases with few files, or few releases with many files, so that
/// every list crosses a page of a hundred.
fn world(g: &mut Gen) -> World {
    let (releases, files) = *g.pick(&[(0, 0), (2, 2), (5, 1), (1, 5)]);
    let mut all = Vec::new();
    for r in 0..releases {
        let version = format!("0.{r}.0");
        let list = (0..files)
            .map(|f| {
                (
                    format!("smith-{f}"),
                    format!("{version} smith-{f} {}", g.below(99)),
                )
            })
            .collect();
        all.push((version, list));
    }
    let count = releases * files;
    let held = (0..count).map(|_| g.one_in(2)).collect();
    World {
        releases: all,
        held,
    }
}

const FAKE_GH: &str = r#"#!/bin/sh
set -eu
case "$1" in
  api)
    expr=
    while [ $# -gt 0 ]; do
      [ "$1" = --jq ] && expr=$2
      shift
    done
    jq -r "$expr" "$FAKE/releases.json"
    ;;
  release)
    version=$3
    dir=
    pattern=
    while [ $# -gt 0 ]; do
      case "$1" in --dir) dir=$2 ;; --pattern) pattern=$2 ;; esac
      shift
    done
    cp "$FAKE/github/${version#v}/$pattern" "$dir/$pattern"
    ;;
esac
"#;

const FAKE_CURL: &str = r#"#!/bin/sh
set -eu
url=
upload=
while [ $# -gt 0 ]; do
  case "$1" in
    --upload-file) upload=$2; shift ;;
    --header) shift ;;
    -*) ;;
    *) url=$1 ;;
  esac
  shift
done
slice() {
  page=$(printf '%s' "$url" | sed -E 's/.*&page=([0-9]+).*/\1/')
  size=$(printf '%s' "$url" | sed -E 's/.*per_page=([0-9]+).*/\1/')
  if [ -n "${FAKE_BROKEN_PAGE:-}" ] && [ "$page" = "$FAKE_BROKEN_PAGE" ]; then exit 22; fi
  jq -c --argjson p "$page" --argjson n "$size" '.[($p - 1) * $n:$p * $n]'
}
case "$url" in
  */packages/generic/smith/*)
    rest=${url#*/packages/generic/smith/}
    mkdir -p "$FAKE/gitlab/${rest%/*}"
    cp "$upload" "$FAKE/gitlab/$rest"
    echo "$rest" >> "$FAKE/uploads"
    ;;
  */package_files\?*)
    id=$(printf '%s' "$url" | sed -E 's|.*/packages/([0-9]+)/package_files.*|\1|')
    version=$(ls "$FAKE/gitlab" | sort | sed -n "$((id + 1))p")
    for f in "$FAKE/gitlab/$version"/*; do
      printf '%s %s\n' "$(basename "$f")" "$(sha256sum "$f" | cut -d' ' -f1)"
    done | jq -R -s 'split("\n") | map(select(length > 0) | split(" ") | {file_name: .[0], file_sha256: .[1]})' | slice
    ;;
  */packages\?*)
    ls "$FAKE/gitlab" | sort | jq -R -s 'split("\n") | map(select(length > 0)) | to_entries | map({id: .key, version: .value})' | slice
    ;;
esac
"#;

/// Runs the mirror script against fakes of `gh` and `curl` that serve `world`
/// and keep what is uploaded. Returns the folder the fakes keep their state in
/// and the script's exit status.
fn run_mirror(
    root: &std::path::Path,
    world: &World,
    broken_page: Option<usize>,
) -> std::process::ExitStatus {
    let (bin, fake) = (root.join("bin"), root.join("fake"));
    std::fs::create_dir_all(&bin).unwrap();
    for (name, text) in [("gh", FAKE_GH), ("curl", FAKE_CURL)] {
        let path = bin.join(name);
        std::fs::write(&path, text).unwrap();
        let _ = Command::new("chmod").arg("+x").arg(&path).status();
    }
    let json: Vec<String> = world
        .releases
        .iter()
        .map(|(version, files)| {
            let assets: Vec<String> = files
                .iter()
                .map(|(name, content)| {
                    format!(
                        r#"{{"name":"{name}","digest":"sha256:{}"}}"#,
                        crate::sha256::hex(content.as_bytes())
                    )
                })
                .collect();
            format!(
                r#"{{"tag_name":"v{version}","assets":[{}]}}"#,
                assets.join(",")
            )
        })
        .collect();
    std::fs::create_dir_all(fake.join("gitlab")).unwrap();
    std::fs::write(fake.join("releases.json"), format!("[{}]", json.join(","))).unwrap();
    let mut index = 0;
    for (version, files) in &world.releases {
        std::fs::create_dir_all(fake.join("github").join(version)).unwrap();
        for (name, content) in files {
            std::fs::write(fake.join("github").join(version).join(name), content).unwrap();
            if world.held.get(index).copied().unwrap_or(false) {
                let dir = fake.join("gitlab").join(version);
                std::fs::create_dir_all(&dir).unwrap();
                std::fs::write(dir.join(name), content).unwrap();
            }
            index += 1;
        }
    }
    let script = format!(
        "{}/../.github/mirror-releases.sh",
        env!("CARGO_MANIFEST_DIR")
    );
    let mut command = Command::new("sh");
    command
        .arg(script)
        .env(
            "PATH",
            format!("{}:{}", bin.display(), std::env::var("PATH").unwrap()),
        )
        .env("FAKE", &fake)
        .env("GH_TOKEN", "x")
        .env("REPOSITORY", "o/r")
        .env("GITLAB_PACKAGE_TOKEN", "x")
        .env("GITLAB_API", "https://gitlab.test/api")
        .env("PAGE_SIZE", "2");
    if let Some(page) = broken_page {
        command.env("FAKE_BROKEN_PAGE", page.to_string());
    }
    command.output().unwrap().status
}

fn uploads(root: &std::path::Path) -> Vec<String> {
    std::fs::read_to_string(root.join("fake/uploads"))
        .unwrap_or_default()
        .lines()
        .map(str::to_string)
        .collect()
}

fn the_script_uploads_what_gitlab_lacks_for_every_number_of_releases_and_files() {
    static NEXT: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
    generate::check(51, 4, world, |world| {
        let id = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let root = std::env::temp_dir().join(format!("smith-releases-{}-{id}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        assert!(run_mirror(&root, world, None).success());
        let mut expected = Vec::new();
        let mut index = 0;
        for (version, files) in &world.releases {
            for (name, _) in files {
                if !world.held[index] {
                    expected.push(format!("{version}/{name}"));
                }
                index += 1;
            }
        }
        let mut got = uploads(&root);
        got.sort();
        expected.sort();
        assert_eq!(got, expected);
        // A later run finds everything there and uploads nothing more.
        let before = got.len();
        let held = World {
            releases: world.releases.clone(),
            held: vec![true; world.held.len()],
        };
        let _ = std::fs::remove_file(root.join("fake/uploads"));
        assert!(run_mirror(&root, &held, None).success());
        assert_eq!(uploads(&root).len(), 0, "{before}");
        let _ = std::fs::remove_dir_all(&root);
    });
}

fn a_list_that_cannot_be_read_whole_fails_the_run_before_any_upload() {
    // GitLab holds more versions than a page, so its second page of packages is
    // needed, and GitHub holds one more that is still to be uploaded.
    let releases: Vec<(String, Vec<(String, String)>)> = (0..5)
        .map(|r| {
            let version = format!("0.{r}.0");
            let content = format!("{version} smith-0");
            (version, vec![("smith-0".to_string(), content)])
        })
        .collect();
    let mut held = vec![true; 4];
    held.push(false);
    let world = World { releases, held };
    let root = std::env::temp_dir().join(format!("smith-releases-broken-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    assert!(!run_mirror(&root, &world, Some(2)).success());
    assert!(uploads(&root).is_empty());
    let _ = std::fs::remove_dir_all(&root);
    assert!(run_mirror(&root, &world, None).success());
    assert_eq!(uploads(&root), vec!["0.4.0/smith-0".to_string()]);
    let _ = std::fs::remove_dir_all(&root);
}
