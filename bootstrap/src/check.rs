//! Checks a parsed program against the rules of the language and lowers it
//! to the program every target receives.

use crate::diag::{Error, Pos};
use crate::ir::{Program, Step, Stream};
use crate::parser::Function;

/// The effects the standard library declares, other than `console`.
const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects built into the compiler.
const BUILT_IN_EFFECTS: &[&str] = &["alloc", "spawn", "gpu", "diverge", "foreign", "exec"];

/// Checks the file's one function, if any, and lowers it. Every error is
/// reported, except one that a mistake already reported would explain.
pub fn check(function: Option<Function>) -> Result<Program, Vec<Error>> {
    let function = match function {
        Some(f) if f.name == "main" => f,
        _ => {
            return Err(vec![Error::new(
                Pos::START,
                "there is no function named `main`, where the program starts",
            )]);
        }
    };

    let mut errors = Vec::new();
    let mut listed: Vec<&str> = Vec::new();
    // An effect with no such name may be `console` misspelt, so a missing
    // `console` is then not reported.
    let mut unknown_effect = false;
    for (effect, pos) in &function.effects {
        let effect = effect.as_str();
        if listed.contains(&effect) {
            errors.push(Error::new(
                *pos,
                format!("the `{effect}` effect is already listed"),
            ));
            continue;
        }
        listed.push(effect);
        if effect == "console" {
            continue;
        }
        if LIBRARY_EFFECTS.contains(&effect) || BUILT_IN_EFFECTS.contains(&effect) {
            errors.push(Error::unsupported(*pos, &format!("the `{effect}` effect")));
        } else {
            unknown_effect = true;
            errors.push(Error::new(
                *pos,
                format!("there is no effect named `{effect}`"),
            ));
        }
    }

    let call = function.body;
    match call.callee.as_str() {
        "print" => {}
        "main" => {
            errors.push(Error::unsupported(
                call.pos,
                "calling a function other than `print`",
            ));
            return Err(errors);
        }
        name => {
            errors.push(Error::new(
                call.pos,
                format!("there is no function named `{name}`"),
            ));
            return Err(errors);
        }
    }
    let mut args = call.args.into_iter();
    let text = args.next();
    if text.is_none() {
        errors.push(
            Error::new(call.end, "`print` needs the text to write")
                .with_fix("write `print(\"hello\")`"),
        );
    }
    if let Some(extra) = args.next() {
        errors.push(Error::unsupported(
            extra.pos,
            "the `end` and `to` arguments of `print`",
        ));
    }
    if !listed.contains(&"console") && !unknown_effect {
        listed.push("console");
        errors.push(
            Error::new(
                call.pos,
                "`print` needs the `console` effect, which `main` does not declare",
            )
            .with_fix(format!("write `fn main() ! {}`", listed.join(", "))),
        );
    }
    let Some(text) = text.filter(|_| errors.is_empty()) else {
        return Err(errors);
    };

    let mut bytes = text.text;
    bytes.push(b'\n');
    Ok(Program {
        steps: vec![Step::Write {
            stream: Stream::Stdout,
            bytes,
        }],
    })
}
