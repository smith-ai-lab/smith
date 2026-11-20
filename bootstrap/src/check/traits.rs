//! Traits: what a trait declares, what implements it, the bounds and
//! `requires` lines that ask for an implementation, the traits a type is
//! given, and the traits the prelude defines.

use super::*;

/// The traits the language and its prelude define, which the compiler
/// does not build yet.
pub(super) const TRAITS_NOT_BUILT: &[&str] = &[
    "Copy",
    "Discard",
    "Add",
    "Sub",
    "Mul",
    "Div",
    "Pow",
    "MatMul",
    "FromInt",
    "FromFloat",
    "FromString",
    "FromArray",
];

/// The names of the traits `PRELUDE` defines.
pub(super) const PRELUDE_TRAITS: &[&str] = &[
    "Fmt",
    "Repr",
    "Exit",
    "Try",
    "Iterate",
    "Eq",
    "Ord",
    "Hash",
    "Wrapping",
    "Saturating",
    "Checked",
    "Bits",
    "Convert",
];

/// Moves the `requires` lines that name a trait from the contracts of each
/// function to what it needs. Parsing does not know what a name refers to,
/// so a line that is `Name[...]` is one that names a trait here. A function
/// that the file shares with another, as the prelude's, was split where it
/// was read, and is left as it is.
pub(super) fn split_needs(mut file: File) -> File {
    let traits: Vec<String> = file.traits.iter().map(|t| t.name.clone()).collect();
    split_functions_needs(&mut file.functions, &traits);
    file
}

/// `split_needs` for functions that are read after the file is, given the
/// names of the file's traits.
pub(super) fn split_functions_needs(functions: &mut [std::sync::Arc<Function>], traits: &[String]) {
    let names_trait = |e: &Expr| {
        matches!(e, Expr::Index { of, .. }
            if matches!(&**of, Expr::Name { name, .. }
                if traits.contains(name) || TRAITS_NOT_BUILT.contains(&name.as_str())))
    };
    for f in functions {
        let Some(f) = std::sync::Arc::get_mut(f) else {
            continue;
        };
        let (needs, requires) = std::mem::take(&mut f.requires)
            .into_iter()
            .partition(|e| names_trait(e));
        f.needs = needs;
        f.requires = requires;
    }
}

/// The type a type written as an expression is, where `Name[Name, ...]` is
/// the one form of `f[n](x)` that a `requires` takes for a type.
fn expr_type(e: &Expr) -> Option<Type> {
    match e {
        Expr::Name { name, pos } => Some(Type {
            name: name.clone(),
            pos: *pos,
            args: Vec::new(),
            function: false,
            range: None,
        }),
        Expr::Index { of, index, .. } => {
            let Expr::Name { name, pos } = &**of else {
                return None;
            };
            let given: Vec<&Expr> = match &**index {
                Expr::Tuple { items, .. } => items.iter().collect(),
                one => vec![one],
            };
            Some(Type {
                name: name.clone(),
                pos: *pos,
                args: given.into_iter().map(expr_type).collect::<Option<_>>()?,
                function: false,
                range: None,
            })
        }
        Expr::Tuple { pos, items } => Some(Type {
            name: String::new(),
            pos: *pos,
            args: items.iter().map(expr_type).collect::<Option<_>>()?,
            function: false,
            range: None,
        }),
        _ => None,
    }
}

/// Whether some choice of the type parameters in both makes the two types
/// the same. Each side's parameters are in `sol`, those of `b` after those
/// of `a`.
fn overlap(a: &Ty, b: &Ty, sol: &mut Vec<Option<Ty>>) -> bool {
    fn walk(t: &Ty, sol: &[Option<Ty>]) -> Ty {
        let mut t = t.clone();
        while let Ty::Param(i) = t {
            match &sol[i] {
                Some(u) => t = u.clone(),
                None => break,
            }
        }
        t
    }
    fn occurs(p: usize, t: &Ty, sol: &[Option<Ty>]) -> bool {
        match t {
            Ty::Param(i) => *i == p || sol[*i].as_ref().is_some_and(|u| occurs(p, u, sol)),
            t => t.args().iter().any(|t| occurs(p, t, sol)),
        }
    }
    match (walk(a, sol), walk(b, sol)) {
        (Ty::Param(i), Ty::Param(j)) if i == j => true,
        (Ty::Param(i), t) | (t, Ty::Param(i)) => {
            if occurs(i, &t, sol) {
                false
            } else {
                sol[i] = Some(t);
                true
            }
        }
        (Ty::Struct(s, x), Ty::Struct(t, y)) | (Ty::Enum(s, x), Ty::Enum(t, y)) => {
            s == t && x.iter().zip(&y).all(|(a, b)| overlap(a, b, sol))
        }
        (Ty::Tuple(x), Ty::Tuple(y)) | (Ty::Fn(x), Ty::Fn(y)) => {
            x.len() == y.len() && x.iter().zip(&y).all(|(a, b)| overlap(a, b, sol))
        }
        (Ty::Unknown, _) | (_, Ty::Unknown) => true,
        (a, b) => a == b,
    }
}

/// What a type parameter must implement.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum Bound {
    None,
    /// A trait, by its place in the file's traits.
    Trait(usize),
    /// `Copy`, which every value of the type parameter may be given.
    Copy,
    /// `Discard`, in the same way.
    Discard,
    /// A bound already reported as a mistake.
    Invalid,
}

/// An implementation a definition requires: a trait and its types, among
/// the type parameters of the definition, `out` ones included.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(super) struct Requirement {
    pub(super) of: usize,
    pub(super) types: Vec<Ty>,
}

/// An implementation that can be used: its trait, its type parameters,
/// its types, and which of its functions is each of the trait's.
pub(super) struct ImplInfo {
    pub(super) of: usize,
    pub(super) scope: Scope,
    pub(super) args: Vec<Ty>,
    /// For each function of the trait, the place of the function that
    /// implements it among the implementation's, if one does.
    pub(super) fns: Vec<Option<usize>>,
    /// For each function of the trait, the type parameters its function is
    /// written among: the implementation's, then the function's own.
    pub(super) fn_scopes: Vec<Scope>,
    /// Whether it is chosen by calls, which an implementation for types an
    /// earlier one already covers is not.
    pub(super) chosen: bool,
}

/// What implements a trait for some types.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(super) enum By {
    /// A requirement of the definition promises an implementation.
    Bound,
    /// An implementation written in the source, with a type for each of
    /// its type parameters.
    Impl(usize, Vec<Ty>),
    /// The implementation a type is given automatically.
    Given,
}

/// What looking for an implementation found.
pub(super) enum Found {
    /// The types of the trait, `out` ones included, and what implements
    /// it.
    Yes(Vec<Ty>, By),
    No,
    /// Two implementations, by their places in the file's, both fit.
    Two(usize, usize),
    /// A mistake already reported stands in the way.
    Unknown,
}

