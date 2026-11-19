//! How a module's names are written in the file that holds every module: the
//! names a module does not list are written with the module after them, and
//! the tokens of a module are rewritten to say so.

use std::collections::{HashMap, HashSet};

/// The tokens with each name of `rename` written as the name it maps to. A
/// name is not written anew where it is the name of a field or of an argument
/// given by name, which another module writes as the module that defines it
/// did, nor where it follows a `.`. A parameter is written anew with its uses.
pub(super) fn renamed_tokens(
    tokens: &[crate::lexer::Token],
    rename: &dyn Fn(&str) -> Option<String>,
    through: &HashMap<String, HashMap<String, String>>,
) -> Vec<crate::lexer::Token> {
    use crate::lexer::{Piece, Tok, Token};
    // The depth of parentheses inside the parameters of a function, which is 0
    // outside them, and whether the next `(` opens them.
    let mut params = 0usize;
    let mut signature = false;
    let mut out: Vec<Token> = Vec::with_capacity(tokens.len());
    // How many tokens after this one a name written through its module takes.
    let mut skip = 0usize;
    // Whether an `enum` was read and its `{` is not, and how many braces
    // deep inside the body of an enum the tokens are, which is 0 outside it.
    let mut enum_next = false;
    let mut enum_depth = 0usize;
    // The parameters of a function whose body is being read, with how many
    // braces deep the body is, one entry for each function that holds the
    // tokens, and the parameters of the one whose signature is being read, and
    // whether its parameters have ended.
    let mut bodies: Vec<(Vec<String>, usize)> = Vec::new();
    let mut signed: Option<Vec<String>> = None;
    let mut parameters_ended = false;
    let mut brace = 0usize;
    for (i, t) in tokens.iter().enumerate() {
        if skip > 0 {
            skip -= 1;
            continue;
        }
        // A case that holds nothing is written alone on its line in the body of
        // its enum, where it is the name the enum gives it, whatever another
        // item of the module is called, as it is after a `.` where it is used.
        let case_defined = enum_depth == 1
            && matches!(
                out.last(),
                Some(Token {
                    tok: Tok::Punct("{") | Tok::Newline,
                    ..
                })
            );
        match &t.tok {
            Tok::Keyword("enum") => enum_next = true,
            Tok::Punct("{") if enum_next => {
                enum_next = false;
                enum_depth = 1;
            }
            Tok::Punct("{") if enum_depth > 0 => enum_depth += 1,
            Tok::Punct("}") if enum_depth > 0 => enum_depth -= 1,
            _ => {}
        }
        // A parameter is its own name in its function, whatever an item of the
        // module is called, as it is where a call names it.
        let declared = params == 1
            && signed.is_some()
            && matches!(&t.tok, Tok::Ident(_))
            && matches!(
                tokens.get(i + 1),
                Some(Token {
                    tok: Tok::Punct(":"),
                    ..
                })
            );
        // The name of the result, which the clauses after it may write.
        let result_named = parameters_ended
            && signed.is_some()
            && matches!(&t.tok, Tok::Ident(_))
            && matches!(
                out.last(),
                Some(Token {
                    tok: Tok::Punct("->"),
                    ..
                })
            )
            && matches!(
                tokens.get(i + 1),
                Some(Token {
                    tok: Tok::Punct(":"),
                    ..
                })
            );
        match &t.tok {
            Tok::Keyword("fn") => {
                signed = Some(Vec::new());
                parameters_ended = false;
            }
            Tok::Keyword("struct" | "enum" | "trait" | "impl") => signed = None,
            Tok::Ident(name) if declared || result_named => {
                if let Some(names) = &mut signed {
                    names.push(name.clone());
                }
            }
            Tok::Punct(")") if params == 1 && signed.is_some() => parameters_ended = true,
            Tok::Punct("{") => {
                brace += 1;
                if parameters_ended && let Some(names) = signed.take() {
                    bodies.push((names, brace));
                }
            }
            Tok::Punct("}") => {
                while bodies.last().is_some_and(|(_, depth)| *depth == brace) {
                    bodies.pop();
                }
                brace = brace.saturating_sub(1);
                signed = None;
            }
            _ => {}
        }
        match &t.tok {
            Tok::Keyword("fn") => signature = true,
            Tok::Punct("(") if params > 0 => params += 1,
            Tok::Punct("(") if signature => {
                signature = false;
                params = 1;
            }
            Tok::Punct(")") if params > 0 => params -= 1,
            _ => {}
        }
        let tok = match &t.tok {
            Tok::Ident(name) => {
                let after_dot = matches!(
                    out.last(),
                    Some(Token {
                        tok: Tok::Punct("."),
                        ..
                    })
                );
                let labelled = matches!(
                    tokens.get(i + 1),
                    Some(Token {
                        tok: Tok::Punct(":"),
                        ..
                    })
                ) && params != 1;
                // A module imported itself, written before a name of its own.
                let member = match (tokens.get(i + 1), tokens.get(i + 2)) {
                    (
                        Some(Token {
                            tok: Tok::Punct("."),
                            ..
                        }),
                        Some(Token {
                            tok: Tok::Ident(member),
                            ..
                        }),
                    ) if !after_dot => through.get(name).and_then(|names| names.get(member)),
                    _ => None,
                };
                // Between the `)` of the parameters and the `{` of the body, the
                // clauses that name them are in the signature still.
                let is_parameter = declared
                    || result_named
                    || (parameters_ended
                        && signed
                            .as_ref()
                            .is_some_and(|names| names.iter().any(|n| n == name)))
                    || bodies
                        .iter()
                        .any(|(names, _)| names.iter().any(|n| n == name));
                match rename(name) {
                    _ if member.is_some() => {
                        skip = 2;
                        Tok::Ident(member.cloned().unwrap_or_default())
                    }
                    Some(to) if !after_dot && !labelled && !case_defined && !is_parameter => {
                        Tok::Ident(to)
                    }
                    _ => t.tok.clone(),
                }
            }
            Tok::Interp(pieces) => Tok::Interp(
                pieces
                    .iter()
                    .map(|p| match p {
                        Piece::Expr(inner) => Piece::Expr(renamed_tokens(inner, rename, through)),
                        text => text.clone(),
                    })
                    .collect(),
            ),
            other => other.clone(),
        };
        out.push(Token { tok, pos: t.pos });
    }
    out
}

