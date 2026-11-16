//! Checks a parsed program against the rules of the language and lowers it
//! to the program every target receives.
//!
//! It collects every function's signature, then checks the body of each
//! function `main` reaches against them. A function that `main` does not
//! reach is parsed and not checked.

use std::collections::HashMap;

use crate::diag::{Error, Pos};
use crate::ir::{self, Step, Stream, Value};
use crate::parser::{Arg, Call, Expr, Function, Stmt, Type};

/// The effects the standard library declares, other than `console`.
const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects built into the compiler.
const BUILT_IN_EFFECTS: &[&str] = &["alloc", "spawn", "gpu", "diverge", "foreign", "exec"];

/// The one type the compiler builds so far.
const TEXT: &str = "str";

/// What an expression gives.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Ty {
    Text,
    Nothing,
    /// The expression holds a mistake already reported, so what it gives
    /// is not known and nothing about it is reported again.
    Unknown,
}

/// Checks the file's functions, and lowers those `main` reaches. Every
/// error is reported, except one that a mistake already reported would
/// explain.
pub fn check(functions: Vec<Function>) -> Result<ir::Program, Vec<Error>> {
    let mut errors = Vec::new();
    let mut by_name: HashMap<&str, usize> = HashMap::new();
    for (i, f) in functions.iter().enumerate() {
        if f.name == "print" {
            errors.push(Error::unsupported(
                f.pos,
                "defining `print`, which the prelude defines,",
            ));
        } else if by_name.contains_key(f.name.as_str()) {
            errors.push(Error::new(
                f.pos,
                format!("there is already a function named `{}`", f.name),
            ));
        } else {
            by_name.insert(&f.name, i);
        }
    }
    let Some(&main) = by_name.get("main") else {
        errors.push(Error::new(
            Pos::START,
            "there is no function named `main`, where the program starts",
        ));
        return Err(errors);
    };

    // The functions `main` reaches, in the order they are first reached.
    let mut reached = vec![main];
    let mut i = 0;
    while i < reached.len() {
        for call in calls(&functions[reached[i]]) {
            if let Some(&callee) = by_name.get(call.callee.as_str())
                && !reached.contains(&callee)
            {
                reached.push(callee);
            }
        }
        i += 1;
    }

    let signatures: Vec<Signature> = functions.iter().map(Signature::of).collect();
    let mut checker = Checker {
        functions: &functions,
        signatures: &signatures,
        by_name: &by_name,
        errors,
    };
    checker.main(&functions[main]);
    for &f in &reached {
        checker.signature(f);
    }
    for &f in &reached {
        checker.body(f);
    }
    checker.recursion(main, &mut Vec::new(), &mut Vec::new());
    if !checker.errors.is_empty() {
        return Err(checker.errors);
    }

    let mut lowering = Lowering {
        by_name: &by_name,
        signatures: &signatures,
        reached: &reached,
        texts: Vec::new(),
    };
    let lowered = reached
        .iter()
        .map(|&f| lowering.function(&functions[f]))
        .collect();
    Ok(ir::Program {
        texts: lowering.texts,
        functions: lowered,
        main: 0,
    })
}

/// Every call in a function's body, outermost first and then from left to
/// right.
fn calls(f: &Function) -> Vec<&Call> {
    fn walk<'a>(expr: &'a Expr, out: &mut Vec<&'a Call>) {
        if let Expr::Call(call) = expr {
            out.push(call);
            for arg in &call.args {
                walk(&arg.value, out);
            }
        }
    }
    let mut out = Vec::new();
    for stmt in &f.body {
        match stmt {
            Stmt::Define { value, .. } | Stmt::Expr(value) => walk(value, &mut out),
        }
    }
    out
}

/// What a caller needs to know of a function.
struct Signature {
    params: Vec<String>,
    gives: Ty,
    /// The effects it carries that a caller must have, which are those it
    /// lists that the compiler builds.
    effects: Vec<String>,
}

impl Signature {
    fn of(f: &Function) -> Signature {
        Signature {
            params: f.params.iter().map(|p| p.name.clone()).collect(),
            gives: match &f.result {
                Some(t) if t.name == TEXT => Ty::Text,
                Some(_) => Ty::Unknown,
                None => Ty::Nothing,
            },
            effects: f
                .effects
                .iter()
                .filter(|(e, _)| e == "console")
                .map(|(e, _)| e.clone())
                .take(1)
                .collect(),
        }
    }

    fn print() -> Signature {
        Signature {
            params: vec!["text".to_string()],
            gives: Ty::Nothing,
            effects: vec!["console".to_string()],
        }
    }
}

