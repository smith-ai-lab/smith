//! Checks numbers and operators: the whole number types, a number written out,
//! `+`, `-`, and `*`, the `wrapping_`, `saturating_`, `checked_`, and `narrow`
//! functions, the functions of `Bits`, and `Convert` between numbers.

use super::*;

/// The whole number types, which the compiler builds.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub(super) enum Int {
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
}

impl Int {
    pub(super) const ALL: [Int; 8] = [
        Int::I8,
        Int::I16,
        Int::I32,
        Int::I64,
        Int::U8,
        Int::U16,
        Int::U32,
        Int::U64,
    ];

    pub(super) fn name(self) -> &'static str {
        match self {
            Int::I8 => "i8",
            Int::I16 => "i16",
            Int::I32 => "i32",
            Int::I64 => "i64",
            Int::U8 => "u8",
            Int::U16 => "u16",
            Int::U32 => "u32",
            Int::U64 => "u64",
        }
    }

    /// The type a name written in a file stands for: `Int` is `i64`, `Nat`
    /// is `u64`, and `Byte` is `u8`.
    pub(super) fn named(name: &str) -> Option<Int> {
        let name = match name {
            "Int" => "i64",
            "Byte" => "u8",
            "Nat" => "u64",
            _ => name,
        };
        Int::ALL.into_iter().find(|t| t.name() == name)
    }

    pub(super) fn signed(self) -> bool {
        matches!(self, Int::I8 | Int::I16 | Int::I32 | Int::I64)
    }

    pub(super) fn bits(self) -> u32 {
        match self {
            Int::I8 | Int::U8 => 8,
            Int::I16 | Int::U16 => 16,
            Int::I32 | Int::U32 => 32,
            Int::I64 | Int::U64 => 64,
        }
    }

    /// The least value of the type.
    pub(super) fn min(self) -> i128 {
        if self.signed() {
            -(1i128 << (self.bits() - 1))
        } else {
            0
        }
    }

    /// The greatest value of the type.
    pub(super) fn max(self) -> i128 {
        if self.signed() {
            (1i128 << (self.bits() - 1)) - 1
        } else {
            (1i128 << self.bits()) - 1
        }
    }
}

/// Whether `expr`, a number with `magnitude`, is a character outside ASCII.
pub(super) fn expr_is_wide_character(expr: &Expr, magnitude: u128) -> bool {
    matches!(expr, Expr::Number { text, .. } if text.starts_with('\'')) && magnitude > 127
}

/// Whether an expression is a literal, or an operation on literals only,
/// which takes the type of what it is used with.
pub(super) fn flexible(e: &Expr) -> bool {
    match e {
        Expr::Number { .. } => true,
        Expr::Arith { left, right, .. } => flexible(left) && flexible(right),
        Expr::Match(m) => literal_blocks(m),
        _ => false,
    }
}

/// Whether an `if` with `else`, or a `match`, gives a value that every block
/// that does not leave ends in a literal, and at least one does not leave.
/// Its type is that of what it is used with, as a literal's is.
pub(super) fn literal_blocks(m: &Match) -> bool {
    if !matches!(m.form, Form::Match | Form::If { otherwise: true }) {
        return false;
    }
    let mut blocks = m
        .arms
        .iter()
        .filter(|a| !proof::ends_in_exit(&a.body))
        .peekable();
    blocks.peek().is_some()
        && blocks.all(|a| matches!(a.body.last(), Some(Stmt::Expr(e)) if flexible(e)))
}

/// The whole numbers a body binds to names with no type written.
pub(super) enum Numbers {
    /// None are looked for, as in a default.
    Off,
    /// The first look, at how each is used, which gives each an undecided
    /// type. The places of the literals, in order, are noted.
    Finding(Vec<Pos>),
    /// The second look, with each type decided, by the place of its
    /// literal, with the place of the use that decided it, if one did.
    Decided(HashMap<Pos, (Int, Option<Pos>)>),
}

/// What a narrowing does with a value that does not fit.
#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum Narrowing {
    /// `narrow`, which compiles only when the value is proven to fit.
    Plain,
    /// `wrapping_narrow`, which keeps the low bits.
    Wrapping,
    /// `saturating_narrow`, which gives the nearest value the type holds.
    Saturating,
    /// `checked_narrow`, which gives `.none`.
    Checked,
}

