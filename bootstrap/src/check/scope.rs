//! The names a file writes that are not its own: those that are not bound
//! inside it, so that they can only mean a name of the prelude or of
//! another module.

use std::collections::HashSet;

use super::{exprs, statements, values};
use crate::diag::Pos;
use crate::parser::{Expr, File, Function, Generic, Pattern, Stmt};

/// Each name the file writes, with where, that no function, parameter,
/// binding, pattern, loop, lambda, type parameter, field, or case of the file
/// gives.
pub(super) fn used_names(file: &File) -> Vec<(String, Pos)> {
    if file.idents.is_empty() {
        return Vec::new();
    }
    let mut bound: HashSet<&str> = HashSet::new();
    let mut functions: Vec<&Function> = file.functions.iter().map(|f| &**f).collect();
    for t in &file.traits {
        names_of(&t.generics, &mut bound);
        functions.extend(&t.functions);
    }
    for im in &file.impls {
        names_of(&im.generics, &mut bound);
        functions.extend(&im.functions);
    }
    for s in &file.structs {
        names_of(&s.generics, &mut bound);
        bound.extend(s.fields.iter().map(|f| f.name.as_str()));
    }
    for e in &file.enums {
        names_of(&e.generics, &mut bound);
        bound.extend(e.cases.iter().map(|c| c.name.as_str()));
    }
    for f in functions {
        names_of(&f.generics, &mut bound);
        bound.extend(f.params.iter().map(|p| p.name.as_str()));
        bound.extend(f.result_name.iter().map(|(n, _)| n.as_str()));
        for stmt in statements(&f.body) {
            match stmt {
                Stmt::Define { name, .. } => {
                    bound.insert(name.as_str());
                }
                Stmt::Destructure { pattern, .. } => patterns(pattern, &mut bound),
                Stmt::Assign { .. } | Stmt::Expr(_) => {}
            }
        }
        for e in exprs(values(&f.body)) {
            match e {
                Expr::Match(m) => {
                    for arm in &m.arms {
                        patterns(&arm.pattern, &mut bound);
                    }
                }
                Expr::Lambda(l) => bound.extend(l.params.iter().map(|(n, _)| n.as_str())),
                Expr::Loop(l) => {
                    bound.extend(l.range.iter().map(|r| r.name.as_str()));
                    bound.extend(l.over.iter().map(|o| o.name.as_str()));
                }
                _ => {}
            }
        }
    }
    (file.idents.iter())
        .filter(|(name, _)| !bound.contains(name.as_str()))
        .cloned()
        .collect()
}

/// The names a pattern binds.
fn patterns<'a>(pattern: &'a Pattern, into: &mut HashSet<&'a str>) {
    match pattern {
        Pattern::Bind { name, .. } => {
            into.insert(name.as_str());
        }
        Pattern::Case { inner, .. } => {
            if let Some(inner) = inner {
                patterns(inner, into);
            }
        }
        Pattern::Tuple { items, .. } => {
            for item in items {
                patterns(item, into);
            }
        }
        Pattern::Number { .. } | Pattern::Text { .. } => {}
    }
}

/// The names of type parameters.
fn names_of<'a>(list: &'a [Generic], bound: &mut HashSet<&'a str>) {
    bound.extend(list.iter().map(|g| g.name.as_str()));
}
