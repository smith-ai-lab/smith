//! Lowers numbers and operators: a number written out, `+`, `-`, and `*`, the
//! functions the compiler builds for comparing, for `wrapping_`, `saturating_`,
//! and `checked_` arithmetic, for `Bits`, and for `narrow` and its relatives, and
//! `Convert` between numbers.

use super::*;

/// How many bits a number of the type has, in the scalar form.
pub(super) fn bits_of(t: Int) -> form::Bits {
    match t.bits() {
        8 => form::Bits::B8,
        16 => form::Bits::B16,
        32 => form::Bits::B32,
        _ => form::Bits::B64,
    }
}

/// The value of a whole number literal.
pub(super) fn signed(magnitude: u128, negative: bool) -> i128 {
    let magnitude = magnitude as i128;
    if negative { -magnitude } else { magnitude }
}

impl<'a> Lowering<'a> {
    /// Lowers a whole number literal.
    pub(super) fn number(
        &mut self,
        out: &mut Out<'a>,
        expr: &'a Expr,
        (magnitude, negative): (u128, bool),
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let typed = self.typed;
        let ty = typed.type_of(expr, env);
        let Ty::Int(t) = ty else {
            unreachable!("checked")
        };
        let magnitude = magnitude as i128;
        let value = if negative { -magnitude } else { magnitude };
        (ty, vec![out.int(bits_of(t), value)])
    }

    /// Lowers `left + right`, `left - right`, or `left * right`.
    pub(super) fn operation(
        &mut self,
        out: &mut Out<'a>,
        expr: &'a Expr,
        (op, left, right): (ArithOp, &'a Expr, &'a Expr),
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let typed = self.typed;
        let ty = typed.type_of(expr, env);
        let a = self.expr(out, left, env).1[0];
        let b = self.expr(out, right, env).1[0];
        let word = match op {
            crate::parser::ArithOp::Add => form::Binary::Add,
            crate::parser::ArithOp::Sub => form::Binary::Sub,
            crate::parser::ArithOp::Mul => form::Binary::Mul,
        };
        (ty, vec![out.binary(word, a, b)])
    }

    /// Whether the call is `Eq::eq` or `Ord::compare` of two values that are
    /// whole numbers or `bool`, whose `Eq` and `Ord` no program writes, and
    /// which are therefore compared where they are.
    pub(super) fn compares_scalars(&self, call: &Call, name: &str, env: &[Ty]) -> bool {
        let typed = self.typed;
        matches!(&call.qualifier, Some((q, _)) if q == if name == "eq" { "Eq" } else { "Ord" })
            && call.callee == name
            && call.args.len() == 2
            && match typed.type_of(&call.args[0].value, env) {
                Ty::Int(_) => true,
                Ty::Enum(e, _) => e == typed.defs.boolean,
                _ => false,
            }
    }

    /// Lowers `left == right` of whole numbers or `bool`s: one comparison.
    pub(super) fn equal_scalars(
        &mut self,
        out: &mut Out<'a>,
        call: &'a Call,
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let a = self.expr(out, &call.args[0].value, env).1[0];
        let b = self.expr(out, &call.args[1].value, env).1[0];
        (
            self.typed.defs.bool_ty(),
            vec![out.compare(form::Compare::Equal, a, b)],
        )
    }

