//! Checks a parsed program against the rules of the language and lowers it
//! to the program every target receives.
//!
//! It collects every function's signature, then checks the body of each
//! function `main` reaches against them, and each struct those functions
//! name. A function or a struct that `main` does not reach is parsed and
//! not checked.
//!
//! A struct is lowered to its fields, one after another, until only texts
//! are left, so the form every target receives holds texts alone.

use std::collections::HashMap;

use crate::diag::{Error, Pos};
use crate::ir::{self, Step, Stream, Value};
use crate::parser::{Arg, Call, Expr, File, Function, Stmt, Struct, Type};

/// The effects the standard library declares, other than `console`.
const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects built into the compiler.
const BUILT_IN_EFFECTS: &[&str] = &["alloc", "spawn", "gpu", "diverge", "foreign", "exec"];

/// The one type the prelude defines that the compiler builds so far.
const TEXT: &str = "str";

/// The types the language and its library define that the compiler does
/// not build yet.
const TYPES_NOT_BUILT: &[&str] = &[
    "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "usize", "f16", "bf16", "f32",
    "f64", "Nat", "Pos", "Fin", "Ptr", "List", "Map", "Option", "Result",
];

/// What an expression gives.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Ty {
    Text,
    /// A struct, by its place in the file's structs.
    Struct(usize),
    Nothing,
    /// The expression holds a mistake already reported, so what it gives
    /// is not known and nothing about it is reported again.
    Unknown,
}

/// What the definitions of a file are named.
struct Defs<'a> {
    functions: &'a [Function],
    structs: &'a [Struct],
    function_names: HashMap<&'a str, usize>,
    struct_names: HashMap<&'a str, usize>,
    /// Whether each struct contains itself, directly or through others, as
    /// already reported. A type that names one is not known.
    cyclic: Vec<bool>,
}

impl Defs<'_> {
    /// The type a type written in the source stands for, with nothing
    /// reported.
    fn resolve(&self, t: &Type) -> Ty {
        if let Some(&s) = self.struct_names.get(t.name.as_str()) {
            if self.cyclic[s] {
                Ty::Unknown
            } else {
                Ty::Struct(s)
            }
        } else if t.name == TEXT {
            Ty::Text
        } else {
            Ty::Unknown
        }
    }

    /// The struct a call constructs, if it constructs one.
    fn constructs(&self, call: &Call) -> Option<usize> {
        self.struct_names.get(call.callee.as_str()).copied()
    }

    /// What a value of the type is, as an error describes it.
    fn describe(&self, ty: Ty) -> String {
        match ty {
            Ty::Text => "text".to_string(),
            Ty::Struct(s) => format!("a `{}`", self.structs[s].name),
            Ty::Nothing | Ty::Unknown => "nothing".to_string(),
        }
    }
}

