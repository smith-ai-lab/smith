//! What `install.sh` does, run against a release made on disk, on a machine
//! that `uname` is made to describe, so no test needs GitHub or the machine it
//! names.

use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::sync::atomic::{AtomicUsize, Ordering};

use crate::Test;
use crate::generate::{self, Gen};
use crate::sha256;
use smith::target::Target;

pub const ONCE: &[Test] = &[
    (
        "the_installer_picks_the_file_for_the_machine_and_refuses_every_other",
        the_installer_picks_the_file_for_the_machine_and_refuses_every_other,
    ),
    (
        "a_file_that_does_not_match_its_hash_installs_nothing",
        a_file_that_does_not_match_its_hash_installs_nothing,
    ),
    (
        "a_download_that_fails_part_way_installs_nothing",
        a_download_that_fails_part_way_installs_nothing,
    ),
    (
        "running_the_installer_again_installs_the_newer_release",
        running_the_installer_again_installs_the_newer_release,
    ),
    (
        "the_installer_says_so_only_when_the_folder_is_not_on_the_path",
        the_installer_says_so_only_when_the_folder_is_not_on_the_path,
    ),
    (
        "a_script_cut_off_part_way_installs_the_whole_compiler_or_changes_nothing",
        a_script_cut_off_part_way_installs_the_whole_compiler_or_changes_nothing,
    ),
    (
        "the_installer_holds_no_version_and_no_hash",
        the_installer_holds_no_version_and_no_hash,
    ),
];

/// The targets a release holds a macOS or Linux file for, and the others,
/// whose files the installer never takes.
fn targets(unix: bool) -> Vec<String> {
    Target::ALL
        .iter()
        .map(|t| t.name().to_string())
        .filter(|name| (name.starts_with("macos_") || name.starts_with("linux_")) == unix)
        .collect()
}

/// What `uname -s` and `uname -m` print for a machine, and the target whose
/// file the installer must take for it, if any.
#[derive(Debug, Clone)]
struct Machine {
    system: &'static str,
    chip: &'static str,
    target: Option<String>,
}

fn machine(g: &mut Gen) -> Machine {
    // What `uname` prints, and the words a target's name is made of.
    type Words = Option<(&'static str, &'static str)>;
    const MACHINES: &[(&str, &str, Words)] = &[
        ("Linux", "x86_64", Some(("linux", "x64"))),
        ("Linux", "aarch64", Some(("linux", "arm64"))),
        ("Linux", "arm64", Some(("linux", "arm64"))),
        ("Linux", "amd64", Some(("linux", "x64"))),
        ("Darwin", "arm64", Some(("macos", "arm64"))),
        ("Darwin", "x86_64", Some(("macos", "x64"))),
        ("Linux", "i686", None),
        ("Linux", "riscv64", None),
        ("Darwin", "i386", None),
        ("FreeBSD", "amd64", None),
        ("MINGW64_NT-10.0", "x86_64", None),
        ("Windows_NT", "arm64", None),
    ];
    let &(system, chip, words) = g.pick(MACHINES);
    Machine {
        system,
        chip,
        target: words.map(|(os, arch)| format!("{os}_{arch}")),
    }
}

/// A newer release has a larger version.
fn version(g: &mut Gen) -> String {
    format!("0.{}.{}", g.below(12), g.below(12))
}

/// The bytes of a stand-in compiler for a target: a script that reports
/// itself the way a release does, naming its target too.
fn compiler(version: &str, target: &str) -> Vec<u8> {
    format!("#!/bin/sh\necho 'smith {version} {target}'\n").into_bytes()
}