    /// Lowers a `match` that the parser wrote for `<`, `<=`, `>`, `>=`, or
    /// `!=` of whole numbers or `bool`s: one comparison, and for `<=`, `>=`,
    /// and `!=` the answer of the opposite comparison with its bit flipped,
    /// which `fold` leaves out when a block branches on it. Gives `None` for any
    /// other `match`.
    pub(super) fn comparison(
        &mut self,
        out: &mut Out<'a>,
        m: &'a Match,
        env: &[Ty],
    ) -> Option<Vec<form::Value>> {
        let Expr::Call(call) = &m.of else {
            return None;
        };
        if m.form == Form::Not {
            if !self.compares_scalars(call, "eq", env) {
                return None;
            }
            let same = self.equal_scalars(out, call, env).1[0];
            let one = out.int(form::Bits::B8, 1);
            return Some(vec![out.binary(form::Binary::Xor, same, one)]);
        }
        if m.form != Form::Match || !self.compares_scalars(call, "compare", env) {
            return None;
        }
        // What each of `less`, `equal`, and `greater` answers.
        let mut answers = [None; 3];
        for arm in &m.arms {
            let Pattern::Case {
                name, inner: None, ..
            } = &arm.pattern
            else {
                return None;
            };
            let at = ["less", "equal", "greater"]
                .iter()
                .position(|c| c == name)?;
            let [Stmt::Expr(Expr::Name { name, .. })] = &arm.body[..] else {
                return None;
            };
            answers[at] = Some(match name.as_str() {
                "true" => true,
                "false" => false,
                _ => return None,
            });
        }
        let ty = self.typed.type_of(&call.args[0].value, env);
        let a = self.expr(out, &call.args[0].value, env).1[0];
        let b = self.expr(out, &call.args[1].value, env).1[0];
        let less = match ty {
            Ty::Int(t) if t.signed() => form::Compare::LessS,
            _ => form::Compare::LessU,
        };
        // `a <= b` is not `b < a`, and `a >= b` is not `a < b`.
        let (op, x, y, flip) = match answers {
            [Some(true), Some(false), Some(false)] => (less, a, b, false),
            [Some(false), Some(false), Some(true)] => (less, b, a, false),
            [Some(true), Some(true), Some(false)] => (less, b, a, true),
            [Some(false), Some(true), Some(true)] => (less, a, b, true),
            _ => return None,
        };
        let below = out.compare(op, x, y);
        if !flip {
            return Some(vec![below]);
        }
        let one = out.int(form::Bits::B8, 1);
        Some(vec![out.binary(form::Binary::Xor, below, one)])
    }

    /// Lowers a call of `narrow` or one of its relatives.
    pub(super) fn narrow_call(
        &mut self,
        out: &mut Out<'a>,
        expr: &'a Expr,
        call: &'a Call,
        narrowing: Narrowing,
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let typed = self.typed;
        let (given, values) = self.expr(out, &call.args[0].value, env);
        let (Ty::Int(from), ty) = (given, typed.type_of(expr, env)) else {
            unreachable!("checked")
        };
        let to = match &ty {
            Ty::Int(to) => *to,
            Ty::Enum(_, of) => match of.as_slice() {
                [Ty::Int(to)] => *to,
                _ => unreachable!("checked"),
            },
            _ => unreachable!("checked"),
        };
        let values = self.narrowing(out, narrowing, (from, to), values[0]);
        (ty, values)
    }

    /// Lowers a call of a function the compiler builds for the prelude's
    /// operations on numbers.
    pub(super) fn intrinsic_call(
        &mut self,
        out: &mut Out<'a>,
        expr: &'a Expr,
        call: &'a Call,
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let typed = self.typed;
        let mut args = Vec::new();
        let mut int = None;
        for arg in &call.args {
            let (ty, values) = self.expr(out, &arg.value, env);
            if let (Ty::Int(t), None) = (ty, int) {
                int = Some(t);
            }
            args.push(values[0]);
        }
        let int = int.expect("checked");
        let values = match typed.type_of(expr, env) {
            Ty::Int(to) if call.callee == "__convert" => self.widening(out, int, to, args[0]),
            _ => self.intrinsic(out, &call.callee, int, &args),
        };
        (typed.type_of(expr, env), values)
    }