/// What a name a module defines is written as in the file: as the file that
/// is built names it with `as`, as it is when the file lists it or the
/// compiler builds it, or with the module after it.
pub(super) fn own_final<S: std::hash::BuildHasher>(
    module: &str,
    aliased: &[(String, String)],
    kept: &HashSet<String, S>,
    name: &str,
) -> String {
    match aliased.iter().find(|(from, _)| from == name) {
        Some((_, alias)) => alias.clone(),
        None if kept.contains(name) => name.to_string(),
        None => format!("{name}@{module}"),
    }
}

/// What one module of the file calls what: the module's name, the names it
/// writes as they are, the names the file that is built gives its names with
/// `as`, and what it lists from other modules. The module that is read whole
/// and the one that is read item by item both answer from it, so that a name
/// is written one way only.
pub(super) struct ModuleScope<'s, 'k> {
    /// The module's name.
    pub(super) name: &'s str,
    /// Whether the module is of the standard library, whose names that start
    /// with `__` are the compiler's and are not renamed.
    pub(super) standard: bool,
    /// Whether the module writes a name as it is.
    pub(super) kept: &'k dyn Fn(&str) -> bool,
    /// The names the file that is built gives the module's names with `as`.
    pub(super) aliased: &'s [(String, String)],
    /// What the module lists from other modules, as it writes each and as the
    /// file writes it.
    pub(super) imports: &'s [(String, String)],
}

impl<'s, 'k> ModuleScope<'s, 'k> {
    /// What a name the module defines is written as in the file: as the file
    /// that is built names it with `as`, as it is when the module writes it so
    /// or the compiler builds it, or with the module after it.
    pub(super) fn final_name(&self, name: &str) -> String {
        match self.aliased.iter().find(|(from, _)| from == name) {
            Some((_, alias)) => alias.clone(),
            None if (self.kept)(name) => name.to_string(),
            None => format!("{name}@{}", self.name),
        }
    }

    /// What the name the module writes is written as in the file, when it is
    /// not as the module writes it. `defined` says whether the module defines
    /// the name.
    pub(super) fn rename(&self, name: &str, defined: bool) -> Option<String> {
        if let Some((_, to)) = self.imports.iter().find(|(from, _)| from == name) {
            return Some(to.clone());
        }
        if !defined || (self.standard && name.starts_with("__")) {
            return None;
        }
        let to = self.final_name(name);
        (to != name).then_some(to)
    }

    /// The names, as the module writes them, that the name as the file writes
    /// it may be, among those the module defines, and among those it lists
    /// from other modules when `listed` says so.
    pub(super) fn originals<'n>(&self, written: &'n str, listed: bool) -> Vec<&'n str>
    where
        's: 'n,
    {
        let mut found: Vec<&str> = Vec::new();
        if let Some((name, module)) = written.split_once('@') {
            if module == self.name {
                found.push(name);
            }
        } else if (self.kept)(written) {
            found.push(written);
        }
        found.extend(
            (self.aliased.iter())
                .filter(|(_, alias)| alias == written)
                .map(|(from, _)| from.as_str()),
        );
        if listed {
            found.extend(
                (self.imports.iter())
                    .filter(|(_, to)| to == written)
                    .map(|(from, _)| from.as_str()),
            );
        }
        found
    }
}
