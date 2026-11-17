//! The `smith` command: `build`, `run`, and `check`, over the compiler in
//! `lib.rs`.

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use smith::diag;
use smith::target::Target;
use smith::timings::Timings;

const USAGE: &str = "usage: smith build [--target <target>] [--timings] [<file>.smith]
       smith run [--target <target>] [--timings] [<file>.smith [<word>...]]
       smith check [--timings] [<file>.smith]
       smith --version";

/// The version a release was built as, set by the release workflow. A build
/// that is not a release has none.
const VERSION: &str = match option_env!("SMITH_VERSION") {
    Some(version) => version,
    None => "dev",
};

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match command(&args) {
        Ok(code) => code,
        Err(message) => {
            eprintln!("{message}");
            ExitCode::FAILURE
        }
    }
}

/// What a `build` or `run` command line asks for.
struct Request {
    target: Target,
    entry: PathBuf,
    timings: bool,
    /// What follows the file's name in `smith run`, which are the words the
    /// program is started with.
    words: Vec<String>,
}

fn command(args: &[String]) -> Result<ExitCode, String> {
    let Some((name, rest)) = args.split_first() else {
        return Err(USAGE.to_string());
    };
    match name.as_str() {
        "--version" if rest.is_empty() => {
            println!("smith {VERSION}");
            Ok(ExitCode::SUCCESS)
        }
        "build" => {
            let request = request(rest, false)?;
            build(&request)?;
            Ok(ExitCode::SUCCESS)
        }
        "run" => {
            let request = request(rest, true)?;
            if Target::host() != Some(request.target) {
                return Err(format!(
                    "smith run runs only programs for this machine, and {} is not its target",
                    request.target.name()
                ));
            }
            let executable = build(&request)?;
            run(&executable, &request.words)
        }
        "check" => check(rest),
        "new" | "test" | "fmt" | "vendor" | "hole" | "find" | "deps" | "cost" => {
            Err(format!("smith {name} is not supported yet"))
        }
        _ => Err(format!("unknown command `{name}`\n{USAGE}")),
    }
}

/// What a command line asks for. When `with_words`, every word after the
/// file's name is for the program and no flag is read among them.
fn request(args: &[String], with_words: bool) -> Result<Request, String> {
    let mut target = None;
    let mut entry = None;
    let mut timings = false;
    let mut words = Vec::new();
    let mut args = args.iter();
    while let Some(arg) = args.next() {
        if arg == "--target" {
            let Some(name) = args.next() else {
                return Err("--target needs a target name".to_string());
            };
            target = Some(Target::from_name(name).ok_or_else(|| {
                let names: Vec<_> = Target::ALL.iter().map(|t| t.name()).collect();
                format!(
                    "unknown target `{name}`; the targets are {}",
                    names.join(", ")
                )
            })?);
        } else if arg == "--timings" {
            timings = true;
        } else if arg.starts_with("--") {
            return Err(format!("unknown flag `{arg}`\n{USAGE}"));
        } else if entry.is_some() {
            return Err(format!("only one file can be built\n{USAGE}"));
        } else {
            entry = Some(PathBuf::from(arg));
            if with_words {
                words = args.by_ref().cloned().collect();
            }
        }
    }
    let target = match target {
        Some(target) => target,
        None => Target::host().ok_or_else(|| {
            format!(
                "this machine, {}, is not a target; choose one with --target",
                Target::host_description()
            )
        })?,
    };
    Ok(Request {
        target,
        entry: entry.unwrap_or_else(|| PathBuf::from("main.smith")),
        timings,
        words,
    })
}

/// Checks the entry, `main.smith` unless a file is named, and reports its
/// errors, or else its warnings, writing no file. It fails if there are
/// any. With `--timings` it then prints how long each stage took.
fn check(args: &[String]) -> Result<ExitCode, String> {
    let mut entry = None;
    let mut timings = false;
    for arg in args {
        if arg == "--timings" {
            timings = true;
        } else if arg.starts_with("--") {
            return Err(format!("unknown flag `{arg}`\n{USAGE}"));
        } else if entry.is_some() {
            return Err(format!("only one file can be checked\n{USAGE}"));
        } else {
            entry = Some(PathBuf::from(arg));
        }
    }
    let entry = entry.unwrap_or_else(|| PathBuf::from("main.smith"));
    let (_, source) = read_entry(&entry)?;
    let mut stages = Timings::new();
    let found = match smith::check_timed(&source, &mut stages) {
        Ok(warnings) => warnings,
        Err(errors) => errors,
    };
    let mut report = diag::render(&entry.to_string_lossy(), &source, &found);
    if timings {
        report.push_str(&stages.report());
        report.push('\n');
    }
    eprint!("{report}");
    Ok(if found.is_empty() {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    })
}

/// The stem of an entry's name, and its text.
fn read_entry(entry: &Path) -> Result<(&std::ffi::OsStr, Vec<u8>), String> {
    let shown = entry.display();
    let stem = match (entry.extension(), entry.file_stem()) {
        (Some(ext), Some(stem)) if ext == "smith" && !stem.is_empty() => stem,
        _ => return Err(format!("{shown}: a Smith file's name ends in .smith")),
    };
    let source = std::fs::read(entry).map_err(|e| format!("{shown}: {e}"))?;
    Ok((stem, source))
}

/// Builds the entry, and with `--timings` prints how long each stage took:
/// after the errors when the build fails, and before returning when it
/// succeeds.
fn build(request: &Request) -> Result<PathBuf, String> {
    let mut timings = Timings::new();
    let result = build_timed(request, &mut timings);
    match (result, request.timings) {
        (Ok(path), true) => {
            eprintln!("{}", timings.report());
            Ok(path)
        }
        (Err(message), true) => Err(format!("{message}\n{}", timings.report())),
        (result, false) => result,
    }
}

/// Builds the entry into an executable in the current folder, and returns
/// its path.
fn build_timed(request: &Request, timings: &mut Timings) -> Result<PathBuf, String> {
    let entry = &request.entry;
    let (stem, source) = read_entry(entry)?;
    let bytes = smith::compile_timed(&source, request.target, timings).map_err(|errors| {
        let file = entry.to_string_lossy();
        let text = diag::render(&file, &source, &errors);
        text.strip_suffix('\n').unwrap_or(&text).to_string()
    })?;
    let cwd = std::env::current_dir().map_err(|e| format!("cannot find this folder: {e}"))?;
    let name = if stem == "main" {
        cwd.file_name().unwrap_or(stem)
    } else {
        stem
    };
    let output = cwd.join(request.target.executable_name(&name.to_string_lossy()));
    timings
        .stage("write", || write_executable(&output, &bytes))
        .map_err(|e| format!("{}: {e}", output.display()))?;
    Ok(output)
}

/// Writes the executable, replacing any file already there.
fn write_executable(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    if path.is_file() {
        std::fs::remove_file(path)?;
    }
    std::fs::write(path, bytes)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755))?;
    }
    Ok(())
}

/// Runs a built executable with the words it is started with, and exits as it
/// does.
fn run(executable: &Path, words: &[String]) -> Result<ExitCode, String> {
    let status = std::process::Command::new(executable)
        .args(words)
        .status()
        .map_err(|e| format!("{}: {e}", executable.display()))?;
    let code = status.code().unwrap_or(1);
    Ok(ExitCode::from(u8::try_from(code).unwrap_or(1)))
}
