//! Checks lists and text: `push`, a list written out, an index, and a string
//! that holds expressions.

use super::*;

/// Whether a call is of `push`, which the compiler builds, and no function
/// of the program takes the name of.
pub(super) fn is_push(defs: &Defs, call: &Call) -> bool {
    call.qualifier.is_none()
        && call.callee == "push"
        && !defs.function_names.contains_key("push")
        && !defs.struct_names.contains_key("push")
}

/// Whether a call is of `utf8`, which the compiler builds, and no function
/// of the program takes the name of.
pub(super) fn is_utf8(defs: &Defs, call: &Call) -> bool {
    call.qualifier.is_none()
        && call.callee == "utf8"
        && !defs.function_names.contains_key("utf8")
        && !defs.struct_names.contains_key("utf8")
}

/// Whether a text or a slice is given where a list is needed, which it is
/// used as, with no call and no copy.
pub(super) fn as_list(given: &Ty, needed: &Ty) -> bool {
    match (given, needed) {
        (Ty::Text, _) => *needed == Ty::list(Ty::Int(Int::U8)),
        (Ty::Slice(of), Ty::List(wanted)) => of == wanted,
        _ => false,
    }
}

/// The type a value of the type `given` has where a `pattern` is needed: a
/// text where a list is needed is the list of bytes it is used as, and a
/// slice is the list of its elements.
pub(super) fn as_read(given: &Ty, pattern: &Ty) -> Ty {
    match (given, pattern) {
        (Ty::Text, Ty::List(_)) => Ty::list(Ty::Int(Int::U8)),
        (Ty::Slice(of), Ty::List(_)) => Ty::list(of[0].clone()),
        _ => given.clone(),
    }
}

/// Whether a call is of `freeze`, which the compiler builds, and no function
/// of the program takes the name of.
pub(super) fn is_freeze(defs: &Defs, call: &Call) -> bool {
    call.qualifier.is_none()
        && call.callee == "freeze"
        && !defs.function_names.contains_key("freeze")
        && !defs.struct_names.contains_key("freeze")
}

impl<'a> Checker<'a> {
    /// Checks `utf8(bytes)`, which gives `.ok` of the text that is the same
    /// bytes, or `.err` with the place of the first byte that is not valid.
    pub(super) fn utf8(&mut self, body: &mut Body<'a>, call: &Call) -> Ty {
        let defs = self.defs;
        let plain = call.args.iter().all(|a| a.name.is_none() && !a.after);
        if call.args.len() != 1 || !plain {
            self.errors.push(Error::new(
                call.pos,
                "`utf8` takes the one slice of bytes to read",
            ));
            return self.unknown_call(body, call);
        }
        let bytes = Ty::Slice(vec![Ty::Int(Int::U8)]);
        let value = &call.args[0].value;
        let given = self.expr(body, value, &Want::Ty(bytes.clone()));
        if given != Ty::Text {
            self.expect(body, value, &given, &bytes);
        }
        let result = defs.enum_names["Result"];
        let error = defs.struct_names["Utf8Error"];
        Ty::Enum(result, vec![Ty::Text, Ty::Struct(error, Vec::new())])
    }

    /// Checks `freeze(list)`, which gives the memory of the list as a slice,
    /// with nothing copied.
    pub(super) fn freeze(&mut self, body: &mut Body<'a>, call: &Call, want: &Want) -> Ty {
        let defs = self.defs;
        let plain = call.args.iter().all(|a| a.name.is_none() && !a.after);
        if call.args.len() != 1 || !plain {
            self.errors.push(Error::new(
                call.pos,
                "`freeze` takes the one list to freeze",
            ));
            return self.unknown_call(body, call);
        }
        let list_want = match want {
            Want::Ty(Ty::Slice(of)) => Want::Ty(Ty::List(of.clone())),
            _ => Want::Any,
        };
        match self.expr(body, &call.args[0].value, &list_want) {
            Ty::List(of) => Ty::Slice(of),
            Ty::Unknown => Ty::Unknown,
            other => {
                let given = defs.describe(&other, body.scope);
                self.errors.push(Error::new(
                    call.args[0].value.pos(),
                    format!("`freeze` takes a list, and this is {given}"),
                ));
                Ty::Unknown
            }
        }
    }

