//! Checks effects: the effects a function lists and those a call or a name
//! needs, and what a default may carry.

use super::*;
use crate::parser::is_operation;

/// The effects the standard library declares, other than `console`.
pub(super) const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects the compiler builds.
pub(super) const BUILT: &[&str] = &["console", "alloc", "diverge"];

/// The effects built into the compiler.
pub(super) const BUILT_IN_EFFECTS: &[&str] =
    &["alloc", "spawn", "gpu", "diverge", "foreign", "exec"];

/// What the target answers an operation of an effect with, when the effect
/// reaches `main` with no handler: the effect and the operation as the
/// library's source declares them, what a call of the operation lowers to,
/// and the signature the compiler builds the call from, for as much of the
/// declaration as it builds.
pub(super) struct Provided {
    pub(super) effect: &'static str,
    pub(super) operation: &'static str,
    pub(super) answer: fn() -> Callee,
    pub(super) signature: fn() -> Signature,
}

/// The operations the target provides, which a handler will replace.
pub(super) const PROVIDED: &[Provided] = &[Provided {
    effect: "console",
    operation: "print",
    answer: || Callee::Print,
    signature: Signature::print,
}];

/// A name as the program writes it: without the module that a name which is
/// not listed is written with after it.
pub(super) fn written_name(name: &str) -> &str {
    name.split('@').next().unwrap_or(name)
}

impl<'a> Defs<'a> {
    /// Whether the file declares an effect with the name.
    pub(super) fn declares(&self, effect: &str) -> bool {
        self.file.effects.iter().any(|e| e.name == effect)
    }

    /// Whether a function of the file is an operation of an effect.
    pub(super) fn is_operation(&self, f: usize) -> bool {
        is_operation(&self.file.functions[f])
    }

    /// What the target provides for a function of the file, when it is the
    /// operation of an effect of the library that the target provides.
    pub(super) fn provided(&self, f: usize) -> Option<&'static Provided> {
        let f = &self.file.functions[f];
        if !is_operation(f) || !of_std(f.pos) {
            return None;
        }
        let effect = written_name(&f.effects.first()?.0);
        PROVIDED
            .iter()
            .find(|p| p.effect == effect && p.operation == written_name(&f.name))
    }

    /// What the target provides for a call of the function the name holds,
    /// if it is the operation of a provided effect.
    pub(super) fn provided_by_name(&self, name: &str) -> Option<&'static Provided> {
        self.provided(*self.function_names.get(name)?)
    }
}

/// The errors of the effects a file declares: an effect named twice, and the
/// effects `main` carries that nothing answers.
pub(super) fn declarations(file: &File, main: usize, errors: &mut Vec<Error>) {
    for (i, e) in file.effects.iter().enumerate() {
        if file.effects[..i].iter().any(|d| d.name == e.name) {
            errors.push(Error::new(
                e.pos,
                format!(
                    "there is already an effect named `{}`",
                    written_name(&e.name)
                ),
            ));
        }
    }
    for (effect, pos) in &file.functions[main].effects {
        let Some(declared) = file.effects.iter().find(|e| e.name == *effect) else {
            continue;
        };
        let provided =
            of_std(declared.pos) && PROVIDED.iter().any(|p| p.effect == written_name(effect));
        if !provided {
            errors.push(Error::new(
                *pos,
                format!(
                    "`main` carries the `{}` effect, which the target does not provide and no handler answers",
                    written_name(effect)
                ),
            ));
        }
    }
}

impl<'a> Checker<'a> {
    /// Checks a list of effects: each named once, each one there is.
    pub(super) fn effects(&mut self, effects: &[(String, Pos)]) {
        let mut listed: Vec<&str> = Vec::new();
        for (effect, pos) in effects {
            let effect = effect.as_str();
            if listed.contains(&effect) {
                self.errors.push(Error::new(
                    *pos,
                    format!("the `{effect}` effect is already listed"),
                ));
                continue;
            }
            listed.push(effect);
            if BUILT.contains(&effect) || self.defs.declares(effect) {
                continue;
            }
            if LIBRARY_EFFECTS.contains(&effect) || BUILT_IN_EFFECTS.contains(&effect) {
                self.errors
                    .push(Error::unsupported(*pos, &format!("the `{effect}` effect")));
            } else {
                self.errors.push(Error::new(
                    *pos,
                    format!("there is no effect named `{effect}`"),
                ));
            }
        }
    }

    /// Reports a call in a default of a function with this signature, if it
    /// carries an effect, and gives whether it does.
    pub(super) fn effect_in_default(
        &mut self,
        body: &Body,
        call: &Call,
        signature: &Signature,
    ) -> bool {
        let (Some(_), Some(effect)) = (&body.default, signature.effects.first()) else {
            return false;
        };
        self.errors.push(Error::new(
            call.pos,
            format!(
                "a default carries no effect, and `{}` needs the `{effect}` effect",
                call.callee
            ),
        ));
        true
    }

    /// Reports that what is at `pos`, which an error names as `what`, needs
    /// an effect the body does not list, unless that is already reported or
    /// a listed effect with no such name may be it misspelt. A default lists
    /// none, so each thing in it that needs one is reported.
    pub(super) fn needs(&mut self, body: &mut Body, effect: &str, pos: Pos, what: &str) {
        if body.listed.contains(&effect) || body.unknown_effect {
            return;
        }
        if body.default.is_some() {
            self.errors.push(Error::new(
                pos,
                format!("a default carries no effect, and {what} needs the `{effect}` effect"),
            ));
            return;
        }
        if body.missing.iter().any(|m| m == effect) {
            return;
        }
        body.missing.push(effect.to_string());
        let mut effects = body.listed.clone();
        effects.extend(body.missing.iter().map(String::as_str));
        self.errors.push(
            Error::new(
                pos,
                format!(
                    "{what} needs the `{effect}` effect, which `{}` does not declare",
                    body.owner().name
                ),
            )
            .with_fix(format!("write `{}`", written(body.owner(), &effects))),
        );
    }
}