    /// What the function the compiler builds for an operation on numbers of
    /// type `t` gives for the values `args`: whether they are equal, how
    /// they are ordered, or the result of `wrapping_`, `saturating_`, or
    /// `checked_` `add`, `sub`, or `mul`.
    pub(super) fn intrinsic(
        &mut self,
        out: &mut Out<'a>,
        name: &str,
        t: Int,
        args: &[form::Value],
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let (a, b) = (args[0], args.get(1).copied());
        let case = |out: &mut Out<'a>, e: usize, name: &str| {
            let k = defs.case_of(e, name).expect("a case");
            out.constant(
                form::Constant::Int(k as u64),
                form::Type::Int(form::Bits::B8),
            )
        };
        match name {
            "__equal" => vec![out.compare(form::Compare::Equal, a, b.expect("two"))],
            "__compare" => {
                let b = b.expect("two");
                let less = if t.signed() {
                    form::Compare::LessS
                } else {
                    form::Compare::LessU
                };
                let below = out.compare(less, a, b);
                let same = out.compare(form::Compare::Equal, a, b);
                let (l, e, g) = (
                    case(out, defs.order, "less"),
                    case(out, defs.order, "equal"),
                    case(out, defs.order, "greater"),
                );
                let after = out.choose(same, e, g);
                vec![out.choose(below, l, after)]
            }
            "__bit_and" => vec![out.binary(form::Binary::And, a, b.expect("two"))],
            "__bit_or" => vec![out.binary(form::Binary::Or, a, b.expect("two"))],
            "__bit_xor" => vec![out.binary(form::Binary::Xor, a, b.expect("two"))],
            "__bit_not" => {
                let all = out.int(bits_of(t), -1);
                vec![out.binary(form::Binary::Xor, a, all)]
            }
            "__shift_left" | "__shift_right" => {
                let by = b.expect("two");
                let op = match (name, t.signed()) {
                    ("__shift_left", _) => form::Binary::ShiftLeft,
                    (_, true) => form::Binary::ShiftRightS,
                    (_, false) => form::Binary::ShiftRightU,
                };
                vec![out.binary(op, a, by)]
            }
            "__decimal" => {
                let kind = Runtime::Decimal {
                    bits: bits_of(t),
                    signed: t.signed(),
                };
                self.call_runtime(out, kind, vec![a], TEXT.to_vec())
            }
            _ => {
                let (kind, op) = name[2..].split_once('_').expect("a named operation");
                self.arithmetic(out, kind, op, t, a, b.expect("two"))
            }
        }
    }

    /// The values of `a` and `b`, numbers of type `t`, put through an
    /// operation: `add`, `sub`, or `mul`. `wrapping` gives the result with
    /// the bits that do not fit left off, `saturating` gives the nearest
    /// value that fits, and `checked` gives an `Option` that holds the
    /// result if it fits.
    ///
    /// A type of fewer than 64 bits is worked out in 64, where the result is
    /// exact, and it fits if it is between the limits of the type. For a
    /// type of 64 bits the result wraps, and whether it fit is read from the
    /// half of the product that did not stay, or from the signs.
    pub(super) fn arithmetic(
        &mut self,
        out: &mut Out<'a>,
        kind: &str,
        op: &str,
        t: Int,
        a: form::Value,
        b: form::Value,
    ) -> Vec<form::Value> {
        use form::Binary::*;
        use form::Compare::*;
        let defs = self.typed.defs;
        let bits = bits_of(t);
        let word = match op {
            "add" => Add,
            "sub" => Sub,
            _ => Mul,
        };
        if kind == "wrapping" {
            return vec![out.binary(word, a, b)];
        }
        let (r, below, above) = if t.bits() < 64 {
            let (x, y) = (out.widen(t.signed(), a), out.widen(t.signed(), b));
            let r = out.binary(word, x, y);
            let wide = form::Bits::B64;
            let (least, greatest) = (out.int(wide, t.min()), out.int(wide, t.max()));
            let no = out.int(form::Bits::B8, 0);
            let (below, above) = if t.signed() {
                (
                    out.compare(LessS, r, least),
                    out.compare(LessS, greatest, r),
                )
            } else if op == "sub" {
                (out.compare(LessU, x, y), no)
            } else {
                (no, out.compare(LessU, greatest, r))
            };
            (out.narrow(r, bits), below, above)
        } else {
            let r = out.binary(word, a, b);
            let zero = out.int(bits, 0);
            let no = out.int(form::Bits::B8, 0);
            let one = out.int(form::Bits::B8, 1);
            let (below, above) = if !t.signed() {
                match op {
                    "add" => (no, out.compare(LessU, r, a)),
                    "sub" => (out.compare(LessU, a, b), no),
                    _ => {
                        let high = out.binary(MulHighU, a, b);
                        (no, out.compare(LessU, zero, high))
                    }
                }
            } else {
                // Which way an overflow went is decided by the sign of the
                // second operand, or for a product by whether the signs of
                // the operands differ.
                let negative = match op {
                    "mul" => {
                        let signs = out.binary(Xor, a, b);
                        out.compare(LessS, signs, zero)
                    }
                    _ => out.compare(LessS, b, zero),
                };
                let overflowed = match op {
                    "add" => {
                        let (x, y) = (out.binary(Xor, a, r), out.binary(Xor, b, r));
                        let both = out.binary(And, x, y);
                        out.compare(LessS, both, zero)
                    }
                    "sub" => {
                        let (x, y) = (out.binary(Xor, a, b), out.binary(Xor, a, r));
                        let both = out.binary(And, x, y);
                        out.compare(LessS, both, zero)
                    }
                    _ => {
                        let high = out.binary(MulHighS, a, b);
                        let below_zero = out.compare(LessS, r, zero);
                        let all = out.int(bits, -1);
                        let sign = out.choose(below_zero, all, zero);
                        let same = out.compare(Equal, high, sign);
                        out.binary(Xor, same, one)
                    }
                };
                let positive = out.binary(Xor, negative, one);
                // An addition goes below when the operand added is negative,
                // a subtraction when the operand taken is not, and a product
                // when the operands' signs differ.
                let (down, up) = match op {
                    "sub" => (positive, negative),
                    _ => (negative, positive),
                };
                (
                    out.binary(And, overflowed, down),
                    out.binary(And, overflowed, up),
                )
            };
            (r, below, above)
        };
        if kind == "saturating" {
            let (least, greatest) = (out.int(bits, t.min()), out.int(bits, t.max()));
            let low = out.choose(below, least, r);
            return vec![out.choose(above, greatest, low)];
        }
        let one = out.int(form::Bits::B8, 1);
        let wrong = out.choose(below, one, above);
        let some = defs.case_of(defs.option, "some").expect("a case");
        let none = defs.case_of(defs.option, "none").expect("a case");
        let some = out.int(form::Bits::B8, some as i128);
        let none = out.int(form::Bits::B8, none as i128);
        let zero = out.int(bits, 0);
        vec![out.choose(wrong, none, some), out.choose(wrong, zero, r)]
    }

    /// The value `a`, a whole number of type `from`, taken to type `to`,
    /// which does not hold all of its values: as `narrow` and `wrapping_narrow`
    /// do, which keep the bits, as `saturating_narrow` does, which gives the
    /// nearest value that fits, and as `checked_narrow` does, which gives an
    /// `Option` that holds the value if it fits. A narrowing to a smaller type
    /// is one instruction, and to a type of the same size none.
    pub(super) fn narrowing(
        &mut self,
        out: &mut Out<'a>,
        narrowing: Narrowing,
        (from, to): (Int, Int),
        a: form::Value,
    ) -> Vec<form::Value> {
        use form::Compare::*;
        let defs = self.typed.defs;
        let (wide, narrow) = (bits_of(from), bits_of(to));
        let r = out.resize(from.signed(), a, narrow);
        if matches!(narrowing, Narrowing::Plain | Narrowing::Wrapping) {
            return vec![r];
        }
        let less = if from.signed() { LessS } else { LessU };
        let no = out.int(form::Bits::B8, 0);
        let below = if from.min() < to.min() {
            let least = out.int(wide, to.min());
            out.compare(less, a, least)
        } else {
            no
        };
        let above = if from.max() > to.max() {
            let greatest = out.int(wide, to.max());
            out.compare(less, greatest, a)
        } else {
            no
        };
        if narrowing == Narrowing::Saturating {
            let (least, greatest) = (out.int(narrow, to.min()), out.int(narrow, to.max()));
            let low = out.choose(below, least, r);
            return vec![out.choose(above, greatest, low)];
        }
        let one = out.int(form::Bits::B8, 1);
        let wrong = out.choose(below, one, above);
        let some = defs.case_of(defs.option, "some").expect("a case");
        let none = defs.case_of(defs.option, "none").expect("a case");
        let some = out.int(form::Bits::B8, some as i128);
        let none = out.int(form::Bits::B8, none as i128);
        let zero = out.int(narrow, 0);
        vec![out.choose(wrong, none, some), out.choose(wrong, zero, r)]
    }

    /// The number `value` of type `from` as a number of type `to`, which
    /// holds every value of `from`.
    pub(super) fn widening(
        &mut self,
        out: &mut Out<'a>,
        from: Int,
        to: Int,
        value: form::Value,
    ) -> Vec<form::Value> {
        vec![out.resize(from.signed(), value, bits_of(to))]
    }
}