impl Narrowing {
    pub(super) fn name(self) -> &'static str {
        match self {
            Narrowing::Plain => "narrow",
            Narrowing::Wrapping => "wrapping_narrow",
            Narrowing::Saturating => "saturating_narrow",
            Narrowing::Checked => "checked_narrow",
        }
    }
}

/// Which narrowing a call is of, if it is of one the compiler builds, which
/// no function of the program takes the name of.
pub(super) fn narrowing(defs: &Defs, call: &Call) -> Option<Narrowing> {
    let named = [
        Narrowing::Plain,
        Narrowing::Wrapping,
        Narrowing::Saturating,
        Narrowing::Checked,
    ]
    .into_iter()
    .find(|n| n.name() == call.callee)?;
    let taken = defs.function_names.contains_key(call.callee.as_str())
        || defs.struct_names.contains_key(call.callee.as_str())
        || defs.trait_fns.contains_key(call.callee.as_str());
    (call.qualifier.is_none() && !taken).then_some(named)
}

impl<'a> Checker<'a> {
    /// Checks a whole number literal bound to `name` with no type written.
    /// While the uses are looked at, it is an undecided number. Once they
    /// have decided its type, it is that type, and the literal must fit.
    pub(super) fn literal_binding(&mut self, body: &mut Body<'a>, name: &str, value: &Expr) -> Ty {
        let pos = value.pos();
        match &mut body.numbers {
            Numbers::Off => unreachable!("numbers are looked for"),
            Numbers::Finding(places) => {
                places.push(pos);
                self.expr(body, value, &Want::Ty(Ty::Unknown));
                let Numbers::Finding(places) = &body.numbers else {
                    unreachable!()
                };
                Ty::Flex(places.len() - 1)
            }
            Numbers::Decided(decided) => {
                let (int, by) = decided[&pos];
                self.deciding = by;
                let ty = self.expr(body, value, &Want::Ty(Ty::Int(int)));
                self.deciding = None;
                body.decided.insert(name.to_string(), (int, by));
                ty
            }
        }
    }

    /// Checks `left + right`, `left - right`, or `left * right`, whose sides
    /// are whole numbers of one type, which is the type it gives. It
    /// compiles only when `arith` proves that the result fits that type.
    pub(super) fn arith(
        &mut self,
        body: &mut Body<'a>,
        expr: &Expr,
        (op, left, right, op_pos): (ArithOp, &Expr, &Expr, Pos),
        want: &Want,
    ) -> Ty {
        let wanted = match want {
            Want::Ty(t @ (Ty::Int(_) | Ty::Flex(_))) => Want::Ty(t.clone()),
            _ => Want::Any,
        };
        let (l, r) = match (flexible(left), flexible(right)) {
            (true, false) => {
                let r = self.expr(body, right, &Want::Any);
                let side = Self::side_want(&r);
                (self.expr(body, left, &side), r)
            }
            (false, true) => {
                let l = self.expr(body, left, &Want::Any);
                let side = Self::side_want(&l);
                let r = self.expr(body, right, &side);
                (l, r)
            }
            (true, true) => (
                self.expr(body, left, &wanted),
                self.expr(body, right, &wanted),
            ),
            // A call whose result a literal may decide takes the type of the
            // other side, as a literal does.
            (false, false) if matches!(right, Expr::Call(_)) && !matches!(left, Expr::Call(_)) => {
                let l = self.expr(body, left, &Want::Any);
                let side = Self::side_want(&l);
                (l, self.expr(body, right, &side))
            }
            (false, false) if matches!(left, Expr::Call(_)) && !matches!(right, Expr::Call(_)) => {
                let r = self.expr(body, right, &Want::Any);
                let side = Self::side_want(&r);
                (self.expr(body, left, &side), r)
            }
            (false, false) => (
                self.expr(body, left, &Want::Any),
                self.expr(body, right, &Want::Any),
            ),
        };
        let symbol = op.symbol();
        let defs = self.defs;
        let gives = match (&l, &r) {
            (Ty::Unknown, _) | (_, Ty::Unknown) => return Ty::Unknown,
            (Ty::Int(a), Ty::Int(b)) if a == b => l.clone(),
            (Ty::Int(a), Ty::Int(b)) => {
                self.errors.push(Error::new(
                    op_pos,
                    format!(
                        "`{symbol}` needs both sides to be of one type, and the left is `{}` \
                         where the right is `{}`",
                        self.defs.spelling(*a),
                        self.defs.spelling(*b)
                    ),
                ));
                return Ty::Unknown;
            }
            (Ty::Flex(_) | Ty::Int(_), Ty::Flex(_) | Ty::Int(_)) => {
                use_at(op_pos);
                if !flex_pair(&l, &r) {
                    unreachable!("a number and a number")
                }
                if matches!(l, Ty::Flex(_)) && matches!(r, Ty::Flex(_)) {
                    l.clone()
                } else if matches!(l, Ty::Flex(_)) {
                    r.clone()
                } else {
                    l.clone()
                }
            }
            (other, _) | (_, other) if !matches!(other, Ty::Int(_) | Ty::Flex(_)) => {
                let given = defs.describe(other, body.scope);
                self.errors.push(
                    Error::new(
                        op_pos,
                        format!("`{symbol}` is for whole numbers, and a side of it is {given}"),
                    )
                    .with_fix("only whole numbers have arithmetic so far"),
                );
                return Ty::Unknown;
            }
            _ => return Ty::Unknown,
        };
        if let Ty::Int(t) = gives {
            self.fits(body, expr, t);
        }
        gives
    }