/// A list of names, each in backticks, joined as a sentence joins them.
fn names(list: &[&str]) -> String {
    let quoted: Vec<String> = list.iter().map(|n| format!("`{n}`")).collect();
    match quoted.as_slice() {
        [] => String::new(),
        [one] => one.clone(),
        [a, b] => format!("{a} and {b}"),
        [rest @ .., last] => format!("{}, and {last}", rest.join(", ")),
    }
}

/// A function's signature as it is written, with `effects` in place of
/// its list of effects.
fn written(f: &Function, effects: &[&str]) -> String {
    let params: Vec<String> = f
        .params
        .iter()
        .map(|p| format!("{}: {}", p.name, p.ty.name))
        .collect();
    let mut text = format!("fn {}({})", f.name, params.join(", "));
    if let Some(t) = &f.result {
        text.push_str(&format!(" -> {}", t.name));
    }
    if !effects.is_empty() {
        text.push_str(&format!(" ! {}", effects.join(", ")));
    }
    text
}

struct Checker<'a> {
    functions: &'a [Function],
    signatures: &'a [Signature],
    by_name: &'a HashMap<&'a str, usize>,
    errors: Vec<Error>,
}

/// What checking one body needs to keep.
struct Body<'a> {
    f: &'a Function,
    /// The effects it lists that have a name, each once.
    listed: Vec<&'a str>,
    /// Whether it lists an effect with no such name, which may be one it
    /// needs misspelt, so that a missing effect is then not reported.
    unknown_effect: bool,
    /// The effects already reported missing, each reported once.
    missing: Vec<String>,
    names: Vec<&'a str>,
}

impl<'a> Checker<'a> {
    /// What only `main` must be.
    fn main(&mut self, f: &Function) {
        if let Some(p) = f.params.first() {
            self.errors
                .push(Error::unsupported(p.pos, "a parameter of `main`"));
        }
        if let Some(t) = &f.result {
            self.errors
                .push(Error::unsupported(t.pos, "a return type on `main`"));
        }
    }

    fn ty(&mut self, t: &Type) {
        if t.name != TEXT {
            self.errors
                .push(Error::unsupported(t.pos, &format!("the type `{}`", t.name)));
        }
    }