/// The traits each `without` of the file takes away, each with the struct
/// or the enum it takes it from. Mistakes go in `errors`.
pub(super) fn withouts(defs: &Defs, errors: &mut Vec<Error>) -> Vec<(usize, Def)> {
    let mut out = Vec::new();
    for w in &defs.file.withouts {
        let name = w.name.as_str();
        let k = match defs.trait_at(w.name_pos, name) {
            Some(k)
                if [
                    defs.fmt,
                    defs.repr,
                    defs.eq,
                    defs.ord(),
                    defs.hash(),
                    defs.convert(),
                ]
                .contains(&k)
                    || defs.serialize == Some(k)
                    || defs.deserialize == Some(k) =>
            {
                k
            }
            Some(_) => {
                errors.push(Error::new(
                    w.name_pos,
                    format!(
                        "`{name}` is not given automatically, so `without` cannot take it away"
                    ),
                ));
                continue;
            }
            None if name == "Copy" => ownership::COPY,
            None if name == "Discard" => ownership::DISCARD,
            None if TRAITS_NOT_BUILT.contains(&name) => {
                errors.push(Error::unsupported(w.pos, &format!("`without {name}`")));
                continue;
            }
            None => {
                let message = if defs.is_type(name) {
                    format!("`{name}` is not a trait")
                } else {
                    format!("there is no trait named `{name}`")
                };
                errors.push(Error::new(w.name_pos, message));
                continue;
            }
        };
        let ty = w.ty.as_str();
        let d = if let Some(s) = defs.struct_at(w.ty_pos, ty) {
            Def::Struct(s)
        } else if let Some(e) = defs.enum_at(w.ty_pos, ty) {
            Def::Enum(e)
        } else {
            errors.push(if TYPES_NOT_BUILT.contains(&ty) {
                Error::unsupported(w.ty_pos, &format!("the type `{ty}`"))
            } else if ty == TEXT {
                Error::new(
                    w.ty_pos,
                    format!(
                        "`without` takes a trait from a struct or an enum, and `{ty}` is neither"
                    ),
                )
            } else if defs.trait_at(w.ty_pos, ty).is_some() {
                Error::new(w.ty_pos, format!("`{ty}` is a trait, which is not a type"))
            } else {
                Error::new(w.ty_pos, format!("there is no type named `{ty}`"))
            });
            continue;
        };
        // A conversion is given to the enum it goes to.
        let at = usize::from(k == defs.convert());
        if at == 1 && !matches!(d, Def::Enum(_)) {
            errors.push(Error::new(
                w.ty_pos,
                format!("`Convert` is given to an enum, and `{ty}` is not one"),
            ));
            continue;
        }
        let written = (defs.impls_of.get(k).into_iter().flatten())
            .filter_map(|&i| defs.impls[i].as_ref())
            .any(|info| head(&info.args[at]) == Some(d));
        if out.contains(&(k, d)) {
            errors.push(Error::new(
                w.pos,
                format!("`{name}` is already taken away from `{ty}`"),
            ));
        } else if written {
            errors.push(Error::new(
                w.pos,
                format!("`{ty}` implements `{name}` itself, so nothing is given to take away"),
            ));
        } else {
            out.push((k, d));
        }
    }
    out
}

/// What is known of an implementation before it is checked, if its trait
/// and its types are known.
pub(super) fn impl_info(defs: &Defs, im: &crate::parser::Impl) -> Option<ImplInfo> {
    let of = defs.trait_at(im.pos, im.name.as_str())?;
    let t = &defs.file.traits[of];
    let needed = t.generics.iter().filter(|g| g.default.is_none()).count();
    if im.args.len() < needed || im.args.len() > t.generics.len() {
        return None;
    }
    let scope = defs.scope(&im.generics, &im.requires);
    let mut args: Vec<Ty> = im.args.iter().map(|a| defs.quietly(a, &scope)).collect();
    defs.fill_defaults(&t.generics, &mut args);
    if args.contains(&Ty::Unknown) {
        return None;
    }
    // Each type parameter must be chosen by the types that are not `out`.
    let chosen = (0..scope.names.len())
        .all(|p| (args.iter().zip(&defs.outs[of])).any(|(a, out)| !out && a.mentions(p)));
    let fns = (t.functions.iter())
        .map(|f| {
            im.functions
                .iter()
                .position(|g| written_name(&g.name) == written_name(&f.name))
        })
        .collect::<Vec<Option<usize>>>();
    // A function has the type parameters of the trait's, as `implemented`
    // checks.
    let fn_scopes = (t.functions.iter())
        .map(|f| {
            let generics: Vec<Generic> = (im.generics.iter()).chain(&f.generics).cloned().collect();
            defs.scope(&generics, &im.requires)
        })
        .collect();
    Some(ImplInfo {
        of,
        scope,
        args,
        fns,
        fn_scopes,
        chosen,
    })
}

/// Whether two implementations of a trait are for some of the same types.
pub(super) fn overlaps(outs: &[bool], a: &ImplInfo, b: &ImplInfo) -> bool {
    let by = a.scope.names.len();
    let mut sol = vec![None; by + b.scope.names.len()];
    (a.args.iter().zip(&b.args).zip(outs))
        .filter(|(_, out)| !**out)
        .all(|((x, y), _)| overlap(x, &shifted(y, by), &mut sol))
}

/// Gives `out` the names the `requires` lines that name a trait use, with
/// where each is: the trait, and the types it is given.
pub(super) fn needs_named(needs: &[Expr], out: &mut impl FnMut(&str, Pos)) {
    fn walk(e: &Expr, out: &mut impl FnMut(&str, Pos)) {
        match e {
            Expr::Name { name, pos } => out(name, *pos),
            Expr::Index { of, index, .. } => {
                walk(of, out);
                walk(index, out);
            }
            Expr::Tuple { items, .. } => items.iter().for_each(|i| walk(i, out)),
            _ => {}
        }
    }
    needs.iter().for_each(|e| walk(e, out));
}

/// Gives `out` the names the bounds of type parameters use, with where each is.
pub(super) fn bounds_named(generics: &[Generic], out: &mut impl FnMut(&str, Pos)) {
    for bound in generics.iter().filter_map(|g| g.bound.as_ref()) {
        type_names_in(bound, out);
    }
}

/// The type with each of its type parameters moved by `by` places.
pub(super) fn shifted(t: &Ty, by: usize) -> Ty {
    match t {
        Ty::Param(i) => Ty::Param(i + by),
        t => t.with_args(t.args().iter().map(|t| shifted(t, by)).collect()),
    }
}

/// How type parameters are written, as in `[W: Sink, X]`, or nothing for none.
fn generics_written(generics: &[Generic]) -> String {
    if generics.is_empty() {
        return String::new();
    }
    let each: Vec<String> = (generics.iter())
        .map(|g| match &g.bound {
            Some(b) => format!("{}: {}", g.name, b.name),
            None => g.name.clone(),
        })
        .collect();
    format!("[{}]", each.join(", "))
}

