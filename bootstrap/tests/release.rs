//! What a release is made of: the version worked out from commit messages,
//! the version the compiler reports, and who may write to the repository.

use std::process::Command;

use crate::Test;
use crate::clock;
use crate::executable;
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
        "an_empty_gitlab_gets_every_file_github_holds",
        an_empty_gitlab_gets_every_file_github_holds,
    ),
    (
        "the_script_copies_every_file_to_an_empty_gitlab",
        the_script_copies_every_file_to_an_empty_gitlab,
    ),
    (
        "a_run_whose_uploads_are_lost_or_changed_fails_and_names_a_file",
        a_run_whose_uploads_are_lost_or_changed_fails_and_names_a_file,
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
        "a_job_that_never_started_is_started_again_twice_and_then_counts_as_failed",
        a_job_that_never_started_is_started_again_twice_and_then_counts_as_failed,
    ),
    (
        "a_run_passes_only_when_every_machine_it_must_see_has_a_newest_job_that_succeeded",
        a_run_passes_only_when_every_machine_it_must_see_has_a_newest_job_that_succeeded,
    ),
    (
        "a_list_of_jobs_that_cannot_be_read_stops_the_step_that_asked",
        a_list_of_jobs_that_cannot_be_read_stops_the_step_that_asked,
    ),
    (
        "every_job_that_follows_another_says_when_it_runs",
        every_job_that_follows_another_says_when_it_runs,
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
        let mut lines = case.subjects.join("\n");
        lines.push('\n');
        let output = clock::output_within_given(
            Command::new("sh").arg(script()).arg(&last),
            lines.as_bytes(),
            true,
        )
        .unwrap();
        assert!(output.status.success());
        let version = String::from_utf8(output.stdout).unwrap();
        assert_eq!(version.trim(), case.expected.as_deref().unwrap_or(""));
    });
}