    /// Checks `of[a..b]`, `of[..b]`, or `of[a..]`, the part of a slice from
    /// `a` up to `b`, which shares its memory, and compiles only when `arith`
    /// proves that `a <= b` and `b <= of.size`.
    pub(super) fn piece(
        &mut self,
        body: &mut Body<'a>,
        of: &Expr,
        ends: (Option<&Expr>, Option<&Expr>),
        pos: Pos,
    ) -> Ty {
        let defs = self.defs;
        let slice = self.expr(body, of, &Want::Any);
        for end in [ends.0, ends.1].into_iter().flatten() {
            let wanted = match flexible(end) {
                true => Want::Ty(Ty::Int(Int::Nat)),
                false => Want::Any,
            };
            match self.expr(body, end, &wanted) {
                Ty::Int(Int::Nat) | Ty::Unknown => {}
                Ty::Int(t) => {
                    self.errors.push(
                        Error::new(
                            end.pos(),
                            format!(
                                "an end of a range is a `Nat`, and this is a `{}`",
                                self.defs.spelling(t)
                            ),
                        )
                        .with_fix("use `convert`"),
                    );
                }
                at @ Ty::Flex(_) => self.expect(body, end, &at, &Ty::Int(Int::Nat)),
                other => {
                    let given = defs.describe(&other, body.scope);
                    self.errors.push(Error::new(
                        end.pos(),
                        format!("an end of a range is a whole number, and this is {given}"),
                    ));
                }
            }
        }
        match slice {
            Ty::Slice(_) => {
                self.in_piece(body, of, ends, pos);
                slice
            }
            Ty::List(_) | Ty::Text => {
                self.errors
                    .push(Error::unsupported(pos, "a piece of a list or a text"));
                Ty::Unknown
            }
            Ty::Unknown => Ty::Unknown,
            other => {
                let given = defs.describe(&other, body.scope);
                self.errors.push(Error::new(
                    pos,
                    format!("a piece is taken from a slice, and this is {given}"),
                ));
                Ty::Unknown
            }
        }
    }

    /// Checks `push(list, item)`, which gives the list with the item after
    /// its last element, and asks the system for memory.
    pub(super) fn push(&mut self, body: &mut Body<'a>, call: &Call, want: &Want) -> Ty {
        let defs = self.defs;
        self.needs(body, "alloc", call.pos, "`push`");
        let plain = call.args.iter().all(|a| a.name.is_none() && !a.after);
        if call.args.len() != 2 || !plain {
            self.errors.push(Error::new(
                call.pos,
                "`push` takes a list and the item to add after it",
            ));
            return self.unknown_call(body, call);
        }
        let list_want = match want {
            Want::Ty(t @ Ty::List(_)) => Want::Ty(t.clone()),
            _ => Want::Any,
        };
        let list = self.expr(body, &call.args[0].value, &list_want);
        match list {
            Ty::Slice(_) => {
                self.expr(body, &call.args[1].value, &Want::Ty(Ty::Unknown));
                self.errors.push(
                    Error::new(
                        call.args[0].value.pos(),
                        "`push` adds to a list, and this is a slice, which cannot change",
                    )
                    .with_fix("write `copy(s)` of the slice, which is a new list"),
                );
                Ty::Unknown
            }
            Ty::List(of) => {
                let item = self.expr(body, &call.args[1].value, &Want::Ty(of[0].clone()));
                self.expect(body, &call.args[1].value, &item, &of[0]);
                Ty::List(of)
            }
            other => {
                self.expr(body, &call.args[1].value, &Want::Ty(Ty::Unknown));
                if other != Ty::Unknown {
                    let given = defs.describe(&other, body.scope);
                    self.errors.push(Error::new(
                        call.args[0].value.pos(),
                        format!("`push` adds to a list, and this is {given}"),
                    ));
                }
                Ty::Unknown
            }
        }
    }

