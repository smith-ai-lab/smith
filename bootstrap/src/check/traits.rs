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
    "Iterate",
    "FromInt",
    "FromFloat",
    "FromString",
    "FromArray",
];

/// The functions of the three traits that name how arithmetic that may not
/// fit goes on, and what an operation gives for each: the trait, the
/// function's stem, and what it gives, written with `T` for the type.
const ARITHMETIC: &[(&str, &str, &str)] = &[
    ("Wrapping", "wrapping", "T"),
    ("Saturating", "saturating", "T"),
    ("Checked", "checked", "Option[T]"),
];

/// The operations of those traits.
const OPERATIONS: &[&str] = &["add", "sub", "mul"];

/// The functions of `Bits`: the name, the parameters written with `T` for
/// the type, and the names of the parameters. A shift takes its amount as a
/// `u8`.
const BITS: &[(&str, &str, &str)] = &[
    ("bit_and", "a: T, b: T", "a, b"),
    ("bit_or", "a: T, b: T", "a, b"),
    ("bit_xor", "a: T, b: T", "a, b"),
    ("bit_not", "a: T", "a"),
    ("shift_left", "a: T, by: u8", "a, by"),
    ("shift_right", "a: T, by: u8", "a, by"),
];

/// The implementations of the prelude's traits for a whole number type,
/// which call the functions the compiler builds. Only the prelude may call
/// them, and their names start with two underscores.
pub(super) fn integer_impls(t: Int) -> String {
    let name = t.name();
    let mut out = format!(
        "impl Eq[{name}] {{
  fn eq(a: {name}, b: {name}) -> bool {{ __equal(a, b) }}
}}

impl Ord[{name}] {{
  fn compare(a: {name}, b: {name}) -> Order {{ __compare(a, b) }}
}}

impl Fmt[{name}] {{
  fn fmt(value: {name}) -> str ! alloc {{ __decimal(value) }}
}}

impl Repr[{name}] {{
  fn repr(value: {name}) -> str ! alloc {{ __decimal(value) }}
}}
"
    );
    for (tr, stem, gives) in ARITHMETIC {
        let gives = gives.replace('T', name);
        out.push_str(&format!("\nimpl {tr}[{name}] {{\n"));
        for op in OPERATIONS {
            out.push_str(&format!(
                "  fn {stem}_{op}(a: {name}, b: {name}) -> {gives} {{ __{stem}_{op}(a, b) }}\n"
            ));
        }
        out.push_str("}\n");
    }
    // It converts to each other type that holds every value of its own.
    for to in Int::ALL {
        if to != t && to.min() <= t.min() && t.max() <= to.max() {
            out.push_str(&format!(
                "\nimpl Convert[{name}, {to}] {{\n  fn convert(a: {name}) -> {to} {{ __convert(a) }}\n}}\n",
                to = to.name()
            ));
        }
    }
    out.push_str(&format!("\nimpl Bits[{name}] {{\n"));
    for (f, params, names) in BITS {
        let params = params.replace('T', name);
        out.push_str(&format!(
            "  fn {f}({params}) -> {name} {{ __{f}({names}) }}\n"
        ));
    }
    out.push_str("}\n");
    out
}

/// The traits the language defines whose functions the prelude names for
/// every whole number type.
pub(super) fn integer_traits() -> String {
    let mut out = String::new();
    for (tr, stem, gives) in ARITHMETIC {
        out.push_str(&format!("trait {tr}[T] {{\n"));
        for op in OPERATIONS {
            let gives = if *gives == "T" { "T" } else { "Option[T]" };
            out.push_str(&format!("  fn {stem}_{op}(a: T, b: T) -> {gives}\n"));
        }
        out.push_str("}\n\n");
    }
    out.push_str("trait Bits[T] {\n");
    for (f, params, _) in BITS {
        out.push_str(&format!("  fn {f}({params}) -> T\n"));
    }
    out.push_str("}\n\n");
    out
}

/// The names of the traits `PRELUDE` defines.
pub(super) const PRELUDE_TRAITS: &[&str] = &[
    "Fmt",
    "Repr",
    "Exit",
    "Try",
    "Eq",
    "Ord",
    "Wrapping",
    "Saturating",
    "Checked",
    "Bits",
    "Convert",
];

/// The place of the prelude's trait with the name among the file's.
pub(super) fn prelude_trait(file: &File, name: &str) -> usize {
    (file.traits.iter())
        .position(|t| t.name == name && of_prelude(t.pos))
        .expect("the prelude defines it")
}