    /// Checks a signature: its types, and its list of effects.
    fn signature(&mut self, f: usize) {
        let f = &self.functions[f];
        for (i, p) in f.params.iter().enumerate() {
            if f.params[..i].iter().any(|q| q.name == p.name) {
                self.errors.push(Error::new(
                    p.pos,
                    format!("`{}` is already defined in `{}`", p.name, f.name),
                ));
            }
            self.ty(&p.ty);
        }
        if let Some(t) = &f.result {
            self.ty(t);
        }
        let mut listed: Vec<&str> = Vec::new();
        for (effect, pos) in &f.effects {
            let effect = effect.as_str();
            if listed.contains(&effect) {
                self.errors.push(Error::new(
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

    fn body(&mut self, f: usize) {
        let f = &self.functions[f];
        let mut listed = Vec::new();
        let mut unknown_effect = false;
        for (effect, _) in &f.effects {
            let effect = effect.as_str();
            if !listed.contains(&effect) {
                listed.push(effect);
                unknown_effect |= effect != "console"
                    && !LIBRARY_EFFECTS.contains(&effect)
                    && !BUILT_IN_EFFECTS.contains(&effect);
            }
        }
        let mut body = Body {
            f,
            listed,
            unknown_effect,
            missing: Vec::new(),
            names: f.params.iter().map(|p| p.name.as_str()).collect(),
        };
        for (i, stmt) in f.body.iter().enumerate() {
            let last = i + 1 == f.body.len();
            let gives = match stmt {
                Stmt::Define {
                    name,
                    pos,
                    ty,
                    value,
                } => {
                    if let Some(t) = ty {
                        self.ty(t);
                    }
                    let gives = self.expr(&mut body, value);
                    if gives == Ty::Nothing {
                        self.errors.push(if ty.is_some() {
                            nothing_where_text(value)
                        } else {
                            Error::unsupported(value.pos(), "binding nothing")
                        });
                    }
                    if body.names.contains(&name.as_str()) {
                        self.errors.push(Error::new(
                            *pos,
                            format!("`{name}` is already defined in `{}`", f.name),
                        ));
                    } else {
                        body.names.push(name);
                    }
                    if last && f.result.is_some() {
                        self.errors.push(Error::new(
                            *pos,
                            format!(
                                "`{}` returns `{TEXT}`, and its body ends with a binding, \
                                 which gives nothing",
                                f.name
                            ),
                        ));
                    }
                    continue;
                }
                Stmt::Expr(value) => (self.expr(&mut body, value), value),
            };
            if !last {
                continue;
            }
            match (gives, &f.result) {
                ((Ty::Nothing, value), Some(_)) => self.errors.push(nothing_where_text(value)),
                ((Ty::Text, value), None) => self.errors.push(Error::new(
                    value.pos(),
                    format!("`{}` returns nothing, and its body ends with text", f.name),
                )),
                _ => {}
            }
        }
        if f.body.is_empty() && f.result.is_some() {
            self.errors.push(Error::new(
                f.end,
                format!(
                    "`{}` returns `{TEXT}`, and its body is empty, which gives nothing",
                    f.name
                ),
            ));
        }
    }

    fn expr(&mut self, body: &mut Body<'a>, expr: &Expr) -> Ty {
        match expr {
            Expr::Text { .. } => Ty::Text,
            Expr::Name { name, pos } => {
                if body.names.contains(&name.as_str()) {
                    Ty::Text
                } else {
                    self.errors.push(Error::new(
                        *pos,
                        format!(
                            "there is no name `{name}` defined before this in `{}`",
                            body.f.name
                        ),
                    ));
                    Ty::Unknown
                }
            }
            Expr::Call(call) => self.call(body, call),
        }
    }

    fn call(&mut self, body: &mut Body<'a>, call: &Call) -> Ty {
        let print = Signature::print();
        let is_print = call.callee == "print";
        let signature = if is_print {
            &print
        } else if let Some(&f) = self.by_name.get(call.callee.as_str()) {
            &self.signatures[f]
        } else {
            self.errors.push(Error::new(
                call.pos,
                format!("there is no function named `{}`", call.callee),
            ));
            for arg in &call.args {
                self.expr(body, &arg.value);
            }
            return Ty::Unknown;
        };

        for effect in &signature.effects {
            if body.listed.contains(&effect.as_str())
                || body.unknown_effect
                || body.missing.contains(effect)
            {
                continue;
            }
            body.missing.push(effect.clone());
            let mut effects = body.listed.clone();
            effects.extend(body.missing.iter().map(String::as_str));
            self.errors.push(
                Error::new(
                    call.pos,
                    format!(
                        "`{}` needs the `{effect}` effect, which `{}` does not declare",
                        call.callee, body.f.name
                    ),
                )
                .with_fix(format!("write `{}`", written(body.f, &effects))),
            );
        }
        let mut given: Vec<bool> = vec![false; signature.params.len()];
        let mut named = false;
        let mut too_many = false;
        let mut next = 0;
        for arg in &call.args {
            // An argument `print` does not take yet is not looked into.
            let mut look_into = true;
            match &arg.name {
                None if named => self.errors.push(
                    Error::new(
                        arg.value.pos(),
                        "an argument by position comes after a named one",
                    )
                    .with_fix("name it, or move it before the named arguments"),
                ),
                None if next < given.len() => {
                    given[next] = true;
                    next += 1;
                }
                None if is_print => {
                    look_into = false;
                    if !too_many {
                        self.errors.push(Error::unsupported(
                            arg.value.pos(),
                            "the `end` and `to` arguments of `print`",
                        ));
                    }
                    too_many = true;
                }
                None => {
                    if !too_many {
                        self.errors.push(Error::new(
                            arg.value.pos(),
                            format!(
                                "`{}` takes {} arguments, and this is one more",
                                call.callee,
                                given.len()
                            ),
                        ));
                    }
                    too_many = true;
                }
                Some((name, pos)) => {
                    named = true;
                    match signature.params.iter().position(|p| p == name) {
                        Some(i) if given[i] => self.errors.push(Error::new(
                            *pos,
                            format!("the `{name}` argument is given twice"),
                        )),
                        Some(i) => given[i] = true,
                        None if is_print && (name == "end" || name == "to") => {
                            look_into = false;
                            self.errors.push(Error::unsupported(
                                *pos,
                                "the `end` and `to` arguments of `print`",
                            ));
                        }
                        None => self.errors.push(Error::new(
                            *pos,
                            format!("`{}` has no parameter named `{name}`", call.callee),
                        )),
                    }
                }
            }
            if look_into && self.expr(body, &arg.value) == Ty::Nothing {
                self.errors.push(nothing_where_text(&arg.value));
            }
        }
        let missing: Vec<&str> = signature
            .params
            .iter()
            .zip(&given)
            .filter(|(_, given)| !**given)
            .map(|(p, _)| p.as_str())
            .collect();
        if is_print && !missing.is_empty() {
            self.errors.push(
                Error::new(call.end, "`print` needs the text to write")
                    .with_fix("write `print(\"hello\")`"),
            );
        } else if !missing.is_empty() {
            self.errors.push(Error::new(
                call.end,
                format!(
                    "`{}` needs an argument for {}",
                    call.callee,
                    names(&missing)
                ),
            ));
        }

        signature.gives
    }

    /// Reports each call that closes a cycle of calls, in a walk of the
    /// calls from `f`. `path` is the functions being walked, and `done` those
    /// already walked.
    fn recursion(&mut self, f: usize, path: &mut Vec<usize>, done: &mut Vec<usize>) {
        path.push(f);
        for call in calls(&self.functions[f]) {
            let Some(&callee) = self.by_name.get(call.callee.as_str()) else {
                continue;
            };
            if path.contains(&callee) {
                self.errors.push(Error::unsupported(
                    call.pos,
                    "a function that calls itself, directly or through others,",
                ));
            } else if !done.contains(&callee) {
                self.recursion(callee, path, done);
            }
        }
        path.pop();
        done.push(f);
    }
}

fn nothing_where_text(value: &Expr) -> Error {
    let what = match value {
        Expr::Call(call) => format!("`{}` returns nothing", call.callee),
        _ => "this gives nothing".to_string(),
    };
    Error::new(value.pos(), format!("{what}, where text is needed"))
}

/// Lowers checked functions to the form every target receives.
struct Lowering<'a> {
    by_name: &'a HashMap<&'a str, usize>,
    signatures: &'a [Signature],
    /// The functions `main` reaches, each at its place in the program.
    reached: &'a [usize],
    texts: Vec<Vec<u8>>,
}

/// A function being lowered.
struct Lowered<'a> {
    /// What each name defined so far stands for.
    names: Vec<(&'a str, Value)>,
    locals: usize,
    steps: Vec<Step>,
}

impl<'a> Lowering<'a> {
    fn text(&mut self, text: &[u8]) -> Value {
        let at = match self.texts.iter().position(|t| t == text) {
            Some(at) => at,
            None => {
                self.texts.push(text.to_vec());
                self.texts.len() - 1
            }
        };
        Value::Text(at)
    }

    fn function(&mut self, f: &'a Function) -> ir::Function {
        let mut out = Lowered {
            names: (f.params.iter().enumerate())
                .map(|(i, p)| (p.name.as_str(), Value::Local(i)))
                .collect(),
            locals: f.params.len(),
            steps: Vec::new(),
        };
        let mut result = None;
        for stmt in &f.body {
            result = match stmt {
                Stmt::Define { name, value, .. } => {
                    // A name stands for the value it is bound to, so a
                    // binding needs no step of its own.
                    let value = self.expr(&mut out, value).expect("checked");
                    out.names.push((name, value));
                    None
                }
                Stmt::Expr(value) => self.expr(&mut out, value),
            };
        }
        ir::Function {
            params: f.params.len(),
            locals: out.locals,
            steps: out.steps,
            result: result.filter(|_| f.result.is_some()),
        }
    }

    fn expr(&mut self, out: &mut Lowered<'a>, expr: &'a Expr) -> Option<Value> {
        match expr {
            Expr::Text { text, .. } => Some(self.text(text)),
            Expr::Name { name, .. } => {
                let found = out.names.iter().find(|(n, _)| n == name);
                Some(found.expect("checked").1)
            }
            Expr::Call(call) => self.call(out, call),
        }
    }

    fn call(&mut self, out: &mut Lowered<'a>, call: &'a Call) -> Option<Value> {
        // Arguments are worked out in the order they are written, and then
        // passed in the order of the parameters.
        let print = Signature::print();
        let (signature, function) = match self.by_name.get(call.callee.as_str()) {
            Some(&f) if call.callee != "print" => (
                &self.signatures[f],
                Some(self.reached.iter().position(|r| *r == f).expect("reached")),
            ),
            _ => (&print, None),
        };
        let mut args: Vec<Option<Value>> = vec![None; signature.params.len()];
        let mut next = 0;
        for Arg { name, value } in &call.args {
            let value = self.expr(out, value);
            let at = match name {
                Some((name, _)) => signature.params.iter().position(|p| p == name),
                None => {
                    next += 1;
                    Some(next - 1)
                }
            };
            args[at.expect("checked")] = value;
        }
        let args: Vec<Value> = args.into_iter().map(|a| a.expect("checked")).collect();
        let Some(function) = function else {
            let newline = self.text(b"\n");
            for value in [args[0], newline] {
                out.steps.push(Step::Write {
                    stream: Stream::Stdout,
                    value,
                });
            }
            return None;
        };
        let result = (signature.gives == Ty::Text).then(|| {
            out.locals += 1;
            out.locals - 1
        });
        out.steps.push(Step::Call {
            function,
            args,
            result,
        });
        result.map(Value::Local)
    }
}
