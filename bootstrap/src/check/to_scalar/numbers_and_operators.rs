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

    /// Lowers a call of a function that only calls the function the compiler
    /// builds for `kind` and `op` on its parameters, in order, at the call: a
    /// `wrapping_`, `saturating_`, or `checked_` `add`, `sub`, or `mul`. No
    /// call, frame, or return is left, whatever the function's size.
    /// `places` says which parameter each argument is for.
    pub(super) fn number_call(
        &mut self,
        out: &mut Out<'a>,
        expr: &'a Expr,
        call: &'a Call,
        (kind, op): (&str, &str),
        places: &[usize],
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let mut args = [None; 2];
        let mut int = None;
        for (arg, at) in call.args.iter().zip(places) {
            let (ty, values) = self.expr(out, &arg.value, env);
            if let (Ty::Int(t), None) = (ty, int) {
                int = Some(t);
            }
            args[*at] = Some(values[0]);
        }
        let (a, b) = (args[0].expect("checked"), args[1].expect("checked"));
        let values = self.arithmetic(out, kind, op, int.expect("checked"), a, b);
        (self.typed.type_of(expr, env), values)
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
    /// The operation says whether its answer fitted, except for an addition or
    /// a subtraction of an unsigned number of fewer than 64 bits, which a
    /// comparison says.
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
        let bits = bits_of(t);
        let word = match op {
            "add" => Add,
            "sub" => Sub,
            _ => Mul,
        };
        if kind == "wrapping" {
            return vec![out.binary(word, a, b)];
        }
        if !t.signed() && op != "mul" && t.bits() < 64 {
            // The sum wraps and is below an operand exactly when it did not
            // fit, and a difference does not fit exactly when the operand
            // taken is the greater: one operation, one comparison, and one
            // choice, which is shorter than widening the numbers to see
            // whether they fit. The second is the operand that is a number
            // written out more often, which a compare holds in itself.
            if kind == "saturating" && op == "sub" {
                // The greater of the two, less the second: no number below
                // 0 is made, so no comparison of the difference is needed.
                let below = out.compare(LessU, a, b);
                let greater = out.choose(below, b, a);
                return vec![out.binary(Sub, greater, b)];
            }
            let end = out.int(bits, if op == "add" { t.max() } else { 0 });
            let r = out.binary(word, a, b);
            let wrong = if op == "add" {
                out.compare(LessU, r, b)
            } else {
                out.compare(LessU, a, b)
            };
            if kind == "saturating" {
                return vec![out.choose(wrong, end, r)];
            }
            return self.optional(out, bits, wrong, r);
        }
        // The operation says whether its answer fitted, and a target makes
        // that the flag of the operation. The end that a saturating function
        // stops at is worked out first, from the operands, so that nothing
        // that changes the flags comes between the operation and the choice.
        let end = (kind == "saturating").then(|| {
            if !t.signed() {
                return out.int(bits, if op == "sub" { 0 } else { t.max() });
            }
            // The end is the greatest value when the answer went above it,
            // and the least when it went below, which the signs say: an
            // addition goes below when the second is negative, a subtraction
            // when it is not, and a product when the signs differ.
            let sign = if op == "mul" {
                out.binary(Xor, a, b)
            } else {
                b
            };
            let by = out.int(bits, i128::from(t.bits() - 1));
            let all_or_none = out.binary(ShiftRightS, sign, by);
            let limit = out.int(bits, if op == "sub" { t.min() } else { t.max() });
            out.binary(Xor, all_or_none, limit)
        });
        let (r, overflowed) = out.overflows(word, t.signed(), a, b);
        match end {
            Some(end) => vec![out.choose(overflowed, end, r)],
            None => self.optional(out, bits, overflowed, r),
        }
    }

    /// The `Option` that holds `r`, a number of `bits`, unless `wrong`, a
    /// `bool`, says it did not fit: its tag, then its number.
    fn optional(
        &mut self,
        out: &mut Out<'a>,
        bits: form::Bits,
        wrong: form::Value,
        r: form::Value,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let some = defs.case_of(defs.option, "some").expect("a case");
        let none = defs.case_of(defs.option, "none").expect("a case");
        let zero = out.int(bits, 0);
        // `wrong` is 1 or 0, which is the case when `.none` is 1 and `.some` is 0.
        let tag = if (none, some) == (1, 0) {
            wrong
        } else {
            let some = out.int(form::Bits::B8, some as i128);
            let none = out.int(form::Bits::B8, none as i128);
            out.choose(wrong, none, some)
        };
        vec![tag, out.choose(wrong, zero, r)]
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

/// For a function whose whole body, in the prelude or the library, is a call of the function the compiler builds for `wrapping_`,
/// `saturating_`, or `checked_` `add`, `sub`, or `mul`, which kind it is and
/// which operation.
pub(super) fn number_function(f: &Function) -> Option<(&'static str, &'static str)> {
    let (kind, op) = f.name.split_once('_')?;
    let kind = ["wrapping", "saturating", "checked"]
        .into_iter()
        .find(|k| *k == kind)?;
    let op = ["add", "sub", "mul"].into_iter().find(|o| *o == op)?;
    (f.body.len() == 1 && f.params.len() == 2 && (of_prelude(f.pos) || of_std(f.pos)))
        .then_some((kind, op))
}
