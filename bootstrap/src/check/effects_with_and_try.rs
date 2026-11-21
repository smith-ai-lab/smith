//! Checks effects: the effects a function lists and those a call or a name
//! needs, and what a default may carry.

use super::*;
use crate::parser::{EffectDef, is_operation};

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
pub(super) const PROVIDED: &[Provided] = &[
    Provided {
        effect: "console",
        operation: "print",
        answer: || Callee::Print,
        signature: Signature::print,
    },
    Provided {
        effect: "entropy",
        operation: "fill",
        answer: || Callee::Fill,
        signature: Signature::fill,
    },
];

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

    /// Whether the copy is of an operation of an effect.
    pub(super) fn is_operation_of(&self, instance: &Instance) -> bool {
        matches!(instance.template, Template::Free(f) if self.is_operation(f))
    }

    /// The effect an operation is of, by its place among the file's, and its
    /// place among the effect's operations.
    pub(super) fn operation_of(&self, f: usize) -> (usize, usize) {
        let f = &self.file.functions[f];
        let effect = self.effect_named(&f.effects[0].0).expect("declared");
        let at = (self.file.effects[effect].operations.iter())
            .position(|o| *o == f.name)
            .expect("listed");
        (effect, at)
    }

    /// Whether a function of the file is an operation of an effect.
    pub(super) fn is_operation(&self, f: usize) -> bool {
        is_operation(&self.file.functions[f])
    }

    /// The place among the file's effects of the effect the name is the
    /// handler of, when it is the name of one.
    pub(super) fn effect_named(&self, name: &str) -> Option<usize> {
        self.file.effects.iter().position(|e| e.name == name)
    }

    /// The error for a handler written at `at`, of an effect that another
    /// module declares and has operations that it keeps private, if it is one.
    pub(super) fn private_operations(&self, effect: usize, at: Pos) -> Option<Error> {
        let def = &self.file.effects[effect];
        if !of_library(def.pos) || module_of(def.pos) == module_of(at) {
            return None;
        }
        let private: Vec<&str> = (def.operations.iter().zip(&def.public))
            .filter(|(_, public)| public.is_none())
            .map(|(name, _)| written_name(name))
            .collect();
        let first = private.first()?;
        let module = (self.file.modules.iter())
            .find(|(n, _)| *n == module_of(def.pos))
            .map_or("its module", |(_, name)| name.as_str());
        let file = match module.strip_prefix("std.") {
            Some(name) => format!("std/{name}.smith"),
            None => format!("{}.smith", module.replace('.', "/")),
        };
        let names: Vec<String> = private.iter().map(|n| format!("`{n}`")).collect();
        Some(
            Error::new(
                at,
                format!(
                    "`{}` has operations that are private to `{module}`, so it cannot be handled here: {}",
                    written_name(&def.name),
                    names.join(", ")
                ),
            )
            .with_fix(format!("write `pub` before `{first}` in `{file}`")),
        )
    }

    /// The signature of a call of an effect's name: a function for each
    /// operation, which takes all of the operation's parameters, and the body
    /// to run with them in force, which may carry the effect and gives what
    /// the call gives.
    pub(super) fn handler_signature(&self, effect: usize) -> Signature {
        let def = &self.file.effects[effect];
        let mut params: Vec<(String, Ty)> = Vec::new();
        for name in &def.operations {
            let f = self
                .function(def.pos, name)
                .expect("an operation is a function of the module that declares the effect");
            let sig = &self.signatures[f];
            let mut items: Vec<Ty> = sig.params.iter().map(|(_, t)| t.clone()).collect();
            items.push(sig.gives.clone());
            params.push((written_name(name).to_string(), Ty::Fn(items)));
        }
        params.push(("body".to_string(), Ty::Fn(vec![Ty::Param(0)])));
        let mut fn_effects = vec![Vec::new(); params.len()];
        fn_effects[params.len() - 1] = vec![def.name.clone()];
        Signature {
            scope: Scope {
                names: vec!["T".to_string()],
                invalid: vec![false],
                copy: vec![false],
                discard: vec![false],
                ..Scope::default()
            },
            defaults: vec![false; params.len()],
            params,
            param_defaults: vec![None],
            gives: Ty::Param(0),
            effects: Vec::new(),
            fn_effects,
        }
    }

    /// Whether an effect is one a handler answers: it is declared, and the
    /// target does not answer it.
    pub(super) fn handled(&self, effect: &str) -> bool {
        self.file
            .effects
            .iter()
            .any(|e| e.name == effect && !self.provided_effect(e))
    }

    /// Whether the target answers a declared effect: it is one of the
    /// library's that a row of the table answers.
    pub(super) fn provided_effect(&self, effect: &EffectDef) -> bool {
        answered(effect)
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
    pub(super) fn provided_by_name(&self, pos: Pos, name: &str) -> Option<&'static Provided> {
        self.provided(self.function(pos, name)?)
    }
}

/// Whether the target answers a declared effect: it is one of the library's
/// that a row of the table answers.
fn answered(effect: &EffectDef) -> bool {
    of_std(effect.pos)
        && PROVIDED
            .iter()
            .any(|p| p.effect == written_name(&effect.name))
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
        if !answered(declared) {
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
    /// The effects a lambda given for the parameter `param` of a call may
    /// carry beside those of the function it is written in.
    pub(super) fn lambda_effects(
        &self,
        signature: &Signature,
        param: Option<usize>,
        value: &Expr,
    ) -> Vec<String> {
        match (param, value) {
            (Some(i), Expr::Lambda(_)) => signature.fn_effects.get(i).cloned().unwrap_or_default(),
            _ => Vec::new(),
        }
    }

    /// Checks the effects written on the function types of the parameters.
    pub(super) fn parameter_effects(&mut self, def: &Function) {
        for p in &def.params {
            self.effects(&p.effects);
        }
    }

    /// Checks a call of the name of an effect, which handles it: the lambdas
    /// for the operations and the body, which gives what the call gives.
    pub(super) fn handler_call(&mut self, body: &mut Body<'a>, call: &Call, effect: usize) -> Ty {
        let signature = self.defs.handler_signature(effect);
        let before = self.errors.len();
        let private = self.defs.private_operations(effect, call.name_pos);
        let sol = self.arguments(body, call, &signature, false, None, None);
        // A handler names every operation, and one that is private to another
        // module cannot be named here. That is the one mistake of the call.
        if let Some(error) = private {
            self.errors.truncate(before);
            self.errors.push(error);
            self.refusals += 1;
        }
        signature.gives.solved(&sol)
    }

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
        // The default of a parameter that takes a function carries the effects
        // the type of the parameter lists, as the function calls it under them.
        let unlisted = (signature.effects.iter()).find(|e| !body.extra.contains(e));
        let (Some(_), Some(effect)) = (&body.default, unlisted) else {
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
        // A module of the library that lists an effect of another writes it
        // with that module after it, which is the effect the table names.
        if body.listed.contains(&effect)
            || body.listed.iter().any(|l| written_name(l) == effect)
            || body
                .extra
                .iter()
                .any(|e| e == effect || written_name(e) == effect)
            || body.unknown_effect
        {
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