/// Moves the `requires` lines that name a trait from the contracts of each
/// function to what it needs. Parsing does not know what a name refers to,
/// so a line that is `Name[...]` is one that names a trait here.
pub(super) fn split_needs(mut file: File) -> File {
    let traits: Vec<String> = file.traits.iter().map(|t| t.name.clone()).collect();
    let names_trait = |e: &Expr| {
        matches!(e, Expr::Index { of, .. }
            if matches!(&**of, Expr::Name { name, .. }
                if traits.contains(name) || TRAITS_NOT_BUILT.contains(&name.as_str())))
    };
    for f in &mut file.functions {
        let (needs, requires) = std::mem::take(&mut f.requires)
            .into_iter()
            .partition(|e| names_trait(e));
        f.needs = needs;
        f.requires = requires;
    }
    file
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
            })
        }
        Expr::Tuple { pos, items } => Some(Type {
            name: String::new(),
            pos: *pos,
            args: items.iter().map(expr_type).collect::<Option<_>>()?,
            function: false,
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
        let k = match defs.trait_names.get(name) {
            Some(&k) if k == defs.fmt || k == defs.repr || k == defs.eq || k == defs.convert() => k,
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
        let d = if let Some(&s) = defs.struct_names.get(ty) {
            Def::Struct(s)
        } else if let Some(&e) = defs.enum_names.get(ty) {
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
            } else if defs.trait_names.contains_key(ty) {
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
    let &of = defs.trait_names.get(im.name.as_str())?;
    let t = &defs.file.traits[of];
    if im.args.len() != t.generics.len() {
        return None;
    }
    let scope = defs.scope(&im.generics, &im.requires);
    let args: Vec<Ty> = im.args.iter().map(|a| defs.quietly(a, &scope)).collect();
    if args.contains(&Ty::Unknown) {
        return None;
    }
    // Each type parameter must be chosen by the types that are not `out`.
    let chosen = (0..scope.names.len())
        .all(|p| (args.iter().zip(&defs.outs[of])).any(|(a, out)| !out && a.mentions(p)));
    let fns = (t.functions.iter())
        .map(|f| im.functions.iter().position(|g| g.name == f.name))
        .collect();
    Some(ImplInfo {
        of,
        scope,
        args,
        fns,
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

/// The implementations whose types name `name`, which live with that type.
pub(super) fn impls_for(file: &File, name: &str) -> Vec<Item> {
    (file.impls.iter().enumerate())
        .filter(|(_, im)| im.args.iter().flat_map(type_names).any(|(n, _)| n == name))
        .map(|(i, _)| Item::Impl(i))
        .collect()
}

/// The names the `requires` lines that name a trait use: the trait, and the
/// types it is given.
pub(super) fn needs_named(needs: &[Expr]) -> Vec<&str> {
    fn walk<'a>(e: &'a Expr, out: &mut Vec<&'a str>) {
        match e {
            Expr::Name { name, .. } => out.push(name),
            Expr::Index { of, index, .. } => {
                walk(of, out);
                walk(index, out);
            }
            Expr::Tuple { items, .. } => items.iter().for_each(|i| walk(i, out)),
            _ => {}
        }
    }
    let mut out = Vec::new();
    needs.iter().for_each(|e| walk(e, &mut out));
    out
}

/// The names the bounds of type parameters use.
pub(super) fn bounds_named(generics: &[Generic]) -> Vec<&str> {
    (generics.iter())
        .filter_map(|g| g.bound.as_ref())
        .flat_map(type_names)
        .map(|(name, _)| name)
        .collect()
}

/// The type with each of its type parameters moved by `by` places.
pub(super) fn shifted(t: &Ty, by: usize) -> Ty {
    match t {
        Ty::Param(i) => Ty::Param(i + by),
        t => t.with_args(t.args().iter().map(|t| shifted(t, by)).collect()),
    }
}

/// The traits the prelude defines, and their implementations, written in Smith.
/// They follow `PRELUDE_ENUMS`, and are read at line 0 with it.
pub(super) const PRELUDE: &str = "\
trait Eq[T] {
  fn eq(a: T, b: T) -> bool
}

trait Ord[T] {
  fn compare(a: T, b: T) -> Order
}

trait Convert[A, B] {
  fn convert(a: A) -> B
}

trait Fmt[T] {
  fn fmt(value: T) -> str ! alloc
}

trait Repr[T] {
  fn repr(value: T) -> str ! alloc
}

impl Fmt[str] {
  fn fmt(value: str) -> str ! alloc { value }
}

trait Exit[T] {
  fn exit(value: T) -> u8 ! console, alloc
}

impl Exit[()] {
  fn exit(value: ()) -> u8 ! console, alloc { 0 }
}

impl[T: Exit, E: Fmt] Exit[Result[T, E]] {
  fn exit(value: Result[T, E]) -> u8 ! console, alloc {
    match value {
      .ok(x) { Exit.exit(x) }
      .err(e) {
        __eprint(fmt(e))
        1
      }
    }
  }
}

trait Try[C, R, out T] {
  fn then(c: C, body: (T) -> R) -> R
}

impl[T, U, E] Try[Result[T, E], Result[U, E], T] {
  fn then(c: Result[T, E], body: (T) -> Result[U, E]) -> Result[U, E] {
    match c {
      .ok(x) { body(x) }
      .err(e) { .err(e) }
    }
  }
}

impl[T, U] Try[Option[T], Option[U], T] {
  fn then(c: Option[T], body: (T) -> Option[U]) -> Option[U] {
    match c {
      .some(x) { body(x) }
      .none { .none }
    }
  }
}
";

impl<'a> Defs<'a> {
    /// What a bound requires. Mistakes go in `errors`.
    pub(super) fn bound(&self, bound: &Option<Type>, errors: &mut Vec<Error>) -> Bound {
        let Some(t) = bound else {
            return Bound::None;
        };
        let name = t.name.as_str();
        let error = if !t.args.is_empty() {
            Error::unsupported(t.args[0].pos, "a bound with type arguments")
        } else if let Some(&k) = self.trait_names.get(name) {
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
        };
        for (p, g) in generics.iter().enumerate() {
            match self.bound(&g.bound, &mut Vec::new()) {
                Bound::None => {}
                Bound::Trait(of) => scope.requires.push(Requirement {
                    of,
                    types: vec![Ty::Param(p); self.outs[of].len()],
                }),
                Bound::Invalid => scope.invalid[p] = true,
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

    /// What each `requires` line that names a trait requires, among the
    /// type parameters of `scope`. A line that is a mistake gives `None`,
    /// and goes in `errors`.
    pub(super) fn requirements(
        &self,
        needs: &[Expr],
        scope: &Scope,
        errors: &mut Vec<Error>,
    ) -> Vec<Option<Requirement>> {
        needs
            .iter()
            .map(|e| {
                let Expr::Index { of, index, .. } = e else {
                    errors.push(Error::unsupported(e.pos(), "a `requires` of this form"));
                    return None;
                };
                let Expr::Name { name, pos } = &**of else {
                    errors.push(Error::unsupported(e.pos(), "a `requires` of this form"));
                    return None;
                };
                let Some(&of) = self.trait_names.get(name.as_str()) else {
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

    fn find_impl(
        &self,
        k: usize,
        types: &[Ty],
        scope: &Scope,
        stack: &mut Vec<(usize, Vec<Ty>)>,
    ) -> Found {
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
    /// of a tuple. A `str` is given `Repr` and `Eq`, and `()` each of the
    /// three, which have no parts.
    fn given_parts(&self, k: usize, types: &[Ty]) -> Option<Vec<Ty>> {
        if k == self.convert() {
            let Ty::Enum(e, args) = &types[1] else {
                return None;
            };
            return (!self.withouts.contains(&(k, Def::Enum(*e)))
                && self.holding(*e, args, &types[0]).len() == 1)
                .then(Vec::new);
        }
        if k != self.fmt && k != self.repr && k != self.eq {
            return None;
        }
        let taken = |d: Def| self.withouts.contains(&(k, d));
        match &types[0] {
            Ty::Text if k != self.fmt => Some(Vec::new()),
            Ty::Nothing => Some(Vec::new()),
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

    /// The trait `Convert`, which the prelude defines.
    pub(super) fn convert(&self) -> usize {
        self.trait_names["Convert"]
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
    pub(super) fn chosen(&self, k: usize, fi: usize, types: Vec<Ty>) -> (Instance, Vec<Ty>) {
        let Found::Yes(all, by) = self.find(k, &types, &Scope::default(), &mut Vec::new()) else {
            unreachable!("checked")
        };
        let instance = match by {
            By::Impl(i, args) => Instance {
                template: Template::Impl(i, fi),
                args,
            },
            By::Given => Instance {
                template: Template::Given(k),
                args: types,
            },
            By::Bound => unreachable!("a copy has no type parameter"),
        };
        (instance, all)
    }

    /// The copies a copy of a given implementation runs: for `Fmt`, the
    /// `Repr` of its type, and for `Repr` and `Eq`, that of each part.
    pub(super) fn given_calls(&self, k: usize, types: &[Ty]) -> Vec<Instance> {
        let run = |k: usize, t: &Ty| self.chosen(k, 0, vec![t.clone()]).0;
        if k == self.fmt {
            vec![run(self.repr, &types[0])]
        } else {
            let parts = self.given_parts(k, types).expect("given");
            parts.iter().map(|p| run(k, p)).collect()
        }
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
            Found::Yes(all, by) if all == types => Found::Yes(all, by),
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
            if let Some(g) = f.generics.first() {
                self.errors.push(Error::unsupported(
                    g.pos,
                    "a type parameter of a trait's function",
                ));
                continue;
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
        let Some(&k) = defs.trait_names.get(name) else {
            self.errors.push(if TRAITS_NOT_BUILT.contains(&name) {
                Error::unsupported(im.pos, &format!("implementing `{name}`"))
            } else if defs.is_type(name) {
                Error::new(im.pos, format!("`{name}` is not a trait"))
            } else {
                Error::new(im.pos, format!("there is no trait named `{name}`"))
            });
            return;
        };
        if name == "Eq" && !of_prelude(im.pos) {
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
        if im.args.len() != t.generics.len() {
            self.errors.push(Error::new(
                im.pos,
                format!(
                    "`{name}` takes {}, and this gives {}",
                    count(t.generics.len(), "type"),
                    im.args.len()
                ),
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
                    format!("`{}` is already in this implementation", f.name),
                ));
                continue;
            }
            match t.functions.iter().position(|g| g.name == f.name) {
                Some(fi) => self.implemented(i, fi, f),
                None => self.errors.push(Error::new(
                    f.pos,
                    format!("`{}` is not a function of `{name}`", f.name),
                )),
            }
        }
        let lacking: Vec<&str> = (t.functions.iter().zip(&info.fns))
            .filter(|(_, at)| at.is_none())
            .map(|(f, _)| f.name.as_str())
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
        let scope = &info.scope;
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
        let before = self.errors.len();
        self.params(f, scope);
        self.effects(&f.effects);
        if self.errors.len() > before {
            return;
        }
        let wanted: Vec<(String, Ty)> = (sig.params.iter())
            .map(|(n, t)| (n.clone(), t.subst(&info.args)))
            .collect();
        let gives = sig.gives.subst(&info.args);
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
        let same = f.generics.is_empty()
            && written == wanted
            && written_gives == gives
            && set(&f.effects) == set(&trait_fn.effects);
        if same {
            return;
        }
        let params: Vec<String> = (wanted.iter())
            .map(|(n, t)| format!("{n}: {}", defs.text(t, scope)))
            .collect();
        let mut text = format!("fn {}({})", f.name, params.join(", "));
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
        let mut sol = self.arguments(body, call, sig, false, Some((k, want)));
        let reported = std::mem::replace(&mut self.no_impl, outer);
        if reported || self.refusals != refused || self.function_for_parameter(&sol, call.pos) {
            return Ty::Unknown;
        }
        // A type that only the result names is the one that is expected.
        let outs = &defs.outs[k];
        if let Want::Ty(needed @ (Ty::Int(_) | Ty::Enum(..) | Ty::Struct(..))) = want {
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
        let types: Vec<Ty> = sol
            .iter()
            .map(|t| t.as_ref().map_or(Ty::Unknown, Ty::concrete))
            .collect();
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
                let shifts = defs.file.traits[k].name == "Bits"
                    && defs.file.traits[k].functions[fi].name.starts_with("shift_");
                if shifts && self.errors.len() == mistakes {
                    self.shift(body, call, sig, &types[0]);
                }
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

    /// Checks `__convert(a)`, which the prelude's conversions between whole
    /// numbers call, and which gives the type that is expected.
    pub(super) fn convert_intrinsic(
        &mut self,
        body: &mut Body<'a>,
        call: &Call,
        want: &Want,
    ) -> Ty {
        self.expr(body, &call.args[0].value, &Want::Any);
        match want {
            Want::Ty(t) => t.clone(),
            _ => Ty::Unknown,
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