/// Checks the file, and lowers the functions `main` reaches. Every error is
/// reported, except one that a mistake already reported would explain.
pub fn check(file: File) -> Result<ir::Program, Vec<Error>> {
    let File { functions, structs } = file;
    let mut errors = Vec::new();

    // The first definition of a name holds it, and a later one is a
    // mistake.
    let mut defined: Vec<(Pos, &str, bool)> = (functions.iter())
        .map(|f| (f.pos, f.name.as_str(), true))
        .chain(structs.iter().map(|s| (s.pos, s.name.as_str(), false)))
        .collect();
    defined.sort_by_key(|(pos, _, _)| *pos);
    let mut function_names: HashMap<&str, usize> = HashMap::new();
    let mut struct_names: HashMap<&str, usize> = HashMap::new();
    for (pos, name, is_function) in defined {
        if name == "print" {
            errors.push(Error::unsupported(
                pos,
                "defining `print`, which the prelude defines,",
            ));
        } else if name == TEXT && !is_function {
            errors.push(Error::unsupported(
                pos,
                "defining `str`, which the prelude defines,",
            ));
        } else if function_names.contains_key(name) {
            errors.push(Error::new(
                pos,
                format!("there is already a function named `{name}`"),
            ));
        } else if struct_names.contains_key(name) {
            errors.push(Error::new(
                pos,
                format!("there is already a struct named `{name}`"),
            ));
        } else if is_function {
            let at = functions.iter().position(|f| f.pos == pos).expect("listed");
            function_names.insert(name, at);
        } else {
            let at = structs.iter().position(|s| s.pos == pos).expect("listed");
            struct_names.insert(name, at);
        }
    }
    let Some(&main) = function_names.get("main") else {
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
            if let Some(&callee) = function_names.get(call.callee.as_str())
                && !reached.contains(&callee)
            {
                reached.push(callee);
            }
        }
        i += 1;
    }

    // The structs those functions reach, in the order they are first
    // reached.
    let mut reached_structs: Vec<usize> = Vec::new();
    let reach = |name: &str, reached_structs: &mut Vec<usize>| {
        if let Some(&s) = struct_names.get(name)
            && !reached_structs.contains(&s)
        {
            reached_structs.push(s);
        }
    };
    for &f in &reached {
        for name in types_named(&functions[f]) {
            reach(name, &mut reached_structs);
        }
    }
    let mut i = 0;
    while i < reached_structs.len() {
        for field in &structs[reached_structs[i]].fields {
            reach(&field.ty.name, &mut reached_structs);
        }
        i += 1;
    }

    let mut defs = Defs {
        functions: &functions,
        structs: &structs,
        function_names,
        struct_names,
        cyclic: vec![false; structs.len()],
    };
    let mut done = Vec::new();
    for &s in &reached_structs {
        if !done.contains(&s) {
            cycles(&mut defs, s, &mut Vec::new(), &mut done, &mut errors);
        }
    }

    let signatures: Vec<Signature> = functions.iter().map(|f| Signature::of(f, &defs)).collect();
    let mut checker = Checker {
        defs: &defs,
        signatures: &signatures,
        errors,
    };
    checker.main(&functions[main]);
    for &f in &reached {
        checker.signature(f);
    }
    for &s in &reached_structs {
        checker.structure(s);
    }
    for &f in &reached {
        checker.body(f);
    }
    checker.recursion(main, &mut Vec::new(), &mut Vec::new());
    if !checker.errors.is_empty() {
        return Err(checker.errors);
    }

    let mut lowering = Lowering {
        defs: &defs,
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

/// Reports each field whose type closes a cycle of structs that contain one
/// another, in a walk of the fields from struct `s`, and marks the structs
/// on the cycle. `path` is the structs being walked, and `done` those
/// already walked.
fn cycles(
    defs: &mut Defs,
    s: usize,
    path: &mut Vec<usize>,
    done: &mut Vec<usize>,
    errors: &mut Vec<Error>,
) {
    path.push(s);
    for field in &defs.structs[s].fields {
        let Some(&t) = defs.struct_names.get(field.ty.name.as_str()) else {
            continue;
        };
        if let Some(at) = path.iter().position(|p| *p == t) {
            errors.push(Error::unsupported(
                field.ty.pos,
                "a struct that contains itself, directly or through others,",
            ));
            for &p in &path[at..] {
                defs.cyclic[p] = true;
            }
        } else if !done.contains(&t) {
            cycles(defs, t, path, done, errors);
        }
    }
    path.pop();
    done.push(s);
}

/// Every expression in a function's body, each before the expressions
/// inside it, and then from left to right.
fn exprs(f: &Function) -> Vec<&Expr> {
    fn walk<'a>(expr: &'a Expr, out: &mut Vec<&'a Expr>) {
        out.push(expr);
        match expr {
            Expr::Call(call) => {
                for arg in &call.args {
                    walk(&arg.value, out);
                }
            }
            Expr::Field { of, .. } => walk(of, out),
            Expr::Text { .. } | Expr::Name { .. } => {}
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

/// Every call in a function's body, outermost first and then from left to
/// right. A construction is written as a call, so it is among them.
fn calls(f: &Function) -> Vec<&Call> {
    exprs(f)
        .into_iter()
        .filter_map(|e| match e {
            Expr::Call(call) => Some(call),
            _ => None,
        })
        .collect()
}

/// Every name a function uses as a type: in its signature, in its
/// bindings, and in what its body calls, which may be a struct.
fn types_named(f: &Function) -> Vec<&str> {
    let mut out: Vec<&str> = f.params.iter().map(|p| p.ty.name.as_str()).collect();
    out.extend(f.result.iter().map(|t| t.name.as_str()));
    for stmt in &f.body {
        if let Stmt::Define { ty: Some(t), .. } = stmt {
            out.push(&t.name);
        }
    }
    out.extend(calls(f).into_iter().map(|c| c.callee.as_str()));
    out
}

/// What a caller needs to know of a function.
struct Signature {
    params: Vec<(String, Ty)>,
    gives: Ty,
    /// The effects it carries that a caller must have, which are those it
    /// lists that the compiler builds.
    effects: Vec<String>,
}

impl Signature {
    fn of(f: &Function, defs: &Defs) -> Signature {
        Signature {
            params: (f.params.iter())
                .map(|p| (p.name.clone(), defs.resolve(&p.ty)))
                .collect(),
            gives: match &f.result {
                Some(t) => defs.resolve(t),
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
            params: vec![("text".to_string(), Ty::Text)],
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
    defs: &'a Defs<'a>,
    signatures: &'a [Signature],
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
    /// The names defined so far, and what each gives.
    names: Vec<(&'a str, Ty)>,
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

    /// Checks that a type written in the source is one the compiler builds.
    fn ty(&mut self, t: &Type) {
        if t.name == TEXT || self.defs.struct_names.contains_key(t.name.as_str()) {
            return;
        }
        self.errors
            .push(if TYPES_NOT_BUILT.contains(&t.name.as_str()) {
                Error::unsupported(t.pos, &format!("the type `{}`", t.name))
            } else {
                Error::new(t.pos, format!("there is no type named `{}`", t.name))
            });
    }

    /// Checks a struct's fields: each named once, each of a type there is.
    fn structure(&mut self, s: usize) {
        let s = &self.defs.structs[s];
        for (i, field) in s.fields.iter().enumerate() {
            if s.fields[..i].iter().any(|f| f.name == field.name) {
                self.errors.push(Error::new(
                    field.pos,
                    format!("`{}` is already a field of `{}`", field.name, s.name),
                ));
            }
            self.ty(&field.ty);
        }
    }

    /// Checks a signature: its types, and its list of effects.
    fn signature(&mut self, f: usize) {
        let f = &self.defs.functions[f];
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

    /// Reports a value that gives `given` where `needed` is needed.
    fn expect(&mut self, value: &Expr, given: Ty, needed: Ty) {
        match (given, needed) {
            (_, Ty::Unknown | Ty::Nothing) | (Ty::Unknown, _) => {}
            (Ty::Nothing, _) => self.errors.push(self.nothing_where(value, needed)),
            (given, needed) if given != needed => self.errors.push(Error::new(
                value.pos(),
                format!(
                    "this gives {}, where {} is needed",
                    self.defs.describe(given),
                    self.defs.describe(needed)
                ),
            )),
            _ => {}
        }
    }

    fn nothing_where(&self, value: &Expr, needed: Ty) -> Error {
        let what = match value {
            Expr::Call(call) => format!("`{}` returns nothing", call.callee),
            _ => "this gives nothing".to_string(),
        };
        Error::new(
            value.pos(),
            format!("{what}, where {} is needed", self.defs.describe(needed)),
        )
    }

    fn body(&mut self, f: usize) {
        let defs = self.defs;
        let f = &defs.functions[f];
        let signature = &self.signatures[defs.function_names[f.name.as_str()]];
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
            names: (f.params.iter())
                .map(|p| (p.name.as_str(), defs.resolve(&p.ty)))
                .collect(),
        };
        for (i, stmt) in f.body.iter().enumerate() {
            let last = i + 1 == f.body.len();
            let (gives, value) = match stmt {
                Stmt::Define {
                    name,
                    pos,
                    ty,
                    value,
                } => {
                    let gives = self.expr(&mut body, value);
                    let bound = match ty {
                        Some(t) => {
                            self.ty(t);
                            let needed = defs.resolve(t);
                            self.expect(value, gives, needed);
                            needed
                        }
                        None => {
                            if gives == Ty::Nothing {
                                self.errors
                                    .push(Error::unsupported(value.pos(), "binding nothing"));
                            }
                            gives
                        }
                    };
                    if body.names.iter().any(|(n, _)| n == name) {
                        self.errors.push(Error::new(
                            *pos,
                            format!("`{name}` is already defined in `{}`", f.name),
                        ));
                    } else {
                        body.names.push((name, bound));
                    }
                    if let (true, Some(t)) = (last, &f.result) {
                        self.errors.push(Error::new(
                            *pos,
                            format!(
                                "`{}` returns `{}`, and its body ends with a binding, \
                                 which gives nothing",
                                f.name, t.name
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
                (_, Some(_)) => self.expect(value, gives, signature.gives),
                (Ty::Text | Ty::Struct(_), None) => self.errors.push(Error::new(
                    value.pos(),
                    format!(
                        "`{}` returns nothing, and its body ends with {}",
                        f.name,
                        defs.describe(gives)
                    ),
                )),
                _ => {}
            }
        }
        if let (true, Some(t)) = (f.body.is_empty(), &f.result) {
            self.errors.push(Error::new(
                f.end,
                format!(
                    "`{}` returns `{}`, and its body is empty, which gives nothing",
                    f.name, t.name
                ),
            ));
        }
    }

    fn expr(&mut self, body: &mut Body<'a>, expr: &Expr) -> Ty {
        match expr {
            Expr::Text { .. } => Ty::Text,
            Expr::Name { name, pos } => {
                if let Some((_, ty)) = body.names.iter().find(|(n, _)| n == name) {
                    *ty
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
            Expr::Call(call) => match self.defs.constructs(call) {
                Some(s) => self.construction(body, call, s),
                None => self.call(body, call),
            },
            Expr::Field { of, name, pos } => {
                let defs = self.defs;
                match self.expr(body, of) {
                    Ty::Struct(s) => {
                        let s = &defs.structs[s];
                        match s.fields.iter().find(|f| &f.name == name) {
                            Some(field) => defs.resolve(&field.ty),
                            None => {
                                self.errors.push(Error::new(
                                    *pos,
                                    format!("`{}` has no field named `{name}`", s.name),
                                ));
                                Ty::Unknown
                            }
                        }
                    }
                    Ty::Unknown => Ty::Unknown,
                    given => {
                        self.errors.push(Error::new(
                            *pos,
                            format!(
                                "the field `{name}` is read from {}, which is not a struct",
                                defs.describe(given)
                            ),
                        ));
                        Ty::Unknown
                    }
                }
            }
        }
    }

    /// Checks the construction of struct `s`, which gives each of its
    /// fields once by name, in any order.
    fn construction(&mut self, body: &mut Body<'a>, call: &Call, s: usize) -> Ty {
        let defs = self.defs;
        let def = &defs.structs[s];
        let mut given = vec![false; def.fields.len()];
        let mut by_position = false;
        for arg in &call.args {
            let gives = self.expr(body, &arg.value);
            match &arg.name {
                None => {
                    if !by_position {
                        let mut error = Error::new(
                            arg.value.pos(),
                            format!("the fields of `{}` are given by name", def.name),
                        );
                        if let Some(at) = given.iter().position(|g| !g) {
                            error = error
                                .with_fix(format!("write `{}: ` before it", def.fields[at].name));
                        }
                        self.errors.push(error);
                    }
                    by_position = true;
                }
                Some((name, pos)) => match def.fields.iter().position(|f| &f.name == name) {
                    Some(i) if given[i] => self.errors.push(Error::new(
                        *pos,
                        format!("the `{name}` field is given twice"),
                    )),
                    Some(i) => {
                        given[i] = true;
                        self.expect(&arg.value, gives, defs.resolve(&def.fields[i].ty));
                    }
                    None => self.errors.push(Error::new(
                        *pos,
                        format!("`{}` has no field named `{name}`", def.name),
                    )),
                },
            }
        }
        let missing: Vec<&str> = (def.fields.iter())
            .zip(&given)
            .filter(|(_, given)| !**given)
            .map(|(f, _)| f.name.as_str())
            .collect();
        // A field given by position may be one of those missing.
        if !missing.is_empty() && !by_position {
            self.errors.push(Error::new(
                call.end,
                format!("`{}` needs a value for {}", def.name, names(&missing)),
            ));
        }
        if defs.cyclic[s] {
            Ty::Unknown
        } else {
            Ty::Struct(s)
        }
    }

    fn call(&mut self, body: &mut Body<'a>, call: &Call) -> Ty {
        let print = Signature::print();
        let is_print = call.callee == "print";
        let signature = if is_print {
            &print
        } else if let Some(&f) = self.defs.function_names.get(call.callee.as_str()) {
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
            // The parameter the argument is for, if it is for one.
            let mut param = None;
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
                    param = Some(next);
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
                    match signature.params.iter().position(|(p, _)| p == name) {
                        Some(i) if given[i] => self.errors.push(Error::new(
                            *pos,
                            format!("the `{name}` argument is given twice"),
                        )),
                        Some(i) => {
                            given[i] = true;
                            param = Some(i);
                        }
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
            if !look_into {
                continue;
            }
            let gives = self.expr(body, &arg.value);
            match param {
                Some(i) => self.expect(&arg.value, gives, signature.params[i].1),
                None if gives == Ty::Nothing => {
                    self.errors.push(self.nothing_where(&arg.value, Ty::Text));
                }
                None => {}
            }
        }
        let missing: Vec<&str> = signature
            .params
            .iter()
            .zip(&given)
            .filter(|(_, given)| !**given)
            .map(|((p, _), _)| p.as_str())
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
        for call in calls(&self.defs.functions[f]) {
            let Some(&callee) = self.defs.function_names.get(call.callee.as_str()) else {
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

/// Lowers checked functions to the form every target receives.
struct Lowering<'a> {
    defs: &'a Defs<'a>,
    signatures: &'a [Signature],
    /// The functions `main` reaches, each at its place in the program.
    reached: &'a [usize],
    texts: Vec<Vec<u8>>,
}

/// A function being lowered.
struct Lowered<'a> {
    /// What each name defined so far gives, and the texts it stands for.
    names: Vec<(&'a str, Ty, Vec<Value>)>,
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

    /// How many texts a value of the type is lowered to.
    fn width(&self, ty: Ty) -> usize {
        match ty {
            Ty::Text => 1,
            Ty::Struct(s) => (self.defs.structs[s].fields.iter())
                .map(|f| self.width(self.defs.resolve(&f.ty)))
                .sum(),
            Ty::Nothing | Ty::Unknown => 0,
        }
    }

    /// New locals for a value of the type.
    fn locals(&self, out: &mut Lowered, ty: Ty) -> Vec<usize> {
        let width = self.width(ty);
        out.locals += width;
        (out.locals - width..out.locals).collect()
    }

    fn function(&mut self, f: &'a Function) -> ir::Function {
        let mut out = Lowered {
            names: Vec::new(),
            locals: 0,
            steps: Vec::new(),
        };
        for p in &f.params {
            let ty = self.defs.resolve(&p.ty);
            let locals = self.locals(&mut out, ty);
            out.names
                .push((&p.name, ty, locals.into_iter().map(Value::Local).collect()));
        }
        let params = out.locals;
        let mut result = Vec::new();
        for stmt in &f.body {
            result = match stmt {
                Stmt::Define { name, value, .. } => {
                    // A name stands for the texts it is bound to, so a
                    // binding needs no step of its own.
                    let (ty, value) = self.expr(&mut out, value);
                    out.names.push((name, ty, value));
                    Vec::new()
                }
                Stmt::Expr(value) => self.expr(&mut out, value).1,
            };
        }
        if f.result.is_none() {
            result.clear();
        }
        ir::Function {
            params,
            locals: out.locals,
            steps: out.steps,
            results: result,
        }
    }

    /// The texts an expression gives, and their type.
    fn expr(&mut self, out: &mut Lowered<'a>, expr: &'a Expr) -> (Ty, Vec<Value>) {
        match expr {
            Expr::Text { text, .. } => (Ty::Text, vec![self.text(text)]),
            Expr::Name { name, .. } => {
                let found = out.names.iter().rev().find(|(n, _, _)| n == name);
                let (_, ty, values) = found.expect("checked");
                (*ty, values.clone())
            }
            Expr::Call(call) => match self.defs.constructs(call) {
                Some(s) => (Ty::Struct(s), self.construction(out, call, s)),
                None => self.call(out, call),
            },
            Expr::Field { of, name, .. } => {
                let (Ty::Struct(s), values) = self.expr(out, of) else {
                    unreachable!("checked")
                };
                let mut at = 0;
                for field in &self.defs.structs[s].fields {
                    let ty = self.defs.resolve(&field.ty);
                    let width = self.width(ty);
                    if &field.name == name {
                        return (ty, values[at..at + width].to_vec());
                    }
                    at += width;
                }
                unreachable!("checked")
            }
        }
    }

    /// Works out the fields in the order they are written, and gives their
    /// texts in the order the struct declares them.
    fn construction(&mut self, out: &mut Lowered<'a>, call: &'a Call, s: usize) -> Vec<Value> {
        let def = &self.defs.structs[s];
        let mut fields: Vec<Vec<Value>> = vec![Vec::new(); def.fields.len()];
        for Arg { name, value } in &call.args {
            let (_, value) = self.expr(out, value);
            let (name, _) = name.as_ref().expect("checked");
            let at = def.fields.iter().position(|f| &f.name == name);
            fields[at.expect("checked")] = value;
        }
        fields.concat()
    }

    fn call(&mut self, out: &mut Lowered<'a>, call: &'a Call) -> (Ty, Vec<Value>) {
        // Arguments are worked out in the order they are written, and then
        // passed in the order of the parameters.
        let print = Signature::print();
        let (signature, function) = match self.defs.function_names.get(call.callee.as_str()) {
            Some(&f) if call.callee != "print" => (
                &self.signatures[f],
                Some(self.reached.iter().position(|r| *r == f).expect("reached")),
            ),
            _ => (&print, None),
        };
        let mut args: Vec<Option<Vec<Value>>> = vec![None; signature.params.len()];
        let mut next = 0;
        for Arg { name, value } in &call.args {
            let (_, value) = self.expr(out, value);
            let at = match name {
                Some((name, _)) => signature.params.iter().position(|(p, _)| p == name),
                None => {
                    next += 1;
                    Some(next - 1)
                }
            };
            args[at.expect("checked")] = Some(value);
        }
        let args: Vec<Vec<Value>> = args.into_iter().map(|a| a.expect("checked")).collect();
        let Some(function) = function else {
            let newline = self.text(b"\n");
            for value in [args[0][0], newline] {
                out.steps.push(Step::Write {
                    stream: Stream::Stdout,
                    value,
                });
            }
            return (Ty::Nothing, Vec::new());
        };
        let results = self.locals(out, signature.gives);
        out.steps.push(Step::Call {
            function,
            args: args.concat(),
            results: results.clone(),
        });
        let values = results.into_iter().map(Value::Local).collect();
        (signature.gives, values)
    }
}
