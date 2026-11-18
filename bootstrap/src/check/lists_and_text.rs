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

impl<'a> Checker<'a> {
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
            true => Want::Ty(Ty::Int(Int::U64)),
            false => Want::Any,
        };
        let at = self.expr(body, index, &wanted);
        match &at {
            Ty::Int(t) if t.signed() => {
                self.errors.push(Error::new(
                    index.pos(),
                    "an index is a whole number of an unsigned type",
                ));
            }
            Ty::Int(_) | Ty::Unknown => {}
            Ty::Flex(_) => self.expect(body, index, &at, &Ty::Int(Int::U64)),
            other => {
                let given = defs.describe(other, body.scope);
                self.errors.push(Error::new(
                    index.pos(),
                    format!("an index is a whole number, and this is {given}"),
                ));
            }
        }
        match list {
            Ty::List(element) => {
                self.in_range(body, of, index, pos);
                element[0].clone()
            }
            Ty::Unknown => Ty::Unknown,
            other => {
                let given = defs.describe(&other, body.scope);
                self.errors.push(Error::new(
                    pos,
                    format!("an index is read from a list, and this is {given}"),
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
            self.errors.push(Error::new(
                pos,
                format!("a default carries no effect, and {needs} needs the `alloc` effect"),
            ));
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
