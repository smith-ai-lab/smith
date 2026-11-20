//! Checks effects: the effects a function lists and those a call or a name
//! needs, and what a default may carry.

use super::*;

/// The effects the standard library declares, other than `console`.
pub(super) const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects the compiler builds.
pub(super) const BUILT: &[&str] = &["console", "alloc", "diverge"];

/// The effects built into the compiler.
pub(super) const BUILT_IN_EFFECTS: &[&str] =
    &["alloc", "spawn", "gpu", "diverge", "foreign", "exec"];

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
            if BUILT.contains(&effect) {
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