/// The type parameters, as an error says what a function has.
fn generics_text(generics: &[Generic], first: bool) -> String {
    match (generics.is_empty(), first) {
        (true, true) => "no type parameters".to_string(),
        (true, false) => "none".to_string(),
        (false, true) => format!("the type parameters `{}`", generics_written(generics)),
        (false, false) => format!("`{}`", generics_written(generics)),
    }
}

/// The place among a trait's functions of the one a call names with the
/// trait. A function of a trait a module defines is written with its module
/// after it, unless the file that is built lists it, and a call names it as
/// the module wrote it.
pub(super) fn trait_function(t: &Trait, name: &str) -> Option<usize> {
    t.functions
        .iter()
        .position(|f| written_name(&f.name) == name)
}

impl<'a> Defs<'a> {
    /// The other traits the file in which a call is written can name that
    /// have a function the call could mean as well, each with the place of
    /// that function. A call of a function of one trait is not told from
    /// another's when the file lists the trait and not the function, so the
    /// call is written with the trait.
    pub(super) fn rivals(&self, k: usize, name: &str, at: Pos) -> Vec<(usize, usize)> {
        let Some((_, scope)) = (self.file.scopes.iter()).find(|(m, _)| *m == module_of(at)) else {
            return Vec::new();
        };
        let written = written_name(name);
        (scope.iter())
            .filter_map(|t| self.trait_named(t))
            .filter(|&j| j != k)
            .filter_map(|j| Some((j, trait_function(&self.file.traits[j], written)?)))
            .collect()
    }

    /// What a bound requires. Mistakes go in `errors`.
    pub(super) fn bound(&self, bound: &Option<Type>, errors: &mut Vec<Error>) -> Bound {
        let Some(t) = bound else {
            return Bound::None;
        };
        let name = t.name.as_str();
        let error = if !t.args.is_empty() {
            Error::unsupported(t.args[0].pos, "a bound with type arguments")
        } else if name == "Copy" || name == "Discard" {
            return if name == "Copy" {
                Bound::Copy
            } else {
                Bound::Discard
            };
        } else if let Some(k) = self.trait_at(t.pos, name) {
            return Bound::Trait(k);
        } else if name == "Nat" {
            Error::unsupported(t.pos, "a size parameter")
        } else if TRAITS_NOT_BUILT.contains(&name) {
            Error::unsupported(t.pos, &format!("the trait `{name}`"))
        } else if self.is_type(name) {
            Error::new(t.pos, format!("`{name}` is not a trait"))
        } else {
            Error::new(t.pos, format!("there is no trait named `{name}`"))
        };
        errors.push(error);
        Bound::Invalid
    }

    /// The type parameters of a definition, with the bounds and the
    /// `requires` lines that name a trait, with nothing reported.
    pub(super) fn scope(&self, generics: &[Generic], needs: &[Expr]) -> Scope {
        let mut scope = Scope {
            names: generics.iter().map(|g| g.name.clone()).collect(),
            requires: Vec::new(),
            invalid: vec![false; generics.len()],
            copy: vec![false; generics.len()],
            discard: vec![false; generics.len()],
        };
        for (p, g) in generics.iter().enumerate() {
            match self.bound(&g.bound, &mut Vec::new()) {
                Bound::None => {}
                Bound::Copy => scope.copy[p] = true,
                Bound::Discard => scope.discard[p] = true,
                Bound::Trait(of) => scope.requires.push(Requirement {
                    of,
                    types: vec![Ty::Param(p); self.outs[of].len()],
                }),
                Bound::Invalid => scope.invalid[p] = true,
            }
        }
        for (is_copy, p) in self.ownership_needs(needs, &scope, &mut Vec::new()) {
            if is_copy {
                scope.copy[p] = true;
            } else {
                scope.discard[p] = true;
            }
        }
        let mut found = Vec::new();
        for r in self.requirements(needs, &scope, &mut Vec::new()) {
            match r {
                Some(r) => found.push(r),
                None => scope.invalid.iter_mut().for_each(|i| *i = true),
            }
        }
        scope.requires.extend(found);
        scope
    }

    /// The `requires` lines that ask `Copy` or `Discard` of a type parameter
    /// of `scope`: whether each is `Copy`, and the parameter's place. A line
    /// that is not of this form is reported by `requirements`.
    pub(super) fn ownership_needs(
        &self,
        needs: &[Expr],
        scope: &Scope,
        errors: &mut Vec<Error>,
    ) -> Vec<(bool, usize)> {
        let mut found = Vec::new();
        for e in needs {
            let Expr::Index { of, index, .. } = e else {
                continue;
            };
            let Expr::Name { name, .. } = &**of else {
                continue;
            };
            if name != "Copy" && name != "Discard" {
                continue;
            }
            match &**index {
                Expr::Name { name: t, pos } => match scope.names.iter().position(|n| n == t) {
                    Some(p) => found.push((name == "Copy", p)),
                    None => errors.push(Error::new(
                        *pos,
                        format!("`{name}` is asked of a type parameter, and `{t}` is not one"),
                    )),
                },
                other => errors.push(Error::unsupported(
                    other.pos(),
                    &format!("`{name}` asked of anything but a type parameter"),
                )),
            }
        }
        found
    }

    /// What each `requires` line that names a trait requires, among the
    /// type parameters of `scope`. A line that is a mistake gives `None`,
    /// and goes in `errors`.
    pub(super) fn requirements(
        &self,
        needs: &[Expr],
        scope: &Scope,
        errors: &mut Vec<Error>,
    ) -> Vec<Option<Requirement>> {
        self.ownership_needs(needs, scope, errors);
        needs
            .iter()
            .filter(|e| {
                !matches!(e, Expr::Index { of, .. }
                    if matches!(&**of, Expr::Name { name, .. } if name == "Copy" || name == "Discard"))
            })
            .map(|e| {
                let Expr::Index { of, index, .. } = e else {
                    errors.push(Error::unsupported(e.pos(), "a `requires` of this form"));
                    return None;
                };
                let Expr::Name { name, pos } = &**of else {
                    errors.push(Error::unsupported(e.pos(), "a `requires` of this form"));
                    return None;
                };
                let Some(of) = self.trait_at(*pos, name) else {
                    errors.push(if TRAITS_NOT_BUILT.contains(&name.as_str()) {
                        Error::unsupported(*pos, &format!("the trait `{name}`"))
                    } else if self.is_type(name) {
                        Error::new(*pos, format!("`{name}` is not a trait"))
                    } else {
                        Error::new(*pos, format!("there is no trait named `{name}`"))
                    });
                    return None;
                };
                let given: Vec<&Expr> = match &**index {
                    Expr::Tuple { items, .. } => items.iter().collect(),
                    one => vec![one],
                };
                let arity = self.outs[of].len();
                if given.len() != arity && given.len() != 1 {
                    errors.push(Error::new(
                        *pos,
                        format!(
                            "`{name}` takes {}, and this gives {}",
                            count(arity, "type"),
                            given.len()
                        ),
                    ));
                    return None;
                }
                let before = errors.len();
                let mut types = Vec::new();
                for g in given {
                    let Some(t) = expr_type(g) else {
                        errors.push(Error::new(g.pos(), "this is not a type"));
                        return None;
                    };
                    types.push(self.resolve(&t, scope, errors));
                }
                if errors.len() > before {
                    return None;
                }
                if types.len() == 1 {
                    types = vec![types[0].clone(); arity];
                }
                Some(Requirement { of, types })
            })
            .collect()
    }