    /// Checks a list written out, `[]` or `[a, b, c]`, where a list of
    /// known element type is wanted.
    pub(super) fn list_literal(
        &mut self,
        body: &mut Body<'a>,
        pos: Pos,
        items: &[Expr],
        want: &Want,
    ) -> Ty {
        let defs = self.defs;
        match want {
            Want::Ty(Ty::List(of)) => {
                if !items.is_empty() {
                    self.needs(body, "alloc", pos, "a list that holds elements");
                }
                for item in items {
                    let given = self.expr(body, item, &Want::Ty(of[0].clone()));
                    self.expect(body, item, &given, &of[0]);
                }
                Ty::List(of.clone())
            }
            Want::Ty(slice @ Ty::Slice(of)) => {
                if !matches!(of[0], Ty::Int(_)) {
                    for item in items {
                        self.expr(body, item, &Want::Ty(Ty::Unknown));
                    }
                    self.errors.push(Error::unsupported(
                        pos,
                        "a slice written out whose elements are not whole numbers",
                    ));
                    return Ty::Unknown;
                }
                for item in items {
                    let given = self.expr(body, item, &Want::Ty(of[0].clone()));
                    self.expect(body, item, &given, &of[0]);
                    if !matches!(item, Expr::Number { .. }) {
                        self.errors.push(Error::unsupported(
                            item.pos(),
                            "a slice written out with an element that is not a number written out",
                        ));
                    }
                }
                slice.clone()
            }
            Want::Ty(Ty::Unknown) => {
                for item in items {
                    self.expr(body, item, &Want::Ty(Ty::Unknown));
                }
                Ty::Unknown
            }
            Want::Ty(needed) => {
                for item in items {
                    self.expr(body, item, &Want::Ty(Ty::Unknown));
                }
                let needed = defs.describe(needed, body.scope);
                self.errors.push(Error::new(
                    pos,
                    format!("this is a list, where {needed} is needed"),
                ));
                Ty::Unknown
            }
            Want::Any | Want::Unused => {
                for item in items {
                    self.expr(body, item, &Want::Ty(Ty::Unknown));
                }
                self.errors.push(
                    Error::new(
                        pos,
                        "a list is written where a list of a known type is needed",
                    )
                    .with_fix("write its type, as in `xs: List[u8] = [1, 2, 3]`"),
                );
                Ty::Unknown
            }
        }
    }

    /// Checks `of[index]`, which reads an element of a list, and compiles
    /// only when `arith` proves that the index is below the list's size.
    pub(super) fn index(&mut self, body: &mut Body<'a>, of: &Expr, index: &Expr, pos: Pos) -> Ty {
        let defs = self.defs;
        let list = self.expr(body, of, &Want::Any);
        let wanted = match flexible(index) {
            true => Want::Ty(Ty::Int(Int::Nat)),
            false => Want::Any,
        };
        let at = self.expr(body, index, &wanted);
        match &at {
            Ty::Int(Int::Nat) | Ty::Unknown => {}
            Ty::Int(t) => {
                self.errors.push(
                    Error::new(
                        index.pos(),
                        format!(
                            "an index is a `Nat`, and this is a `{}`",
                            self.defs.spelling(*t)
                        ),
                    )
                    .with_fix("use `convert`"),
                );
            }
            Ty::Flex(_) => self.expect(body, index, &at, &Ty::Int(Int::Nat)),
            other => {
                let given = defs.describe(other, body.scope);
                self.errors.push(Error::new(
                    index.pos(),
                    format!("an index is a whole number, and this is {given}"),
                ));
            }
        }
        match list {
            Ty::List(element) | Ty::Slice(element) => {
                self.in_range(body, of, index, pos);
                element[0].clone()
            }
            Ty::Text => {
                self.in_range(body, of, index, pos);
                Ty::Int(Int::U8)
            }
            Ty::Unknown => Ty::Unknown,
            other => {
                let given = defs.describe(&other, body.scope);
                self.errors.push(Error::new(
                    pos,
                    format!(
                        "an index is read from a list, a slice, or a text, and this is {given}"
                    ),
                ));
                Ty::Unknown
            }
        }
    }

    /// Checks a string that holds expressions, which asks the system for
    /// memory, and checks each expression in it as the text it is written as.
    pub(super) fn interpolation(&mut self, body: &mut Body<'a>, pos: Pos, parts: &[Part]) -> Ty {
        let needs = "a string that holds an expression";
        if body.default.is_some() {
            self.needs(body, "alloc", pos, needs);
            for part in parts {
                if let Part::Expr(Expr::Call(call)) = part {
                    self.unknown_call(body, call);
                }
            }
            return Ty::Unknown;
        }
        self.needs(body, "alloc", pos, needs);
        for part in parts {
            if let Part::Expr(e) = part {
                self.expr(body, e, &Want::Ty(Ty::Text));
            }
        }
        Ty::Text
    }
}