    /// What is wanted of a literal that is an operand, when the other
    /// operand has type `other`.
    pub(super) fn side_want(other: &Ty) -> Want {
        match other {
            Ty::Int(_) | Ty::Flex(_) => Want::Ty(other.clone()),
            _ => Want::Any,
        }
    }

    /// Checks a whole number literal, which becomes the type that is wanted
    /// of it, as `FromInt` says, when it fits.
    pub(super) fn number(
        &mut self,
        body: &Body,
        expr: &Expr,
        magnitude: u128,
        negative: bool,
        pos: Pos,
        want: &Want,
    ) -> Ty {
        let defs = self.defs;
        let int = match want {
            Want::Ty(Ty::Int(int)) => *int,
            Want::Any | Want::Unused => Int::I64,
            Want::Ty(t @ (Ty::Unknown | Ty::Flex(_))) => return t.clone(),
            Want::Ty(needed) => {
                let needed = defs.describe(needed, body.scope);
                self.errors.push(Error::new(
                    pos,
                    format!("this is a number, where {needed} is needed"),
                ));
                return Ty::Unknown;
            }
        };
        let fits = if negative {
            magnitude <= int.min().unsigned_abs()
        } else {
            magnitude <= int.max() as u128
        };
        // A character outside ASCII takes more than one byte in a text.
        let wide = expr_is_wide_character(expr, magnitude);
        if fits && wide && int.bits() == 8 {
            self.errors.push(Error::new(
                pos,
                format!(
                    "`{}` is not ASCII, so it takes more than one byte and is not a `{}`",
                    written_expr(expr),
                    self.defs.spelling(int)
                ),
            ));
        }
        if !fits {
            let because = match self.deciding {
                Some(at) => format!(", as the use at {}:{} needs", at.line, at.col),
                None => String::new(),
            };
            self.errors.push(Error::new(
                pos,
                format!(
                    "`{}` does not fit `{}`{because}, which holds {} to {}",
                    written_expr(expr),
                    self.defs.spelling(int),
                    int.min(),
                    int.max()
                ),
            ));
        }
        Ty::Int(int)
    }

    /// Checks a call of a function the compiler builds for the prelude: it
    /// takes whole numbers of one type, and gives the type, a `bool`, an
    /// `Order`, an `Option` of the type, or text.
    pub(super) fn intrinsic(&mut self, body: &mut Body<'a>, call: &Call) -> Ty {
        let defs = self.defs;
        let tys: Vec<Ty> = (call.args.iter())
            .map(|a| self.expr(body, &a.value, &Want::Any))
            .collect();
        let ty = tys[0].clone();
        match call.callee.as_str() {
            "__equal" => defs.bool_ty(),
            "__compare" => Ty::Enum(defs.order, Vec::new()),
            "__decimal" => {
                self.needs(body, "alloc", call.pos, "`__decimal`");
                Ty::Text
            }
            name if name.starts_with("__checked_") => Ty::Enum(defs.option, vec![ty]),
            _ => ty,
        }
    }

