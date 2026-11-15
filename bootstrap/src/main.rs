//! The bootstrap compiler for Smith. It is written in Rust with no
//! dependencies, and is retired once Smith compiles itself.

mod check;
mod diag;
mod ir;
mod lexer;
mod parser;
mod target;

#[cfg(test)]
mod tests;

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use target::Target;

const USAGE: &str = "usage: smith build [--target <target>] [<file>.smith]
       smith run [--target <target>] [<file>.smith]";

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

/// The program a source file describes, as the target's executable.
pub fn compile(source: &[u8], target: Target) -> Result<Vec<u8>, diag::Error> {
    let tokens = lexer::lex(source)?;
    let function = parser::parse(&tokens)?;
    let program = check::check(function)?;
    target.emit(&program).ok_or_else(|| {
        diag::Error::unsupported(diag::Pos::START, &format!("building for {}", target.name()))
    })
}

/// What a `build` or `run` command line asks for.
struct Request {
    target: Target,
    entry: PathBuf,
}

fn command(args: &[String]) -> Result<ExitCode, String> {
    let Some((name, rest)) = args.split_first() else {
        return Err(USAGE.to_string());
    };
    match name.as_str() {
        "build" => {
            let request = request(rest)?;
            build(&request)?;
            Ok(ExitCode::SUCCESS)
        }
        "run" => {
            let request = request(rest)?;
            if Target::host() != Some(request.target) {
                return Err(format!(
                    "smith run runs only programs for this machine, and {} is not its target",
                    request.target.name()
                ));
            }
            let executable = build(&request)?;
            run(&executable)
        }
        "new" | "check" | "test" | "fmt" | "vendor" | "hole" | "find" | "deps" | "cost" => {
            Err(format!("smith {name} is not supported yet"))
        }
        _ => Err(format!("unknown command `{name}`\n{USAGE}")),
    }
}

fn request(args: &[String]) -> Result<Request, String> {
    let mut target = None;
    let mut entry = None;
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
        } else if arg.starts_with("--") {
            return Err(format!("unknown flag `{arg}`\n{USAGE}"));
        } else if entry.is_some() {
            return Err(format!("only one file can be built\n{USAGE}"));
        } else {
            entry = Some(PathBuf::from(arg));
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
    })
}

/// Builds the entry into an executable in the current folder, and returns
/// its path.
fn build(request: &Request) -> Result<PathBuf, String> {
    let entry = &request.entry;
    let shown = entry.display();
    let stem = match (entry.extension(), entry.file_stem()) {
        (Some(ext), Some(stem)) if ext == "smith" && !stem.is_empty() => stem,
        _ => return Err(format!("{shown}: a Smith file's name ends in .smith")),
    };
    let source = std::fs::read(entry).map_err(|e| format!("{shown}: {e}"))?;
    let bytes = compile(&source, request.target).map_err(|e| format!("{shown}:{e}"))?;
    let cwd = std::env::current_dir().map_err(|e| format!("cannot find this folder: {e}"))?;
    let name = if stem == "main" {
        cwd.file_name().unwrap_or(stem).to_owned()
    } else {
        stem.to_owned()
    };
    let output = cwd.join(name);
    write_executable(&output, &bytes).map_err(|e| format!("{}: {e}", output.display()))?;
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

/// Runs a built executable and exits as it does.
fn run(executable: &Path) -> Result<ExitCode, String> {
    let status = std::process::Command::new(executable)
        .status()
        .map_err(|e| format!("{}: {e}", executable.display()))?;
    let code = status.code().unwrap_or(1);
    Ok(ExitCode::from(u8::try_from(code).unwrap_or(1)))
}
