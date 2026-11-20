//! Which kinds of machine need their tests. `.github/needs-machines.sh`
//! decides from two fingerprints, which `targets::fingerprint` makes of two
//! compilers, so it is tested here without GitHub.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Mutex, OnceLock};

use smith::compile;
use smith::diag::Error;
use smith::target::Target;

use crate::Test;
use crate::cli;
use crate::sha256;
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_compiler_that_differs_for_one_target_needs_exactly_the_machines_that_run_it",
        a_compiler_that_differs_for_one_target_needs_exactly_the_machines_that_run_it,
    ),
    (
        "the_machines_file_names_every_target_but_one_once",
        the_machines_file_names_every_target_but_one_once,
    ),
    (
        "a_compiler_that_differs_for_nothing_needs_no_machine",
        a_compiler_that_differs_for_nothing_needs_no_machine,
    ),
    (
        "nothing_known_of_a_compiler_needs_every_machine",
        nothing_known_of_a_compiler_needs_every_machine,
    ),
    (
        "the_fingerprint_of_a_test_that_builds_floats_has_a_hash_of_something_only_for_the_targets_that_build_them",
        the_fingerprint_of_a_test_that_builds_floats_has_a_hash_of_something_only_for_the_targets_that_build_them,
    ),
    (
        "the_fingerprint_of_a_compiler_is_not_empty_for_any_target_and_is_the_same_twice",
        the_fingerprint_of_a_compiler_is_not_empty_for_any_target_and_is_the_same_twice,
    ),
    (
        "a_fingerprint_that_cannot_be_made_fails_and_says_why_and_one_that_can_is_printed",
        a_fingerprint_that_cannot_be_made_fails_and_says_why_and_one_that_can_is_printed,
    ),
];

fn github() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../.github")
}

/// The target and the machine of each line of `.github/machines`.
fn machines() -> Vec<(String, String)> {
    std::fs::read_to_string(github().join("machines"))
        .unwrap()
        .lines()
        .filter(|l| !l.starts_with('#') && !l.trim().is_empty())
        .map(|l| {
            let mut fields = l.split_whitespace();
            let pair = (fields.next().unwrap(), fields.next().unwrap());
            assert!(fields.next().is_none(), "{l}");
            (pair.0.to_string(), pair.1.to_string())
        })
        .collect()
}

fn the_machines_file_names_every_target_but_one_once() {
    let lines = machines();
    for (target, _) in &lines {
        assert!(
            Target::ALL.iter().any(|t| t.name() == target),
            "{target} is not a target"
        );
    }
    for (i, (target, machine)) in lines.iter().enumerate() {
        assert!(
            lines[..i].iter().all(|(t, m)| t != target && m != machine),
            "{target} or {machine} is named twice"
        );
    }
    // A module runs under `node`, on the machine of the `linux` job, so it
    // has no machine of its own.
    let chips = Target::ALL.iter().filter(|t| t.is_machine_code()).count();
    assert_eq!(lines.len(), chips - 1);
}

/// What the compiler built, by program and target name.
type Built = Mutex<HashMap<(Vec<u8>, &'static str), Vec<u8>>>;

/// What the compiler builds for a program and a target, built once. The
/// three fingerprints of the first test build the same programs, and the
/// compiler that differs for a target builds what this does and a byte more.
fn built(source: &[u8], target: Target) -> Result<Vec<u8>, Vec<Error>> {
    static BUILT: OnceLock<Built> = OnceLock::new();
    let key = (source.to_vec(), target.name());
    let cache = BUILT.get_or_init(Mutex::default);
    if let Some(bytes) = cache.lock().unwrap().get(&key) {
        return Ok(bytes.clone());
    }
    let bytes = compile(source, target)?;
    cache.lock().unwrap().insert(key, bytes.clone());
    Ok(bytes)
}

/// The fingerprint of the compiler, which the three tests share: it is the
/// same for each, and it is most of what each takes.
fn fingerprint_of_the_compiler() -> &'static [(Target, String)] {
    static FINGERPRINT: OnceLock<Vec<(Target, String)>> = OnceLock::new();
    FINGERPRINT.get_or_init(|| fingerprint(built))
}

/// The fingerprint of a compiler that builds as `build` says. It is made of
/// the tests of `cli`, which run only an executable of a program that
/// prints, as it is the script and not the tests that are under test here,
/// and the work of every test of the machine group is not needed for it.
fn fingerprint(build: targets::Build) -> Vec<(Target, String)> {
    targets::fingerprint(cli::MACHINE, build)
}

/// The fingerprint of a compiler as the lines a file holds.
fn lines(fingerprint: &[(Target, String)]) -> String {
    fingerprint
        .iter()
        .map(|(target, hash)| format!("{} {hash}\n", target.name()))
        .collect()
}

/// The machines the script names for a base and a head, in its order.
fn needs(base: &str, head: &str) -> Vec<String> {
    let dir = crate::scratch::path("machines");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("base"), base).unwrap();
    std::fs::write(dir.join("head"), head).unwrap();
    let output = crate::clock::output_within(
        Command::new("sh")
            .arg(github().join("needs-machines.sh"))
            .arg(dir.join("base"))
            .arg(dir.join("head")),
        true,
    )
    .unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    assert!(output.status.success());
    String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect()
}

/// The place among `Target::ALL` of the target the compiler below changes
/// the code for, or `usize::MAX` for every one.
static CHANGED: AtomicUsize = AtomicUsize::new(usize::MAX);

/// The compiler with a byte more in the executables of `CHANGED`.
fn changed(source: &[u8], target: Target) -> Result<Vec<u8>, Vec<Error>> {
    let mut bytes = built(source, target)?;
    let place = CHANGED.load(Ordering::Relaxed);
    if place == usize::MAX || Target::ALL[place] == target {
        bytes.push(0);
    }
    Ok(bytes)
}