    /// Checks the amount of a call of `shift_left` or `shift_right`, which
    /// compiles only when `arith` proves that it is below the number of bits
    /// of the type `ty` that is shifted.
    pub(super) fn shift(&mut self, body: &mut Body<'a>, call: &Call, sig: &Signature, ty: &Ty) {
        let by = (params_of(&sig.params, &call.args).into_iter())
            .position(|p| p == 1)
            .map(|i| &call.args[i].value);
        match (ty, by) {
            (Ty::Int(t), Some(by)) => self.shifts(body, by, *t, call.pos),
            (Ty::Unknown, _) | (_, None) => {}
            _ => self.errors.push(Error::unsupported(
                call.pos,
                "a shift of a number whose type is a type parameter",
            )),
        }
    }

    /// Checks `narrow(x)` or one of its relatives, which take the whole
    /// number `x` to the whole number type that is expected, one that does
    /// not hold all the values of the type of `x`. `checked_narrow` is
    /// expected to give an `Option` of it. `narrow` compiles only when
    /// `arith` proves that `x` fits.
    pub(super) fn narrow(
        &mut self,
        body: &mut Body<'a>,
        call: &Call,
        narrowing: Narrowing,
        want: &Want,
    ) -> Ty {
        let defs = self.defs;
        let name = narrowing.name();
        let plain = call.args.iter().all(|a| a.name.is_none() && !a.after);
        if call.args.len() != 1 || !plain {
            self.errors.push(Error::new(
                call.pos,
                format!("`{name}` takes the one whole number to narrow"),
            ));
            return self.unknown_call(body, call);
        }
        let value = &call.args[0].value;
        let given = self.expr(body, value, &Want::Any);
        let from = match given {
            Ty::Int(from) => from,
            Ty::Unknown | Ty::Flex(_) => return Ty::Unknown,
            other => {
                let given = defs.describe(&other, body.scope);
                self.errors.push(Error::new(
                    value.pos(),
                    format!("`{name}` takes a whole number, and this is {given}"),
                ));
                return Ty::Unknown;
            }
        };
        let (wanted, to) = match want {
            Want::Ty(Ty::Unknown | Ty::Flex(_)) => return Ty::Unknown,
            Want::Ty(Ty::Int(to)) if narrowing != Narrowing::Checked => (Ty::Int(*to), *to),
            Want::Ty(Ty::Enum(e, of)) if *e == defs.option && narrowing == Narrowing::Checked => {
                match of.as_slice() {
                    [Ty::Int(to)] => (Ty::Enum(*e, of.clone()), *to),
                    _ => return self.narrow_unexpected(body, call, name, want),
                }
            }
            _ => return self.narrow_unexpected(body, call, name, want),
        };
        if to.min() <= from.min() && from.max() <= to.max() {
            let error = if to == from {
                Error::new(
                    call.pos,
                    format!(
                        "this is already a `{}`, so there is nothing to narrow",
                        self.defs.spelling(from)
                    ),
                )
            } else {
                Error::new(
                    call.pos,
                    format!(
                        "`{}` holds every value of `{}`, so there is nothing to narrow",
                        self.defs.spelling(to),
                        self.defs.spelling(from)
                    ),
                )
                .with_fix("use `convert`")
            };
            self.errors.push(error);
            return Ty::Unknown;
        }
        if narrowing == Narrowing::Plain {
            // What it gives is the number it is given, which `arith` knows.
            self.narrowed.insert(addr(call));
            let written = format!("{name}({})", written_expr(value));
            self.narrows(body, value, (from, to), (&written, call.pos));
        }
        wanted
    }

    /// The error for a narrowing that is not given the type it gives.
    pub(super) fn narrow_unexpected(
        &mut self,
        body: &Body,
        call: &Call,
        name: &str,
        want: &Want,
    ) -> Ty {
        let message = match want {
            Want::Ty(needed) => {
                let needed = self.defs.describe(needed, body.scope);
                format!(
                    "`{name}` gives {}, and {needed} is needed",
                    self.narrowed(name)
                )
            }
            _ => format!("`{name}` gives the type that is expected here, and none is"),
        };
        let fix = match name {
            "checked_narrow" => "write the type, as in `y: Option[u8] = checked_narrow(x)`",
            _ => "write the type, as in `y: u8 = narrow(x)`",
        };
        self.errors
            .push(Error::new(call.pos, message).with_fix(fix));
        Ty::Unknown
    }

    /// What a narrowing gives, in words.
    pub(super) fn narrowed(&self, name: &str) -> &'static str {
        match name {
            "checked_narrow" => "an `Option` of a whole number",
            _ => "a whole number",
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
}