fn a_build_that_is_not_a_release_reports_dev() {
    let output =
        clock::output_within(Command::new(env!("CARGO_BIN_EXE_smith")).arg("--version"), true)
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

/// A job without a condition is skipped when any job before it, at any
/// distance, was, so each job that needs another says when it runs.
fn every_job_that_follows_another_says_when_it_runs() {
    for name in ["release.yml", "ci.yml"] {
        let path = format!("{}/../.github/workflows/{name}", env!("CARGO_MANIFEST_DIR"));
        let text = std::fs::read_to_string(path).unwrap();
        let jobs = text.split("\njobs:\n").nth(1).unwrap();
        let mut blocks: Vec<Vec<&str>> = Vec::new();
        for line in jobs.lines() {
            let is_name = line.starts_with("  ") && !line.starts_with("   ") && line.ends_with(':');
            if is_name {
                blocks.push(Vec::new());
            }
            if let Some(block) = blocks.last_mut() {
                block.push(line);
            }
        }
        for block in blocks {
            if block.iter().any(|l| l.starts_with("    needs:")) {
                assert!(
                    block.iter().any(|l| l.starts_with("    if:")),
                    "{name}: a job with no condition: {}",
                    block[0]
                );
            }
        }
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
    let output = clock::output_within(
        Command::new("git")
            .args([
                "-c",
                "user.name=t",
                "-c",
                "user.email=t@t",
                "-c",
                "commit.gpgsign=false",
            ])
            .args(args)
            .current_dir(dir),
        true,
    )
    .unwrap();
    assert!(output.status.success(), "git {args:?}");
    String::from_utf8(output.stdout).unwrap()
}

fn the_mirror_pushes_every_tag_under_its_own_name() {
    let script = format!("{}/../.github/mirror-tags.sh", env!("CARGO_MANIFEST_DIR"));
    generate::check(33, 8, tag_names, |names| {
        let root = crate::scratch::path("mirror");
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
        let output =
            clock::output_within(Command::new("sh").arg(&script).current_dir(&clone), true)
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
    // A file of a version GitHub no longer lists must not disturb the plan,
    // and either list may be empty.
    if g.one_in(2) {
        gitlab.push("9.9.9 smith-x 00".to_string());
    }
    if g.one_in(4) {
        gitlab.clear();
    }
    if g.one_in(8) {
        github.clear();
    }
    (github, gitlab)
}

fn plan(github: &[String], gitlab: &[String], tag: &str) -> std::process::Output {
    let dir = crate::scratch::path(&format!("plan-{tag}"));
    std::fs::create_dir_all(&dir).unwrap();
    for (name, lines) in [("github", github), ("gitlab", gitlab)] {
        let text: String = lines.iter().map(|line| format!("{line}\n")).collect();
        std::fs::write(dir.join(name), text).unwrap();
    }
    let script = format!("{}/../.github/plan-uploads.sh", env!("CARGO_MANIFEST_DIR"));
    let output = clock::output_within(
        Command::new("sh")
            .arg(script)
            .arg(dir.join("github"))
            .arg(dir.join("gitlab")),
        true,
    )
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

fn an_empty_gitlab_gets_every_file_github_holds() {
    let github = vec!["0.1.0 a 11".to_string(), "0.1.0 b 22".to_string()];
    let output = plan(&github, &[], "empty");
    assert!(output.status.success());
    assert_eq!(
        String::from_utf8(output.stdout).unwrap(),
        "0.1.0 a 11\n0.1.0 b 22\n"
    );
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
    case "${FAKE_UPLOAD:-keep}" in
      keep) mkdir -p "$FAKE/gitlab/${rest%/*}"; cp "$upload" "$FAKE/gitlab/$rest" ;;
      change) mkdir -p "$FAKE/gitlab/${rest%/*}"; echo changed > "$FAKE/gitlab/$rest" ;;
      lose) ;;
    esac
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
/// and its output.
fn run_mirror(
    root: &std::path::Path,
    world: &World,
    broken_page: Option<usize>,
    upload: &str,
) -> std::process::Output {
    let (bin, fake) = (root.join("bin"), root.join("fake"));
    std::fs::create_dir_all(&bin).unwrap();
    for (name, text) in [("gh", FAKE_GH), ("curl", FAKE_CURL)] {
        executable::write(&bin.join(name), text.as_bytes());
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
    command.env("FAKE_UPLOAD", upload);
    clock::output_within(&mut command, true).unwrap()
}

fn uploads(root: &std::path::Path) -> Vec<String> {
    std::fs::read_to_string(root.join("fake/uploads"))
        .unwrap_or_default()
        .lines()
        .map(str::to_string)
        .collect()
}

fn the_script_uploads_what_gitlab_lacks_for_every_number_of_releases_and_files() {
    generate::check(51, 4, world, |world| {
        let root = crate::scratch::path("releases");
        assert!(run_mirror(&root, world, None, "keep").status.success());
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
        assert!(run_mirror(&root, &held, None, "keep").status.success());
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
    let root = crate::scratch::path("releases-broken");
    let _ = std::fs::remove_dir_all(&root);
    assert!(!run_mirror(&root, &world, Some(2), "keep").status.success());
    assert!(uploads(&root).is_empty());
    let _ = std::fs::remove_dir_all(&root);
    assert!(run_mirror(&root, &world, None, "keep").status.success());
    assert_eq!(uploads(&root), vec!["0.4.0/smith-0".to_string()]);
    let _ = std::fs::remove_dir_all(&root);
}

/// Two releases with two files each, none of them on GitLab.
fn two_releases(held: bool) -> World {
    let releases = (0..2)
        .map(|r| {
            let version = format!("0.{r}.0");
            let files = (0..2)
                .map(|f| (format!("smith-{f}"), format!("{version} smith-{f}")))
                .collect();
            (version, files)
        })
        .collect();
    World {
        releases,
        held: vec![held; 4],
    }
}

fn the_script_copies_every_file_to_an_empty_gitlab() {
    let root = crate::scratch::path("releases-empty");
    let _ = std::fs::remove_dir_all(&root);
    let output = run_mirror(&root, &two_releases(false), None, "keep");
    assert!(output.status.success());
    let mut got = uploads(&root);
    got.sort();
    assert_eq!(
        got,
        [
            "0.0.0/smith-0",
            "0.0.0/smith-1",
            "0.1.0/smith-0",
            "0.1.0/smith-1"
        ]
    );
    let _ = std::fs::remove_dir_all(&root);
}

fn a_run_whose_uploads_are_lost_or_changed_fails_and_names_a_file() {
    for (mode, held) in [("lose", false), ("change", false), ("lose", true)] {
        let root = crate::scratch::path(&format!("releases-lost-{mode}-{held}"));
        let _ = std::fs::remove_dir_all(&root);
        let world = two_releases(held);
        let output = run_mirror(&root, &world, None, mode);
        // When GitLab holds everything already nothing is uploaded, so the
        // mode changes nothing and the run succeeds.
        if held {
            assert!(output.status.success());
        } else {
            assert!(!output.status.success(), "{mode}");
            let message = String::from_utf8(output.stderr).unwrap();
            assert!(
                world
                    .releases
                    .iter()
                    .flat_map(|(version, files)| files.iter().map(move |(name, _)| (version, name)))
                    .any(|(version, name)| message.contains(name.as_str())
                        && message.contains(version.as_str())),
                "{message}"
            );
            let stdout = String::from_utf8(output.stdout).unwrap();
            assert!(!stdout.contains("holds every file"), "{stdout}");
        }
        let _ = std::fs::remove_dir_all(&root);
    }
}

/// How a job can end: it never started, it ran and succeeded, it ran and
/// failed, it was stopped while running, or it was skipped, as the place of a
/// job that never ran is.
#[derive(Clone, Copy, Debug, PartialEq)]
enum End {
    NeverStarted,
    Success,
    Failure,
    Stopped,
    Skipped,
}

/// The jobs asking for each kind of machine, oldest first, and some that name
/// no machine. A job that never started is followed by another only while
/// fewer than three have not.
#[derive(Debug)]
struct Jobs {
    machines: Vec<(String, Vec<End>)>,
    /// The order the jobs are listed in, as (machine, attempt) pairs.
    order: Vec<(usize, usize)>,
    /// Jobs that name no machine, listed after the others.
    unlabelled: Vec<End>,
    /// A machine the run must show that no job asks for.
    absent: bool,
}

fn jobs(g: &mut Gen) -> Jobs {
    let mut machines = Vec::new();
    for m in 0..g.below(6) {
        let mut ends = Vec::new();
        loop {
            let end = *g.pick(&[
                End::NeverStarted,
                End::NeverStarted,
                End::Success,
                End::Failure,
                End::Stopped,
            ]);
            ends.push(end);
            if end != End::NeverStarted || ends.len() == 3 {
                break;
            }
        }
        // The place of a job that was lost, standing in for it.
        if g.one_in(6) {
            ends.push(End::Skipped);
        }
        machines.push((format!("machine-{m}"), ends));
    }
    // Interleave the kinds, each kind's own jobs staying in order.
    let mut next = vec![0; machines.len()];
    let mut order = Vec::new();
    let total: usize = machines.iter().map(|(_, ends)| ends.len()).sum();
    while order.len() < total {
        let m = g.below(machines.len());
        if next[m] < machines[m].1.len() {
            order.push((m, next[m]));
            next[m] += 1;
        }
    }
    let unlabelled = (0..g.below(3))
        .map(|_| {
            *g.pick(&[
                End::Skipped,
                End::Skipped,
                End::Success,
                End::Failure,
                End::NeverStarted,
            ])
        })
        .collect();
    Jobs {
        machines,
        order,
        unlabelled,
        absent: g.one_in(5),
    }
}

fn line(label: &str, end: End) -> String {
    let (conclusion, steps) = match end {
        End::NeverStarted => ("cancelled", 0),
        End::Success => ("success", 4),
        End::Failure => ("failure", 3),
        End::Stopped => ("cancelled", 2),
        End::Skipped => ("skipped", 0),
    };
    format!("{label}\t{conclusion}\t{steps}\n")
}

fn states(jobs: &Jobs) -> String {
    let mut text = String::new();
    for &(m, a) in &jobs.order {
        text.push_str(&line(&jobs.machines[m].0, jobs.machines[m].1[a]));
    }
    for &end in &jobs.unlabelled {
        text.push_str(&line("", end));
    }
    text
}

fn decide(mode: &str, machines: &[&str], input: &str) -> (bool, String) {
    let output = clock::output_within_given(
        Command::new("sh")
            .arg(format!(
                "{}/../.github/never-started.sh",
                env!("CARGO_MANIFEST_DIR")
            ))
            .arg(mode)
            .args(machines),
        input.as_bytes(),
        false,
    )
    .unwrap();
    (
        output.status.success(),
        String::from_utf8(output.stdout).unwrap(),
    )
}

fn first_seen(jobs: &Jobs) -> Vec<usize> {
    let mut seen: Vec<usize> = Vec::new();
    for &(m, _) in &jobs.order {
        if !seen.contains(&m) {
            seen.push(m);
        }
    }
    seen
}

fn a_job_that_never_started_is_started_again_twice_and_then_counts_as_failed() {
    generate::check(33, 120, jobs, |case| {
        let input = states(case);
        // A kind is started again when its newest job never started, and
        // fewer than three have not.
        let expected: Vec<&str> = first_seen(case)
            .iter()
            .map(|&m| &case.machines[m])
            .filter(|(_, ends)| ends.last() == Some(&End::NeverStarted) && ends.len() < 3)
            .map(|(name, _)| name.as_str())
            .collect();
        let (ok, got) = decide("retry", &[], &input);
        assert!(ok);
        assert_eq!(got.lines().collect::<Vec<_>>(), expected, "{input}");
    });
}

fn a_run_passes_only_when_every_machine_it_must_see_has_a_newest_job_that_succeeded() {
    generate::check(34, 200, jobs, |case| {
        let input = states(case);
        let mut wanted: Vec<&str> = case.machines.iter().map(|(n, _)| n.as_str()).collect();
        if case.absent {
            wanted.push("machine-absent");
        }
        let name = |end: &End| match end {
            End::Success => "success",
            End::Failure => "failure",
            End::Skipped => "skipped",
            End::NeverStarted | End::Stopped => "cancelled",
        };
        let expected = if case.unlabelled.iter().any(|end| *end != End::Skipped) {
            "unlabelled".to_string()
        } else {
            wanted
                .iter()
                .find_map(|want| match case.machines.iter().find(|(n, _)| n == want) {
                    None => Some("missing"),
                    Some((_, ends)) => {
                        let end = ends.last().unwrap();
                        (*end != End::Success).then(|| name(end))
                    }
                })
                .unwrap_or("success")
                .to_string()
        };
        if wanted.is_empty() {
            // No machine to see is a mistake, not a pass.
            let (ok, _) = decide("result", &wanted, &input);
            assert!(!ok);
            return;
        }
        let (ok, got) = decide("result", &wanted, &input);
        assert!(ok);
        assert_eq!(got.trim(), expected, "{input}");
    });
}

fn a_list_of_jobs_that_cannot_be_read_stops_the_step_that_asked() {
    let dir = crate::scratch::path("job-states");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("gh"), "#!/bin/sh\nexit 1\n").unwrap();
    let path = format!("{}:{}", dir.display(), std::env::var("PATH").unwrap());
    let output = clock::output_within(
        Command::new("sh")
            .arg(format!(
                "{}/../.github/job-states.sh",
                env!("CARGO_MANIFEST_DIR")
            ))
            .arg("test on ")
            .env("PATH", path)
            .env("GITHUB_REPOSITORY", "o/r")
            .env("GITHUB_RUN_ID", "1"),
        true,
    )
    .unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    assert!(!output.status.success());
    assert!(output.stdout.is_empty());
}