    /// The trait's types other than its `out` ones, as an error names them.
    pub(super) fn types_of(&self, k: usize, types: &[Ty], scope: &Scope) -> String {
        let shown: Vec<String> = (types.iter().zip(&self.outs[k]))
            .filter(|(t, out)| !**out && **t != Ty::Unknown)
            .map(|(t, _)| self.text(t, scope))
            .collect();
        let shown: Vec<&str> = shown.iter().map(String::as_str).collect();
        names(&shown)
    }

    /// The implementation of trait `k` for `types`, among the type
    /// parameters of `scope`. Only the types that are not `out` are read.
    pub(super) fn find(
        &self,
        k: usize,
        types: &[Ty],
        scope: &Scope,
        stack: &mut Vec<(usize, Vec<Ty>)>,
    ) -> Found {
        let outs = &self.outs[k];
        let given: Vec<Ty> = (types.iter().zip(outs))
            .filter(|(_, out)| !**out)
            .map(|(t, _)| t.clone())
            .collect();
        let explained = |t: &Ty| {
            *t == Ty::Unknown || (0..scope.names.len()).any(|p| scope.invalid[p] && t.mentions(p))
        };
        if given.iter().any(explained) {
            return Found::Unknown;
        }
        for r in scope.requires.iter().filter(|r| r.of == k) {
            let same = (r.types.iter().zip(types).zip(outs))
                .filter(|(_, out)| !**out)
                .all(|((a, b), _)| a == b);
            if same {
                return Found::Yes(r.types.clone(), By::Bound);
            }
        }
        let key = (k, given);
        if stack.contains(&key) {
            return Found::No;
        }
        stack.push(key);
        let found = self.find_impl(k, types, scope, stack);
        stack.pop();
        found
    }

    /// The types of trait `k` as its implementations are written for them.
    /// `Eq`, `Ord`, `Fmt`, `Repr`, and `Hash` are written once for every range,
    /// which is the implementation of the size it is kept at, so a range is
    /// the size it is kept at wherever one of these is looked up.
    fn kept_at_size(&self, k: usize, types: &[Ty]) -> Vec<Ty> {
        let by_size =
            k == self.eq || k == self.ord() || k == self.fmt || k == self.repr || k == self.hash();
        types
            .iter()
            .map(|t| match t {
                Ty::Int(r) if by_size => Ty::Int(r.holder()),
                other => other.clone(),
            })
            .collect()
    }

