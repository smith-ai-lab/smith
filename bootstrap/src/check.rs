//! Checks a parsed program against the rules of the language and lowers it
//! to the program every target receives.

use crate::diag::{Error, Pos};
use crate::ir::{Program, Step, Stream};
use crate::parser::Function;

/// The effects the standard library declares, other than `console`.
const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects built into the compiler.
const BUILT_IN_EFFECTS: &[&str] = &["alloc", "spawn", "gpu", "diverge", "foreign", "exec"];

/// Checks the file's one function, if any, and lowers it.
pub fn check(function: Option<Function>) -> Result<Program, Error> {
    let function = match function {
        Some(f) if f.name == "main" => f,
        _ => {
            return Err(Error::new(
                Pos::START,
                "there is no function named `main`, where the program starts",
            ));
        }
    };

    let mut has_console = false;
    for (i, (effect, pos)) in function.effects.iter().enumerate() {
        let effect = effect.as_str();
        if function.effects[..i].iter().any(|(e, _)| e == effect) {
            return Err(Error::new(
                *pos,
                format!("the `{effect}` effect is already listed"),
            ));
        }
        if effect == "console" {
            has_console = true;
        } else if LIBRARY_EFFECTS.contains(&effect) || BUILT_IN_EFFECTS.contains(&effect) {
            return Err(Error::unsupported(*pos, &format!("the `{effect}` effect")));
        } else {
            return Err(Error::new(
                *pos,
                format!("there is no effect named `{effect}`"),
            ));
        }
    }

    let call = function.body;
    match call.callee.as_str() {
        "print" => {}
        "main" => {
            return Err(Error::unsupported(
                call.pos,
                "calling a function other than `print`",
            ));
        }
        name => {
            return Err(Error::new(
                call.pos,
                format!("there is no function named `{name}`"),
            ));
        }
    }
    let mut args = call.args.into_iter();
    let Some(text) = args.next() else {
        return Err(Error::new(
            call.end,
            "`print` needs the text to write, as in `print(\"hello\")`",
        ));
    };
    if let Some(extra) = args.next() {
        return Err(Error::unsupported(
            extra.pos,
            "the `end` and `to` arguments of `print`",
        ));
    }
    if !has_console {
        return Err(Error::new(
            call.pos,
            "`print` needs the `console` effect, which `main` does not declare; \
             write `fn main() ! console`",
        ));
    }

    let mut bytes = text.text;
    bytes.push(b'\n');
    Ok(Program {
        steps: vec![Step::Write {
            stream: Stream::Stdout,
            bytes,
        }],
    })
}