fn fresh(name: &str) -> PathBuf {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    let id = NEXT.fetch_add(1, Ordering::Relaxed);
    let dir =
        std::env::temp_dir().join(format!("smith-install-{name}-{}-{id}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// A release on disk: every file, and `SHA256SUMS` made from them.
fn release(dir: &Path, version: &str) {
    std::fs::create_dir_all(dir).unwrap();
    let mut sums = String::new();
    for target in targets(true) {
        let bytes = compiler(version, &target);
        std::fs::write(dir.join(format!("smith-{target}")), &bytes).unwrap();
        sums.push_str(&format!("{}  smith-{target}\n", sha256::hex(&bytes)));
    }
    // A Windows file is in every release, and no part of the installer.
    for target in targets(false) {
        let bytes = compiler(version, &target);
        std::fs::write(dir.join(format!("smith-{target}.exe")), &bytes).unwrap();
        sums.push_str(&format!("{}  smith-{target}.exe\n", sha256::hex(&bytes)));
    }
    std::fs::write(dir.join("SHA256SUMS"), sums).unwrap();
}

/// A home, a folder of stand-ins for `uname` and `curl`, and a way to run the
/// installer there.
struct Setup {
    home: PathBuf,
    tools: PathBuf,
}

impl Setup {
    fn new(name: &str, machine: &Machine) -> Setup {
        let root = fresh(name);
        let home = root.join("home");
        let tools = root.join("tools");
        std::fs::create_dir_all(&home).unwrap();
        std::fs::create_dir_all(&tools).unwrap();
        let uname = format!(
            "#!/bin/sh\ncase \"$1\" in -s) echo '{}' ;; -m) echo '{}' ;; esac\n",
            machine.system, machine.chip
        );
        write_tool(&tools, "uname", &uname);
        Setup { home, tools }
    }

    /// A `curl` that gives up part way through a download of a compiler.
    fn break_downloads(&self) {
        let real = String::from_utf8(
            Command::new("sh")
                .args(["-c", "command -v curl"])
                .output()
                .unwrap()
                .stdout,
        )
        .unwrap();
        let script = format!(
            "#!/bin/sh\n\
             out=; for a in \"$@\"; do if [ \"$prev\" = -o ]; then out=$a; fi; prev=$a; done\n\
             case \"$*\" in\n\
             */smith-*) {real} \"$@\" || exit $?; head -c 4 \"$out\" > \"$out.part\"; mv \"$out.part\" \"$out\"; exit 18 ;;\n\
             esac\n\
             exec {real} \"$@\"\n",
            real = real.trim()
        );
        write_tool(&self.tools, "curl", &script);
    }

    fn bin(&self) -> PathBuf {
        self.home.join(".local/bin")
    }

    fn installed(&self) -> Option<String> {
        let output = Command::new(self.bin().join("smith"))
            .arg("--version")
            .output()
            .ok()?;
        Some(String::from_utf8(output.stdout).unwrap().trim().to_string())
    }

    fn run(&self, release: &Path, on_path: bool) -> Output {
        self.run_script(release, on_path, None)
    }

    /// Runs the installer as a file, or, given its text, piped into `sh` as
    /// the one-line install does.
    fn run_script(&self, release: &Path, on_path: bool, piped: Option<&[u8]>) -> Output {
        use std::io::Write;
        use std::process::Stdio;
        let mut path = format!(
            "{}:{}",
            self.tools.display(),
            std::env::var("PATH").unwrap()
        );
        if on_path {
            path = format!("{}:{path}", self.bin().display());
        }
        let mut command = Command::new("sh");
        if piped.is_none() {
            command.arg(format!("{}/../install.sh", env!("CARGO_MANIFEST_DIR")));
        }
        let mut child = command
            .env("HOME", &self.home)
            .env("PATH", path)
            .env("SMITH_RELEASE_URL", format!("file://{}", release.display()))
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
        let mut stdin = child.stdin.take().unwrap();
        if let Some(text) = piped {
            // The shell may stop reading before the end, as it does at a
            // syntax error.
            let _ = stdin.write_all(text);
        }
        drop(stdin);
        child.wait_with_output().unwrap()
    }
}

fn write_tool(dir: &Path, name: &str, text: &str) {
    use std::os::unix::fs::PermissionsExt;
    let path = dir.join(name);
    std::fs::write(&path, text).unwrap();
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).unwrap();
}

fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

fn the_installer_picks_the_file_for_the_machine_and_refuses_every_other() {
    generate::check(
        41,
        24,
        |g| (machine(g), version(g)),
        |(machine, version)| {
            let setup = Setup::new("pick", machine);
            let dir = setup.home.join("release");
            release(&dir, version);
            let output = setup.run(&dir, true);
            match &machine.target {
                Some(target) => {
                    assert!(output.status.success(), "{}", text(&output.stderr));
                    assert_eq!(
                        setup.installed().as_deref(),
                        Some(format!("smith {version} {target}").as_str())
                    );
                }
                None => {
                    assert!(!output.status.success());
                    let said = text(&output.stderr);
                    assert!(
                        said.contains(machine.system) && said.contains(machine.chip),
                        "{said}"
                    );
                    assert!(setup.installed().is_none());
                }
            }
        },
    );
}

/// A change to one file that keeps `SHA256SUMS` as it was: one byte altered,
/// or the end cut off.
fn damage(g: &mut Gen) -> usize {
    g.below(2)
}

fn corrupt(dir: &Path, target: &str, how: usize) {
    let path = dir.join(format!("smith-{target}"));
    let mut bytes = std::fs::read(&path).unwrap();
    if how == 0 {
        let last = bytes.len() - 1;
        bytes[last] ^= 1;
    } else {
        bytes.truncate(bytes.len() / 2);
    }
    std::fs::write(path, bytes).unwrap();
}

/// A machine that has a file in the release, and a smith already installed.
fn supported(g: &mut Gen) -> Machine {
    loop {
        let m = machine(g);
        if m.target.is_some() {
            return m;
        }
        // Shrinking draws zeros, which pick a supported machine at once.
    }
}

fn a_file_that_does_not_match_its_hash_installs_nothing() {
    generate::check(
        42,
        12,
        |g| (supported(g), damage(g)),
        |(machine, how)| {
            let setup = Setup::new("hash", machine);
            let target = machine.target.as_deref().unwrap();
            let old = setup.home.join("old");
            release(&old, "0.1.0");
            assert!(setup.run(&old, true).status.success());
            let before = std::fs::read(setup.bin().join("smith")).unwrap();

            let new = setup.home.join("new");
            release(&new, "0.2.0");
            corrupt(&new, target, *how);
            let output = setup.run(&new, true);
            assert!(!output.status.success());
            assert_eq!(std::fs::read(setup.bin().join("smith")).unwrap(), before);

            let fresh_home = Setup::new("hash-none", machine);
            assert!(!fresh_home.run(&new, true).status.success());
            assert!(fresh_home.installed().is_none());
        },
    );
}

fn a_download_that_fails_part_way_installs_nothing() {
    generate::check(43, 12, supported, |machine| {
        let setup = Setup::new("part", machine);
        let old = setup.home.join("old");
        release(&old, "0.1.0");
        assert!(setup.run(&old, true).status.success());
        let before = std::fs::read(setup.bin().join("smith")).unwrap();

        let new = setup.home.join("new");
        release(&new, "0.2.0");
        setup.break_downloads();
        assert!(!setup.run(&new, true).status.success());
        assert_eq!(std::fs::read(setup.bin().join("smith")).unwrap(), before);

        let none = Setup::new("part-none", machine);
        none.break_downloads();
        assert!(!none.run(&new, true).status.success());
        assert!(none.installed().is_none());
    });
}

fn running_the_installer_again_installs_the_newer_release() {
    generate::check(
        44,
        12,
        |g| (supported(g), version(g), version(g)),
        |(machine, first, second)| {
            let setup = Setup::new("again", machine);
            let target = machine.target.as_deref().unwrap();
            let one = setup.home.join("one");
            let two = setup.home.join("two");
            release(&one, first);
            release(&two, second);
            assert!(setup.run(&one, true).status.success());
            assert!(setup.run(&two, true).status.success());
            assert_eq!(
                setup.installed().as_deref(),
                Some(format!("smith {second} {target}").as_str())
            );
        },
    );
}

fn the_installer_says_so_only_when_the_folder_is_not_on_the_path() {
    generate::check(45, 12, supported, |machine| {
        let setup = Setup::new("path", machine);
        let dir = setup.home.join("release");
        release(&dir, "0.1.0");
        let line = "export PATH=\"$HOME/.local/bin:$PATH\"";
        let off = setup.run(&dir, false);
        assert!(off.status.success());
        let said = text(&off.stdout);
        assert!(said.lines().last().unwrap().contains(line), "{said}");
        let on = setup.run(&dir, true);
        assert!(on.status.success());
        let said = text(&on.stdout);
        assert!(!said.contains("PATH") && !said.contains(line), "{said}");
    });
}

fn the_installer_holds_no_version_and_no_hash() {
    let script =
        std::fs::read_to_string(format!("{}/../install.sh", env!("CARGO_MANIFEST_DIR"))).unwrap();
    let mut run = 0;
    for c in script.chars() {
        run = if c.is_ascii_hexdigit() { run + 1 } else { 0 };
        assert!(run < 64, "the installer holds a hash");
    }
    let tokens = script.split(|c: char| !(c.is_ascii_digit() || c == '.'));
    for token in tokens {
        let numbers: Vec<&str> = token.split('.').collect();
        let version = numbers.len() >= 3
            && numbers
                .iter()
                .all(|n| !n.is_empty() && n.bytes().all(|b| b.is_ascii_digit()));
        assert!(!version, "the installer holds a version: {token}");
    }
}

/// Every length the script can be cut to, shortest first, so that shrinking
/// finds the shortest cut that fails. The empty script is no cut: `sh` has
/// nothing to run, and no script can say otherwise.
fn cut(g: &mut Gen) -> usize {
    let length = std::fs::metadata(format!("{}/../install.sh", env!("CARGO_MANIFEST_DIR")))
        .unwrap()
        .len() as usize;
    1 + g.below(length)
}

fn a_script_cut_off_part_way_installs_the_whole_compiler_or_changes_nothing() {
    let script = std::fs::read(format!("{}/../install.sh", env!("CARGO_MANIFEST_DIR"))).unwrap();
    generate::check(
        46,
        20,
        |g| (supported(g), cut(g)),
        |(machine, length)| {
            let setup = Setup::new("cut", machine);
            let target = machine.target.as_deref().unwrap();
            let old = setup.home.join("old");
            release(&old, "0.1.0");
            assert!(setup.run(&old, true).status.success());
            let before = std::fs::read(setup.bin().join("smith")).unwrap();
            let listing = |dir: &Path| {
                let mut names: Vec<_> = std::fs::read_dir(dir)
                    .unwrap()
                    .map(|e| e.unwrap().file_name())
                    .collect();
                names.sort();
                names
            };
            let names = listing(&setup.bin());

            let new = setup.home.join("new");
            release(&new, "0.2.0");
            let output = setup.run_script(&new, true, Some(&script[..*length]));
            let installed = std::fs::read(setup.bin().join("smith")).unwrap();
            if installed == before {
                // Nothing changed: the cut script says it did not finish.
                assert_eq!(listing(&setup.bin()), names);
                assert!(!output.status.success(), "a cut script ended with success");
            } else {
                assert!(output.status.success());
                assert_eq!(
                    setup.installed().as_deref(),
                    Some(format!("smith 0.2.0 {target}").as_str())
                );
            }
        },
    );
    // The whole script, and the whole script without its last newline, which
    // is as much as a cut at the very end leaves.
    for end in [script.len() - 1, script.len()] {
        let machine = Machine {
            system: "Linux",
            chip: "x86_64",
            target: Some(format!("{}_{}", "linux", "x64")),
        };
        let setup = Setup::new("cut-end", &machine);
        let dir = setup.home.join("release");
        release(&dir, "0.2.0");
        assert!(
            setup
                .run_script(&dir, true, Some(&script[..end]))
                .status
                .success()
        );
        assert!(setup.installed().is_some());
    }
}