    fn find_impl(
        &self,
        k: usize,
        types: &[Ty],
        scope: &Scope,
        stack: &mut Vec<(usize, Vec<Ty>)>,
    ) -> Found {
        let kept = self.kept_at_size(k, types);
        let types = &kept[..];
        let mut fit: Option<(usize, Vec<Ty>, Vec<Ty>)> = None;
        'impls: for &i in &self.impls_of[k] {
            let Some(info) = &self.impls[i] else {
                continue;
            };
            if !info.chosen {
                continue;
            }
            let mut sol = vec![None; info.scope.names.len()];
            let fits = (info.args.iter().zip(types).zip(&self.outs[k]))
                .filter(|(_, out)| !**out)
                .all(|((pattern, given), _)| unify(pattern, given, &mut sol));
            if !fits {
                continue;
            }
            let Some(sol) = sol.into_iter().collect::<Option<Vec<Ty>>>() else {
                return Found::Unknown;
            };
            if info.scope.invalid.contains(&true) {
                return Found::Unknown;
            }
            for r in &info.scope.requires {
                let needed: Vec<Ty> = r.types.iter().map(|t| t.subst(&sol)).collect();
                match self.meets_types(r.of, &needed, scope, stack) {
                    Found::Yes(..) => {}
                    Found::No => continue 'impls,
                    other => return other,
                }
            }
            if let Some((j, ..)) = &fit {
                return Found::Two(*j, i);
            }
            let all = info.args.iter().map(|t| t.subst(&sol)).collect();
            fit = Some((i, all, sol));
        }
        if let Some((i, all, sol)) = fit {
            return Found::Yes(all, By::Impl(i, sol));
        }
        match self.given_parts(k, types) {
            Some(parts) => self.given(k, types, &parts, scope, stack),
            None => Found::No,
        }
    }

    /// The parts of the type of trait `k` whose implementations a given one
    /// writes it with, if the trait is one a type is given automatically
    /// and the type is one it is given to, and nothing takes it away: the
    /// fields of a struct, what the cases of an enum hold, and the elements
    /// of a tuple. A `str` is given `Repr`, `Eq`, `Ord`, and `Hash`, and `()` each of
    /// the four, which have no parts, and a whole number and a list `Hash`.
    fn given_parts(&self, k: usize, types: &[Ty]) -> Option<Vec<Ty>> {
        if k == self.convert() {
            let Ty::Enum(e, args) = &types[1] else {
                return None;
            };
            return (!self.withouts.contains(&(k, Def::Enum(*e)))
                && self.holding(*e, args, &types[0]).len() == 1)
                .then(Vec::new);
        }
        if self.serialize == Some(k) || self.deserialize == Some(k) {
            let taken = |d: Def| self.withouts.contains(&(k, d));
            return match &types[0] {
                Ty::Text | Ty::Nothing | Ty::Int(_) => Some(Vec::new()),
                Ty::List(of) => Some(vec![of[0].clone()]),
                Ty::Struct(s, args) if !taken(Def::Struct(*s)) => {
                    Some(self.fields[*s].iter().map(|t| t.subst(args)).collect())
                }
                Ty::Enum(e, args) if !taken(Def::Enum(*e)) => (self.cases[*e].iter().flatten())
                    .map(|t| Some(t.subst(args)))
                    .collect(),
                Ty::Tuple(items) => Some(items.clone()),
                _ => None,
            };
        }
        if k != self.fmt && k != self.repr && k != self.eq && k != self.ord() && k != self.hash() {
            return None;
        }
        let taken = |d: Def| self.withouts.contains(&(k, d));
        match &types[0] {
            Ty::Text if k != self.fmt => Some(Vec::new()),
            Ty::Nothing => Some(Vec::new()),
            Ty::Int(_) if k == self.hash() => Some(Vec::new()),
            Ty::List(of) if k == self.hash() => Some(vec![of[0].clone()]),
            Ty::List(of) | Ty::Slice(of) if k == self.fmt || k == self.repr || k == self.eq => {
                Some(vec![of[0].clone()])
            }
            Ty::Struct(s, args) if !taken(Def::Struct(*s)) => {
                Some(self.fields[*s].iter().map(|t| t.subst(args)).collect())
            }
            Ty::Enum(e, args) if !taken(Def::Enum(*e)) => (self.cases[*e].iter().flatten())
                .map(|t| Some(t.subst(args)))
                .collect(),
            Ty::Tuple(items) => Some(items.clone()),
            _ => None,
        }
    }

    /// The trait `Ord`, which the prelude defines.
    pub(super) fn ord(&self) -> usize {
        self.ord
    }

    /// The trait `Hash`, which the prelude defines.
    pub(super) fn hash(&self) -> usize {
        self.hash
    }

    /// The trait `Convert`, which the prelude defines.
    pub(super) fn convert(&self) -> usize {
        self.convert
    }

    /// The names of the cases of enum `e`, with the types `args`, that hold a `t`.
    pub(super) fn holding(&self, e: usize, args: &[Ty], t: &Ty) -> Vec<&str> {
        (self.file.enums[e].cases.iter().zip(&self.cases[e]))
            .filter(|(_, held)| held.as_ref().is_some_and(|h| h.subst(args) == *t))
            .map(|(case, _)| case.name.as_str())
            .collect()
    }

    /// The implementation of trait `k` given to `types`, whose parts are
    /// `parts`: it is given when each part has it, and, for `Fmt`, which
    /// writes what `Repr` writes, when the type has `Repr`.
    pub(super) fn given(
        &self,
        k: usize,
        types: &[Ty],
        parts: &[Ty],
        scope: &Scope,
        stack: &mut Vec<(usize, Vec<Ty>)>,
    ) -> Found {
        for part in parts {
            match self.meets(k, part, scope, stack) {
                Found::Yes(..) => {}
                other => return other,
            }
        }
        if k == self.fmt {
            match self.meets(self.repr, &types[0], scope, stack) {
                Found::Yes(..) => {}
                other => return other,
            }
        }
        Found::Yes(types.to_vec(), By::Given)
    }

    /// The copy that runs function `fi` of trait `k` for `types`, which hold
    /// no type parameter, and the trait's types, `out` ones included.
    pub(super) fn chosen(
        &self,
        k: usize,
        fi: usize,
        types: Vec<Ty>,
        own: Vec<Ty>,
    ) -> (Instance, Vec<Ty>) {
        let Found::Yes(all, by) = self.find(k, &types, &Scope::default(), &mut Vec::new()) else {
            unreachable!("checked")
        };
        let instance = match by {
            By::Impl(i, mut args) => {
                args.extend(own);
                Instance {
                    template: Template::Impl(i, fi),
                    args,
                }
            }
            By::Given => Instance {
                template: Template::Given(k),
                args: types.into_iter().chain(own).collect(),
            },
            By::Bound => unreachable!("a copy has no type parameter"),
        };
        (instance, all)
    }

    /// The copies a copy of a given implementation runs: for `Fmt`, the
    /// `Repr` of its type, and for `Repr`, `Eq`, and `Ord`, that of each part.
    pub(super) fn given_calls(&self, k: usize, types: &[Ty]) -> Vec<Instance> {
        if self.serialize == Some(k) {
            return self.serialize_calls(&types[0], &types[1]);
        }
        if self.deserialize == Some(k) {
            return self.deserialize_calls(&types[0], &types[1]);
        }
        let run = |k: usize, t: &Ty| self.chosen(k, 0, vec![t.clone()], Vec::new()).0;
        if k == self.fmt {
            vec![run(self.repr, &types[0])]
        } else {
            let parts = self.given_parts(k, types).expect("given");
            parts.iter().map(|p| run(k, p)).collect()
        }
    }

    /// The copies a copy of the `Serialize` given to `t` runs for a writer of
    /// type `w`: the `Serialize` of each part, and the functions of the
    /// writer's `Writer` that the shape of `t` is written with.
    fn serialize_calls(&self, t: &Ty, w: &Ty) -> Vec<Instance> {
        let serialize = self
            .serialize
            .expect("a program that writes has the module");
        let writer = self.writer.expect("a program that writes has the module");
        let names: &[&str] = match t {
            Ty::Int(i) if i.signed() => &["signed"],
            Ty::Int(_) => &["unsigned"],
            Ty::Text => &["text"],
            Ty::Nothing => &["nothing"],
            Ty::Tuple(_) => &["tuple", "end_tuple"],
            Ty::Struct(..) => &["structure", "field", "end_structure"],
            Ty::List(_) => &["sequence", "end_sequence"],
            Ty::Enum(e, _) if *e == self.boolean => &["boolean"],
            Ty::Enum(e, _) if *e == self.option => &["present", "absent"],
            _ => &["case"],
        };
        let mut out: Vec<Instance> = names
            .iter()
            .filter_map(|name| trait_function(&self.file.traits[writer], name))
            .map(|at| self.chosen(writer, at, vec![w.clone()], Vec::new()).0)
            .collect();
        let parts = self
            .given_parts(serialize, std::slice::from_ref(t))
            .expect("given");
        out.extend(parts.iter().map(|p| {
            self.chosen(serialize, 0, vec![p.clone()], vec![w.clone()])
                .0
        }));
        out
    }

    /// The copies a copy of the `Deserialize` given to `t` runs for a reader
    /// of type `r`: the `Deserialize` of each part, and the functions of the
    /// reader's `Reader` that the shape of `t` is read with.
    fn deserialize_calls(&self, t: &Ty, r: &Ty) -> Vec<Instance> {
        let deserialize = self
            .deserialize
            .expect("a program that reads has the module");
        let reader = self.reader.expect("a program that reads has the module");
        let names: &[&str] = match t {
            Ty::Int(i) if i.signed() => &["signed", "fail"],
            Ty::Int(_) => &["unsigned", "fail"],
            Ty::Text => &["text"],
            Ty::Nothing => &["nothing"],
            Ty::Tuple(_) => &["tuple", "end_tuple"],
            Ty::Struct(..) => &["structure", "field", "unknown", "fail"],
            Ty::List(_) => &["sequence", "more"],
            Ty::Enum(e, _) if *e == self.boolean => &["boolean"],
            Ty::Enum(e, _) if *e == self.option => &["present"],
            _ => &["case", "end_case", "fail"],
        };
        let mut out: Vec<Instance> = names
            .iter()
            .filter_map(|name| trait_function(&self.file.traits[reader], name))
            .map(|at| self.chosen(reader, at, vec![r.clone()], Vec::new()).0)
            .collect();
        let parts = self
            .given_parts(deserialize, std::slice::from_ref(t))
            .expect("given");
        out.extend(parts.iter().map(|p| {
            self.chosen(deserialize, 0, vec![p.clone()], vec![r.clone()])
                .0
        }));
        out
    }

    /// The trait and the part of `t` that lacks it, for a type `t` that
    /// lacks trait `k` only because one of its parts lacks a trait. The part
    /// is the innermost one, which lacks the trait on its own.
    fn lacking(&self, k: usize, t: &Ty, scope: &Scope) -> Option<(usize, Ty)> {
        fn walk(defs: &Defs, k: usize, t: &Ty, scope: &Scope) -> (usize, Ty) {
            let types = [t.clone()];
            let written = (defs.impls_of[k].iter())
                .filter_map(|&i| defs.impls[i].as_ref())
                .any(|info| {
                    info.chosen && unify(&info.args[0], t, &mut vec![None; info.scope.names.len()])
                });
            let parts = if written {
                None
            } else {
                defs.given_parts(k, &types)
            };
            let Some(parts) = parts else {
                return (k, t.clone());
            };
            let lacks =
                |k: usize, t: &Ty| matches!(defs.meets(k, t, scope, &mut Vec::new()), Found::No);
            if let Some(part) = parts.iter().find(|p| lacks(k, p)) {
                return walk(defs, k, part, scope);
            }
            if k == defs.fmt && lacks(defs.repr, t) {
                return walk(defs, defs.repr, t, scope);
            }
            (k, t.clone())
        }
        let (lacks, part) = walk(self, k, t, scope);
        (lacks != k || part != *t).then_some((lacks, part))
    }

    /// The end of the error that says `t` has no implementation of trait
    /// `k`, which names the part of `t` that lacks a trait, if that is why.
    pub(super) fn because(&self, k: usize, t: &Ty, scope: &Scope) -> String {
        match self.lacking(k, t, scope) {
            Some((lacks, part)) => format!(
                ", as `{}` has no `{}`",
                self.text(&part, scope),
                self.file.traits[lacks].name
            ),
            None => String::new(),
        }
    }

    /// The types of trait `k` that an implementation for the types that are
    /// not `out` gives, where those may be partly known. The types an
    /// implementation does not fix are unknown. Gives `None` when no
    /// implementation fits.
    pub(super) fn fitting(&self, k: usize, types: &[Ty]) -> Option<Vec<Ty>> {
        for &i in &self.impls_of[k] {
            let Some(info) = &self.impls[i] else {
                continue;
            };
            if !info.chosen {
                continue;
            }
            let mut sol = vec![None; info.scope.names.len()];
            let fits = (info.args.iter().zip(types).zip(&self.outs[k]))
                .filter(|(_, out)| !**out)
                .all(|((pattern, given), _)| unify(pattern, given, &mut sol));
            if fits {
                return Some(info.args.iter().map(|t| t.solved(&sol)).collect());
            }
        }
        None
    }

    /// Whether `t` meets the bound `T: Trait`, which asks for an
    /// implementation whose every type is `t`.
    pub(super) fn meets(
        &self,
        k: usize,
        t: &Ty,
        scope: &Scope,
        stack: &mut Vec<(usize, Vec<Ty>)>,
    ) -> Found {
        self.meets_types(k, &vec![t.clone(); self.outs[k].len()], scope, stack)
    }

    /// Whether `types` meet the requirement `requires Trait[types]`, which
    /// asks for an implementation whose types, `out` ones included, are
    /// these.
    pub(super) fn meets_types(
        &self,
        k: usize,
        types: &[Ty],
        scope: &Scope,
        stack: &mut Vec<(usize, Vec<Ty>)>,
    ) -> Found {
        match self.find(k, types, scope, stack) {
            Found::Yes(all, by) if all == self.kept_at_size(k, types) => Found::Yes(all, by),
            Found::Yes(..) => Found::No,
            other => other,
        }
    }

    /// The error for a use at `pos` that implementations `a` and `b` both
    /// fit.
    pub(super) fn ambiguous(&self, pos: Pos, a: usize, b: usize) -> Error {
        let describe = |i: usize| {
            let info = self.impls[i].as_ref().expect("known");
            let types: Vec<String> = (info.args.iter())
                .map(|t| self.text(t, &info.scope))
                .collect();
            format!(
                "`impl {}[{}]` at line {}",
                self.file.traits[info.of].name,
                types.join(", "),
                self.file.impls[i].pos.line
            )
        };
        Error::new(
            pos,
            format!("{} and {} both fit this use", describe(a), describe(b)),
        )
    }
}