fn a_compiler_that_differs_for_one_target_needs_exactly_the_machines_that_run_it() {
    let same = fingerprint_of_the_compiler().to_vec();
    // The compiler that differs for every target, and the one that differs
    // for the first only.
    CHANGED.store(usize::MAX, Ordering::Relaxed);
    let all = fingerprint(changed);
    CHANGED.store(0, Ordering::Relaxed);
    let first = fingerprint(changed);
    CHANGED.store(usize::MAX, Ordering::Relaxed);
    assert_eq!(all.len(), same.len());
    let machines = machines();
    let run_by = |target: Target| -> Vec<String> {
        machines
            .iter()
            .filter(|(t, _)| t == target.name())
            .map(|(_, m)| m.clone())
            .collect()
    };
    // The one that differs for the first target alone differs from the
    // compiler only there.
    assert_eq!(needs(&lines(&same), &lines(&first)), run_by(Target::ALL[0]));
    // A compiler that differs for one target only is the one that differs
    // for every target, with the others' lines as they were.
    for (place, &target) in Target::ALL.iter().enumerate() {
        let mut one = same.clone();
        one[place] = all[place].clone();
        assert_eq!(one[place].0, target);
        assert_ne!(same[place].1, one[place].1, "{}", target.name());
        assert_eq!(
            needs(&lines(&same), &lines(&one)),
            run_by(target),
            "{}",
            target.name()
        );
    }
}

fn a_compiler_that_differs_for_nothing_needs_no_machine() {
    let same = lines(fingerprint_of_the_compiler());
    assert_eq!(needs(&same, &same), Vec::<String>::new());
}

fn nothing_known_of_a_compiler_needs_every_machine() {
    let known = lines(fingerprint_of_the_compiler());
    let every: Vec<String> = machines().into_iter().map(|(_, m)| m).collect();
    assert_eq!(needs("", &known), every);
    assert_eq!(needs(&known, ""), every);
    assert_eq!(needs("", ""), every);
}

/// The test of floats that was fingerprinted for a target that does not build
/// them. The fingerprint of the whole group takes half a minute, and a test of
/// it would not be one.
fn the_fingerprint_of_a_test_that_builds_floats_has_a_hash_of_something_only_for_the_targets_that_build_them()
{
    let nothing = sha256::hex(b"");
    let fingerprint = targets::fingerprint(crate::float_fused::MACHINE, built);
    assert_eq!(fingerprint.len(), Target::ALL.len());
    for (target, hash) in &fingerprint {
        // A target that builds floats has something fingerprinted. One that
        // does not build them has nothing from this test, and its hash is that
        // of nothing, and the test did not stop for it.
        assert_eq!(
            *hash == nothing,
            !target.builds_floats(),
            "{}: {hash}",
            target.name()
        );
    }
}

/// The fingerprint of the compiler is not that of nothing for any target, and
/// is the same when it is made again.
fn the_fingerprint_of_a_compiler_is_not_empty_for_any_target_and_is_the_same_twice() {
    let nothing = sha256::hex(b"");
    let once = fingerprint_of_the_compiler();
    let twice = fingerprint(built);
    assert_eq!(once, twice.as_slice());
    for (target, hash) in once {
        assert_ne!(*hash, nothing, "{}", target.name());
    }
}

/// What `fingerprint.sh` does with a `cargo` that does as `script` says, in a
/// folder that has a `bootstrap` in it: whether it succeeded, what it printed,
/// and what it said.
fn fingerprint_script(script: &str) -> (bool, String, String) {
    let dir = crate::scratch::path("fingerprint");
    std::fs::create_dir_all(dir.join("bootstrap")).unwrap();
    std::fs::create_dir_all(dir.join("bin")).unwrap();
    std::fs::write(dir.join("bin/cargo"), format!("#!/bin/sh\n{script}\n")).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let cargo = dir.join("bin/cargo");
        std::fs::set_permissions(&cargo, std::fs::Permissions::from_mode(0o755)).unwrap();
    }
    let path = format!(
        "{}:{}",
        dir.join("bin").display(),
        std::env::var("PATH").unwrap_or_default()
    );
    let output = crate::clock::output_within(
        Command::new("sh")
            .arg(github().join("fingerprint.sh"))
            .arg(&dir)
            .env("PATH", path),
        true,
    )
    .unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    (
        output.status.success(),
        String::from_utf8(output.stdout).unwrap(),
        String::from_utf8(output.stderr).unwrap(),
    )
}

fn a_fingerprint_that_cannot_be_made_fails_and_says_why_and_one_that_can_is_printed() {
    // A script that is a file to run is a thing of the machines that have `sh`.
    if !cfg!(unix) {
        return;
    }
    let line = format!("linux_x64 {}", "ab".repeat(32));
    let (ok, out, err) = fingerprint_script(&format!("echo '{line}'"));
    assert!((ok, out.as_str()) == (true, format!("{line}\n").as_str()), "{err}");
    // A run that ends in failure, as one that panics does.
    let (ok, out, err) = fingerprint_script("echo 'the property fails for the program' >&2\nexit 101");
    assert!(!ok && out.is_empty(), "{out}");
    assert!(err.contains("cannot be made"), "{err}");
    assert!(err.contains("the property fails for the program"), "{err}");
    // A run that ends well and gives no line of a fingerprint.
    let (ok, _, err) = fingerprint_script("echo 'nothing of the kind'");
    assert!(!ok);
    assert!(err.contains("gave no line of a fingerprint"), "{err}");
    assert!(err.contains("nothing of the kind"), "{err}");
}