impl<'a> Checker<'a> {
    /// Checks a trait: its types, and the signature of each of its
    /// functions, whose parameters and result must name each type that is not `out`.
    pub(super) fn declared_trait(&mut self, k: usize) {
        let defs = self.defs;
        let t = &defs.file.traits[k];
        self.generics(&t.generics, &format!("`{}`", t.name), Owner::Trait);
        for (i, f) in t.functions.iter().enumerate() {
            if t.functions[..i].iter().any(|g| g.name == f.name) {
                self.errors.push(Error::new(
                    f.pos,
                    format!("`{}` is already a function of `{}`", f.name, t.name),
                ));
                continue;
            }
            self.generics(&f.generics, &format!("`{}`", f.name), Owner::Function);
            for g in &f.generics {
                if t.generics.iter().any(|h| h.name == g.name) {
                    self.errors.push(Error::new(
                        g.pos,
                        format!("`{}` is already a type parameter of `{}`", g.name, t.name),
                    ));
                }
                if g.default.is_some() {
                    self.errors.push(Error::unsupported(
                        g.pos,
                        "a default for a type parameter of a trait's function",
                    ));
                }
                let named =
                    (f.params.iter()).any(|p| type_names(&p.ty).iter().any(|(n, _)| *n == g.name));
                if !named {
                    self.errors.push(Error::unsupported(
                        g.pos,
                        "a type parameter that no parameter's type names",
                    ));
                }
            }
            self.params(f, &defs.trait_sigs[k][i].scope);
            self.defaults(f, &defs.trait_sigs[k][i]);
            self.effects(&f.effects);
            for g in t.generics.iter().filter(|g| g.out.is_none()) {
                let named = (f.params.iter().map(|p| &p.ty))
                    .chain(&f.result)
                    .any(|t| type_names(t).iter().any(|(n, _)| *n == g.name));
                if !named {
                    self.errors.push(Error::unsupported(
                        f.pos,
                        &format!(
                            "a function of a trait whose parameters' and result's types do not name `{}`",
                            g.name
                        ),
                    ));
                }
            }
        }
    }

    /// Checks an implementation: its trait, its types, that it is for types
    /// no earlier one is for, and that it has each of the trait's functions
    /// once, with the trait's signature, and no other function.
    pub(super) fn implementation(&mut self, i: usize) {
        let defs = self.defs;
        let im = &defs.file.impls[i];
        let name = im.name.as_str();
        let Some(k) = defs.trait_at(im.pos, name) else {
            self.errors.push(if TRAITS_NOT_BUILT.contains(&name) {
                Error::unsupported(im.pos, &format!("implementing `{name}`"))
            } else if defs.is_type(name) {
                Error::new(im.pos, format!("`{name}` is not a trait"))
            } else {
                Error::new(im.pos, format!("there is no trait named `{name}`"))
            });
            return;
        };
        if k == defs.eq && !of_prelude(im.pos) {
            self.errors.push(Error::new(
                im.pos,
                "`==` means that two values are the same value, so `Eq` is given to a type \
                 and is not written; a looser comparison is a function with its own name"
                    .to_string(),
            ));
            return;
        }
        let t = &defs.file.traits[k];
        self.generics(&im.generics, "this implementation", Owner::Impl);
        let scope = defs.scope(&im.generics, &im.requires);
        for a in &im.args {
            self.ty(a, &scope);
        }
        defs.requirements(&im.requires, &scope, &mut self.errors);
        let needed = t.generics.iter().filter(|g| g.default.is_none()).count();
        if im.args.len() < needed || im.args.len() > t.generics.len() {
            let takes = if needed == t.generics.len() {
                count(t.generics.len(), "type")
            } else {
                format!("{needed} to {}", count(t.generics.len(), "type"))
            };
            self.errors.push(Error::new(
                im.pos,
                format!("`{name}` takes {takes}, and this gives {}", im.args.len()),
            ));
            return;
        }
        let Some(info) = &defs.impls[i] else {
            return;
        };
        let mut chosen = true;
        for g in &im.generics {
            let named = (im.args.iter().zip(&t.generics))
                .filter(|(_, tg)| tg.out.is_none())
                .any(|(a, _)| type_names(a).iter().any(|(n, _)| *n == g.name));
            if !named {
                chosen = false;
                self.errors.push(Error::unsupported(
                    g.pos,
                    "a type parameter of an implementation that its types other than `out` ones \
                     do not name",
                ));
            }
        }
        if chosen && !info.chosen {
            self.errors.push(Error::new(
                im.pos,
                format!(
                    "`{name}` is already implemented for {}",
                    defs.types_of(k, &info.args, &info.scope)
                ),
            ));
        }
        for (j, f) in im.functions.iter().enumerate() {
            if im.functions[..j].iter().any(|g| g.name == f.name) {
                self.errors.push(Error::new(
                    f.pos,
                    format!(
                        "`{}` is already in this implementation",
                        written_name(&f.name)
                    ),
                ));
                continue;
            }
            match t
                .functions
                .iter()
                .position(|g| written_name(&g.name) == written_name(&f.name))
            {
                Some(fi) => self.implemented(i, fi, f),
                None => self.errors.push(Error::new(
                    f.pos,
                    format!("`{}` is not a function of `{name}`", written_name(&f.name)),
                )),
            }
        }
        let lacking: Vec<&str> = (t.functions.iter().zip(&info.fns))
            .filter(|(_, at)| at.is_none())
            .map(|(f, _)| written_name(&f.name))
            .collect();
        if !lacking.is_empty() {
            self.errors.push(Error::new(
                im.pos,
                format!("this implementation of `{name}` lacks {}", names(&lacking)),
            ));
        }
    }

    /// Checks that function `f` of implementation `i` has the signature of
    /// the trait's function `fi`, for the implementation's types.
    pub(super) fn implemented(&mut self, i: usize, fi: usize, f: &Function) {
        let defs = self.defs;
        let info = defs.impls[i].as_ref().expect("known");
        let trait_fn = &defs.file.traits[info.of].functions[fi];
        let sig = &defs.trait_sigs[info.of][fi];
        let trait_name = &defs.file.traits[info.of].name;
        for p in &f.params {
            if let Some(value) = &p.default {
                self.errors.push(
                    Error::new(
                        value.pos(),
                        format!(
                            "the defaults of `{}` are stated in `{trait_name}`, and an \
                             implementation does not repeat them",
                            f.name
                        ),
                    )
                    .with_fix(format!("remove the default of `{}`", p.name)),
                );
            }
        }
        let scope = &info.fn_scopes[fi];
        // The function has the type parameters of the trait's, under the same
        // names and with the same bounds.
        let same_generics = f.generics.len() == trait_fn.generics.len()
            && (f.generics.iter().zip(&trait_fn.generics)).all(|(a, b)| {
                a.name == b.name
                    && a.default.is_none()
                    && a.bound.as_ref().map(|t| &t.name) == b.bound.as_ref().map(|t| &t.name)
            });
        if !same_generics {
            self.errors.push(
                Error::new(
                    f.generics.first().map_or(f.pos, |g| g.pos),
                    format!(
                        "`{}` has {} here, and {} in `{trait_name}`",
                        f.name,
                        generics_text(&f.generics, true),
                        generics_text(&trait_fn.generics, false)
                    ),
                )
                .with_fix(format!(
                    "write `fn {}{}`",
                    f.name,
                    generics_written(&trait_fn.generics)
                )),
            );
            return;
        }
        let before = self.errors.len();
        self.params(f, scope);
        self.effects(&f.effects);
        if self.errors.len() > before {
            return;
        }
        // The trait's types are the implementation's, and the function's own
        // type parameters stand for themselves.
        let n = info.scope.names.len();
        let mut env = info.args.clone();
        env.extend((0..f.generics.len()).map(|m| Ty::Param(n + m)));
        let wanted: Vec<(String, Ty)> = (sig.params.iter())
            .map(|(n, t)| (n.clone(), t.subst(&env)))
            .collect();
        let gives = sig.gives.subst(&env);
        let written: Vec<(String, Ty)> = (f.params.iter())
            .map(|p| (p.name.clone(), defs.quietly(&p.ty, scope)))
            .collect();
        let written_gives = match &f.result {
            Some(t) => defs.quietly(t, scope),
            None => Ty::Nothing,
        };
        fn set(effects: &[(String, Pos)]) -> Vec<&str> {
            let mut set: Vec<&str> = effects.iter().map(|(e, _)| e.as_str()).collect();
            set.sort_unstable();
            set.dedup();
            set
        }
        let same = written == wanted
            && written_gives == gives
            && set(&f.effects) == set(&trait_fn.effects);
        if same {
            return;
        }
        let params: Vec<String> = (wanted.iter())
            .map(|(n, t)| format!("{n}: {}", defs.text(t, scope)))
            .collect();
        let mut text = format!(
            "fn {}{}({})",
            f.name,
            generics_written(&f.generics),
            params.join(", ")
        );
        if gives != Ty::Nothing {
            text.push_str(&format!(" -> {}", defs.text(&gives, scope)));
        }
        let effects = set(&trait_fn.effects);
        if !effects.is_empty() {
            text.push_str(&format!(" ! {}", effects.join(", ")));
        }
        self.errors.push(
            Error::new(
                f.pos,
                format!("`{}` differs from its signature in `{trait_name}`", f.name),
            )
            .with_fix(format!("write `{text}`")),
        );
    }

    /// Checks a call of function `fi` of trait `k`, which runs the
    /// implementation its types choose.
    pub(super) fn trait_call(
        &mut self,
        body: &mut Body<'a>,
        call: &Call,
        k: usize,
        fi: usize,
        want: &Want,
    ) -> Ty {
        let defs = self.defs;
        let sig = &defs.trait_sigs[k][fi];
        if self.effect_in_default(body, call, sig) {
            return self.unknown_call(body, call);
        }
        let outer = std::mem::replace(&mut self.no_impl, false);
        let refused = self.refusals;
        let mistakes = self.errors.len();
        let mut sol = self.arguments(body, call, sig, false, Some((k, want)), None);
        let reported = std::mem::replace(&mut self.no_impl, outer);
        if reported || self.refusals != refused || self.function_for_parameter(&sol, call.pos) {
            return Ty::Unknown;
        }
        // A type that only the result names is the one that is expected.
        let outs = &defs.outs[k];
        if let Want::Ty(needed @ (Ty::Int(_) | Ty::Enum(..) | Ty::Struct(..) | Ty::Tuple(_))) = want
        {
            let mut wanted = vec![None; sol.len()];
            unify(&sig.gives, needed, &mut wanted);
            for ((t, w), out) in sol.iter_mut().zip(wanted).zip(outs) {
                if t.is_none() && !out {
                    *t = w;
                }
            }
        }
        // A type an argument names that is not known is that of a mistake
        // already reported, so only one that the result alone names is
        // reported here.
        let only_result = |r: usize| !sig.params.iter().any(|(_, p)| p.mentions(r));
        if matches!(want, Want::Any | Want::Unused)
            && (sol.iter().zip(outs).enumerate())
                .any(|(i, (t, out))| !out && t.is_none() && only_result(i))
            && self.errors.len() == mistakes
        {
            self.errors.push(
                Error::new(
                    call.pos,
                    format!(
                        "`{}` gives the type that is expected here, and none is",
                        call.callee
                    ),
                )
                .with_fix("write the type, as in `y: u32 = convert(x)`"),
            );
            return Ty::Unknown;
        }
        if (sol.iter().zip(outs)).any(|(t, out)| !out && t.is_none()) {
            return Ty::Unknown;
        }
        // The function's own type parameters follow the trait's, and the
        // arguments settle them.
        let n = outs.len();
        if sol[n..].iter().any(Option::is_none) {
            return Ty::Unknown;
        }
        let own: Vec<Ty> = sol[n..].iter().flatten().map(Ty::concrete).collect();
        let types: Vec<Ty> = sol[..n]
            .iter()
            .map(|t| t.as_ref().map_or(Ty::Unknown, Ty::concrete))
            .collect();
        self.own_bounds(body, call, sig, n, &sol);
        match defs.find(k, &types, body.scope, &mut Vec::new()) {
            Found::Yes(all, _) => {
                let differs = (sol.iter().zip(&all).zip(outs))
                    .any(|((t, a), out)| *out && t.as_ref().is_some_and(|t| !agrees(t, a)));
                if differs {
                    self.errors.push(Error::new(
                        call.pos,
                        format!(
                            "the types given for `{}` are not those its implementation for \
                             {} has",
                            defs.file.traits[k].name,
                            defs.types_of(k, &types, body.scope)
                        ),
                    ));
                    return Ty::Unknown;
                }
                let bits = k == defs.bits;
                let function = defs.file.traits[k].functions[fi].name.as_str();
                let shifts = bits && function.starts_with("shift_");
                if let (true, Ty::Int(t)) = (bits, &types[0]) {
                    body.facts.note_bit_call(call, sig, function, *t);
                }
                if shifts && self.errors.len() == mistakes {
                    self.shift(body, call, sig, &types[0]);
                }
                let mut all = all;
                all.extend(own);
                sig.gives.subst(&all)
            }
            Found::No => {
                self.errors
                    .push(self.no_implementation(body, call, k, &types));
                Ty::Unknown
            }
            Found::Two(a, b) => {
                self.errors.push(defs.ambiguous(call.pos, a, b));
                Ty::Unknown
            }
            Found::Unknown => Ty::Unknown,
        }
    }

    /// Reports the types a call gives the type parameters of the trait's
    /// function that lack what a bound of the parameter asks.
    fn own_bounds(
        &mut self,
        body: &Body,
        call: &Call,
        sig: &Signature,
        n: usize,
        sol: &[Option<Ty>],
    ) {
        let defs = self.defs;
        for r in sig.scope.requires.iter().filter(|r| {
            r.types
                .iter()
                .any(|t| (n..sol.len()).any(|p| t.mentions(p)))
        }) {
            let types: Vec<Ty> = r.types.iter().map(|t| t.solved(sol)).collect();
            if types.contains(&Ty::Unknown) {
                continue;
            }
            match defs.meets_types(r.of, &types, body.scope, &mut Vec::new()) {
                Found::No => {
                    let because = match types.as_slice() {
                        [t] => defs.because(r.of, t, body.scope),
                        _ => String::new(),
                    };
                    self.errors.push(Error::new(
                        call.pos,
                        format!(
                            "there is no implementation of `{}` for {}, which `{}` needs{because}",
                            defs.file.traits[r.of].name,
                            defs.types_of(r.of, &types, body.scope),
                            call.callee
                        ),
                    ));
                }
                Found::Two(a, b) => self.errors.push(defs.ambiguous(call.pos, a, b)),
                Found::Yes(..) | Found::Unknown => {}
            }
        }
    }

    /// The error for a call of a function of trait `k` whose types no
    /// implementation is for.
    pub(super) fn no_implementation(
        &self,
        body: &Body,
        call: &Call,
        k: usize,
        types: &[Ty],
    ) -> Error {
        let defs = self.defs;
        // Only a trait of one type is given automatically, so only then
        // is a part to blame.
        let because = match types {
            [t] => defs.because(k, t, body.scope),
            [a, Ty::Enum(e, args)] if k == defs.convert() => {
                let (a, to) = (defs.text(a, body.scope), defs.text(&types[1], body.scope));
                let (a, to) = (format!("`{a}`"), format!("`{to}`"));
                let holding = defs.holding(*e, args, &types[0]);
                if defs.withouts.contains(&(k, Def::Enum(*e))) {
                    format!(
                        ", as `without Convert for {}` takes it away",
                        defs.file.enums[*e].name
                    )
                } else if holding.is_empty() {
                    format!(", as no case of {to} holds {a}")
                } else {
                    let cases: Vec<String> = holding.iter().map(|c| format!("`{c}`")).collect();
                    format!(
                        ", as the cases {} of {to} each hold {a}",
                        cases.join(" and ")
                    )
                }
            }
            _ => String::new(),
        };
        Error::new(
            call.pos,
            format!(
                "there is no implementation of `{}` for {}{because}",
                defs.file.traits[k].name,
                defs.types_of(k, types, body.scope)
            ),
        )
    }
}

impl Typed<'_> {
    /// The types of a call of a trait's function, `types` being those its
    /// arguments settle, with the ones that only its result names settled by
    /// the type the call was checked to give.
    pub(super) fn result_types(
        &self,
        call: &Call,
        sig: &Signature,
        env: &[Ty],
        types: Vec<Ty>,
    ) -> Vec<Ty> {
        let Some(result) = self.types.get(&addr(call)) else {
            return types;
        };
        let mut sol: Vec<Option<Ty>> = (types.into_iter())
            .map(|t| (t != Ty::Unknown).then_some(t))
            .collect();
        unify(&sig.gives, &result.subst(env), &mut sol);
        sol.into_iter().map(|t| t.unwrap_or(Ty::Unknown)).collect()
    }
}
