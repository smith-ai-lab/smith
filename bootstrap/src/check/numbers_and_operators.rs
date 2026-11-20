//! Checks numbers and operators: the whole number types, a number written out,
//! `+`, `-`, and `*`, the `wrapping_`, `saturating_`, `checked_`, and `narrow`
//! functions, the functions of `Bits`, and `Convert` between numbers.

use super::*;

/// A whole number type: the range from `min` to `max`, both held.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub(super) struct Int {
    min: i128,
    max: i128,
}

impl Int {
    pub(super) const I8: Int = Int::sized(true, 8);
    pub(super) const I16: Int = Int::sized(true, 16);
    pub(super) const I32: Int = Int::sized(true, 32);
    pub(super) const I64: Int = Int::sized(true, 64);
    pub(super) const U8: Int = Int::sized(false, 8);
    pub(super) const U16: Int = Int::sized(false, 16);
    pub(super) const U32: Int = Int::sized(false, 32);
    pub(super) const U64: Int = Int::sized(false, 64);

    /// The sizes the compiler keeps a range at, which each has a name.
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

    /// The range that fills `bits` bits, signed or not.
    const fn sized(signed: bool, bits: u32) -> Int {
        if signed {
            Int {
                min: -(1i128 << (bits - 1)),
                max: (1i128 << (bits - 1)) - 1,
            }
        } else {
            Int {
                min: 0,
                max: (1i128 << bits) - 1,
            }
        }
    }

    /// The range from `min` up to and including `max`, or none if a
    /// number of it needs more than 64 bits.
    pub(super) fn between(min: i128, max: i128) -> Option<Int> {
        let int = Int { min, max };
        let holder = int.holder();
        (int.min >= holder.min && max <= holder.max && max >= min - 1).then_some(int)
    }

    /// The name a size is written with, which is `u8` for `0..256`.
    fn size_name(self) -> Option<&'static str> {
        const NAMES: [&str; 8] = ["i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"];
        Int::ALL.iter().position(|t| *t == self).map(|i| NAMES[i])
    }

    /// How the type is written when no name of it is: `a..b`.
    pub(super) fn name(self) -> String {
        match self.size_name() {
            Some(name) => name.to_string(),
            None => format!("{}..{}", self.min, self.max + 1),
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
        Int::ALL.into_iter().find(|t| t.size_name() == Some(name))
    }

    /// Whether the type has a negative value, so that it is kept signed.
    pub(super) fn signed(self) -> bool {
        self.min < 0
    }

    /// How many bits the smallest size that holds the type as itself has.
    pub(super) fn bits(self) -> u32 {
        [8, 16, 32, 64]
            .into_iter()
            .find(|&bits| {
                let size = Int::sized(self.signed(), bits);
                size.min <= self.min && self.max <= size.max
            })
            .unwrap_or(64)
    }

    /// Whether every value of `other` is a value of the type.
    pub(super) fn holds(self, other: Int) -> bool {
        self.min <= other.min && other.max <= self.max
    }

    /// The smallest range that holds every number of both, if a type holds it.
    pub(super) fn hull(self, other: Int) -> Option<Int> {
        Int::between(self.min.min(other.min), self.max.max(other.max))
    }

    /// The size the type is kept at: the smallest one that holds it as itself.
    pub(super) fn holder(self) -> Int {
        Int::sized(self.signed(), self.bits())
    }

    /// The least value of the type.
    pub(super) fn min(self) -> i128 {
        self.min
    }

    /// The greatest value of the type.
    pub(super) fn max(self) -> i128 {
        self.max
    }
}

/// Whether `expr`, a number with `magnitude`, is a character outside ASCII.
pub(super) fn expr_is_wide_character(expr: &Expr, magnitude: u128) -> bool {
    matches!(expr, Expr::Number { text, .. } if text.starts_with('\'')) && magnitude > 127
}

/// The exact range of `left op right`, when a type holds it: from the least
/// to the greatest value the operation gives over the numbers of both.
fn exact(op: ArithOp, left: Int, right: Int) -> Option<Int> {
    let (low, high) = match op {
        ArithOp::Add => (
            left.min().checked_add(right.min())?,
            left.max().checked_add(right.max())?,
        ),
        ArithOp::Sub => (
            left.min().checked_sub(right.max())?,
            left.max().checked_sub(right.min())?,
        ),
        ArithOp::Mul => {
            let products = [
                left.min().checked_mul(right.min())?,
                left.min().checked_mul(right.max())?,
                left.max().checked_mul(right.min())?,
                left.max().checked_mul(right.max())?,
            ];
            (*products.iter().min()?, *products.iter().max()?)
        }
    };
    Int::between(low, high)
}

/// A function of `Bits` whose result has the exact range of what it makes
/// of numbers that are not negative.
#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum BitFn {
    And,
    Or,
    Xor,
    Left,
    Right,
}

impl BitFn {
    pub(super) fn named(name: &str) -> Option<BitFn> {
        Some(match name {
            "bit_and" => BitFn::And,
            "bit_or" => BitFn::Or,
            "bit_xor" => BitFn::Xor,
            "shift_left" => BitFn::Left,
            "shift_right" => BitFn::Right,
            _ => return None,
        })
    }
}

/// Which function of `Bits` a call of two numbers is, where it is written by
/// the name the trait gives it and no function of the program takes that name.
pub(super) fn bits_function(defs: &Defs, call: &Call) -> Option<BitFn> {
    let op = BitFn::named(&call.callee)?;
    let plain = call.args.len() == 2 && call.args.iter().all(|a| a.name.is_none() && !a.after);
    let of_bits = match &call.qualifier {
        None => {
            defs.function(call.name_pos, &call.callee).is_none()
                && defs
                    .trait_fns_at(call.name_pos, &call.callee)
                    .is_some_and(|of| matches!(of[..], [(k, _)] if k == defs.bits))
        }
        Some((q, at)) => defs.trait_at(*at, q) == Some(defs.bits),
    };
    (plain && of_bits).then_some(op)
}

/// The least and the greatest of `x op y` over every `x` from `a.0` to `a.1`
/// and every `y` from `b.0` to `b.1`, for numbers that are not negative: found
/// bit by bit from the top, as Warren's "Propagating bounds through logical
/// operations" does.
fn bit_bounds(op: BitFn, (a, b): ((u64, u64), (u64, u64))) -> (u64, u64) {
    let ((a0, a1), (c0, c1)) = (a, b);
    match op {
        BitFn::And => (min_and(a0, a1, c0, c1), max_and(a0, a1, c0, c1)),
        BitFn::Or => (min_or(a0, a1, c0, c1), max_or(a0, a1, c0, c1)),
        BitFn::Xor => (min_xor(a0, a1, c0, c1), max_xor(a0, a1, c0, c1)),
        BitFn::Left | BitFn::Right => unreachable!("a shift is not a bit by bit function"),
    }
}

fn min_or(mut a: u64, b: u64, mut c: u64, d: u64) -> u64 {
    let mut m = 1u64 << 63;
    while m != 0 {
        if !a & c & m != 0 {
            let t = (a | m) & m.wrapping_neg();
            if t <= b {
                a = t;
                break;
            }
        } else if a & !c & m != 0 {
            let t = (c | m) & m.wrapping_neg();
            if t <= d {
                c = t;
                break;
            }
        }
        m >>= 1;
    }
    a | c
}

fn max_or(a: u64, mut b: u64, c: u64, mut d: u64) -> u64 {
    let mut m = 1u64 << 63;
    while m != 0 {
        if b & d & m != 0 {
            let t = (b - m) | (m - 1);
            if t >= a {
                b = t;
                break;
            }
            let t = (d - m) | (m - 1);
            if t >= c {
                d = t;
                break;
            }
        }
        m >>= 1;
    }
    b | d
}

fn min_and(mut a: u64, b: u64, mut c: u64, d: u64) -> u64 {
    let mut m = 1u64 << 63;
    while m != 0 {
        if !a & !c & m != 0 {
            let t = (a | m) & m.wrapping_neg();
            if t <= b {
                a = t;
                break;
            }
            let t = (c | m) & m.wrapping_neg();
            if t <= d {
                c = t;
                break;
            }
        }
        m >>= 1;
    }
    a & c
}

fn max_and(a: u64, mut b: u64, c: u64, mut d: u64) -> u64 {
    let mut m = 1u64 << 63;
    while m != 0 {
        if b & !d & m != 0 {
            let t = (b & !m) | (m - 1);
            if t >= a {
                b = t;
                break;
            }
        } else if !b & d & m != 0 {
            let t = (d & !m) | (m - 1);
            if t >= c {
                d = t;
                break;
            }
        }
        m >>= 1;
    }
    b & d
}

fn min_xor(mut a: u64, b: u64, mut c: u64, d: u64) -> u64 {
    let mut m = 1u64 << 63;
    while m != 0 {
        if !a & c & m != 0 {
            let t = (a | m) & m.wrapping_neg();
            if t <= b {
                a = t;
            }
        } else if a & !c & m != 0 {
            let t = (c | m) & m.wrapping_neg();
            if t <= d {
                c = t;
            }
        }
        m >>= 1;
    }
    a ^ c
}

fn max_xor(a: u64, mut b: u64, c: u64, mut d: u64) -> u64 {
    let mut m = 1u64 << 63;
    while m != 0 {
        if b & d & m != 0 {
            let t = (b - m) | (m - 1);
            if t >= a {
                b = t;
            } else {
                let t = (d - m) | (m - 1);
                if t >= c {
                    d = t;
                }
            }
        }
        m >>= 1;
    }
    b ^ d
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
    /// `wrapping_narrow`, which keeps the low bits.
    Wrapping,
    /// `clamp`, which gives the nearest value the type holds.
    Saturating,
    /// `narrow`, which gives `.none` where the value does not fit.
    Checked,
}

impl Narrowing {
    pub(super) fn name(self) -> &'static str {
        match self {
            Narrowing::Wrapping => "wrapping_narrow",
            Narrowing::Saturating => "clamp",
            Narrowing::Checked => "narrow",
        }
    }
}

/// Which narrowing a call is of, if it is of one the compiler builds, which
/// no function of the program takes the name of.
pub(super) fn narrowing(defs: &Defs, call: &Call) -> Option<Narrowing> {
    let named = [
        Narrowing::Wrapping,
        Narrowing::Saturating,
        Narrowing::Checked,
    ]
    .into_iter()
    .find(|n| n.name() == call.callee)?;
    let (at, name) = (call.name_pos, call.callee.as_str());
    let taken = defs.function(at, name).is_some()
        || defs.struct_at(at, name).is_some()
        || defs.trait_fns_at(at, name).is_some();
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
        // A literal beside a number of a range is the range that holds it
        // alone, where the exact range of the result fits a type, and takes
        // the type of the number where it does not.
        let (l, r) = match (flexible(left), flexible(right)) {
            (true, false) => {
                let r = self.expr(body, right, &Want::Any);
                let side = self.literal_want(op, (left, &r), false);
                (self.expr(body, left, &side), r)
            }
            (false, true) => {
                let l = self.expr(body, left, &Want::Any);
                let side = self.literal_want(op, (right, &l), true);
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
            (Ty::Int(a), Ty::Int(b)) if let Some(exact) = exact(op, *a, *b) => {
                // The result is the exact range, which no proof is asked of,
                // and each side is the number at the size that range is kept at.
                for (side, own) in [(left, *a), (right, *b)] {
                    if own != exact {
                        self.widened.insert(addr(side), exact);
                    }
                }
                return Ty::Int(exact);
            }
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

    /// Checks the measure `e` of a `decreases`, which is an expression of whole
    /// numbers of any types, worked out as an exact number that is never
    /// computed: so no `+`, `-`, or `*` in it is asked to fit a type. The
    /// type given is that of a whole number, where it is one.
    pub(super) fn measure_type(&mut self, body: &mut Body<'a>, e: &Expr) -> Ty {
        let Expr::Arith { left, right, .. } = e else {
            return self.expr(body, e, &Want::Any);
        };
        let (l, r) = (
            self.measure_type(body, left),
            self.measure_type(body, right),
        );
        let ty = match (l, r) {
            (Ty::Unknown, _) | (_, Ty::Unknown) => Ty::Unknown,
            (Ty::Int(a), Ty::Int(b)) => Ty::Int(a.hull(b).unwrap_or(Int::U64)),
            (Ty::Flex(k), _) | (_, Ty::Flex(k)) => Ty::Flex(k),
            (other, _) => other,
        };
        self.types.insert(addr(e), ty.clone());
        ty
    }

    /// The type of a binding written `a..b` where a bound is a name, which is
    /// the smallest range that holds every number the range can have, and
    /// each name with whether it is the end. None, with the mistake reported,
    /// where a name is not one that cannot change.
    pub(super) fn named_range(
        &mut self,
        body: &Body<'a>,
        t: &Type,
    ) -> Option<(Ty, Vec<(String, bool)>)> {
        let Some((first, end)) = &t.range else {
            return None;
        };
        let mut ends = [0i128; 2];
        let mut named = Vec::new();
        for (at, (bound, is_end)) in [(first, false), (end, true)].into_iter().enumerate() {
            ends[at] = match bound {
                Bound::Number(n) => *n,
                Bound::Size(name) => {
                    let found = body
                        .names
                        .iter()
                        .rfind(|(n, _)| n == name)
                        .map(|(_, ty)| ty);
                    match found {
                        None => {
                            self.errors.push(
                                Error::new(
                                    t.pos,
                                    format!(
                                        "there is no name `{name}` defined before this, and a bound is one"
                                    ),
                                )
                                .with_fix("write the bound as a number, or bind the name first"),
                            );
                            return None;
                        }
                        Some(_) if body.vars.contains(name) => {
                            self.errors.push(
                                Error::new(
                                    t.pos,
                                    format!("`{name}` is a `var`, which can change, so its size is not a bound"),
                                )
                                .with_fix("bind its value to a name with `=`, and write the name"),
                            );
                            return None;
                        }
                        Some(Ty::Text) => {
                            self.errors.push(Error::unsupported(
                                t.pos,
                                "the `size` of a `str` as a bound",
                            ));
                            return None;
                        }
                        Some(Ty::List(_) | Ty::Slice(_)) => {
                            named.push((format!("{name}.size"), is_end));
                            if is_end { i128::from(u64::MAX) } else { 0 }
                        }
                        Some(Ty::Unknown) => return None,
                        Some(_) => {
                            self.errors.push(Error::new(
                                t.pos,
                                format!(
                                    "`{name}` is not a list, a slice, or a str, and the `size` of one is a bound"
                                ),
                            ));
                            return None;
                        }
                    }
                }
                Bound::Name(name) => {
                    let found = body
                        .names
                        .iter()
                        .rfind(|(n, _)| n == name)
                        .map(|(_, ty)| ty);
                    match found {
                        None => {
                            self.errors.push(
                                Error::new(
                                    t.pos,
                                    format!(
                                        "there is no name `{name}` defined before this, and a bound is one"
                                    ),
                                )
                                .with_fix("write the bound as a number, or bind the name first"),
                            );
                            return None;
                        }
                        Some(_) if body.vars.contains(name) => {
                            self.errors.push(
                                Error::new(
                                    t.pos,
                                    format!("`{name}` is a `var`, which can change, so it is not a bound"),
                                )
                                .with_fix("bind its value to a name with `=`, and write the name"),
                            );
                            return None;
                        }
                        Some(Ty::Int(int)) => {
                            named.push((name.clone(), is_end));
                            if is_end { int.max() } else { int.min() }
                        }
                        Some(Ty::Unknown) => return None,
                        Some(_) => {
                            self.errors.push(Error::new(
                                t.pos,
                                format!("`{name}` is not a whole number, and a bound is one"),
                            ));
                            return None;
                        }
                    }
                }
            };
        }
        let [min, end] = ends;
        match Int::between(min, (end - 1).max(min - 1)) {
            Some(int) => Some((Ty::Int(int), named)),
            None => {
                self.errors.push(Error::new(
                    t.pos,
                    format!(
                        "`{}` needs more than 64 bits, which is not built yet",
                        t.written()
                    ),
                ));
                None
            }
        }
    }

    /// What is wanted of the operand `side`, which is flexible, beside `other`,
    /// the type of the other operand, which is on the left when `other_left`.
    fn literal_want(&self, op: ArithOp, side: (&Expr, &Ty), other_left: bool) -> Want {
        let (literal, other) = side;
        if let (
            Expr::Number {
                magnitude,
                negative,
                ..
            },
            Ty::Int(own),
        ) = (literal, other)
            && *magnitude <= i64::MAX as u128
        {
            let value = if *negative {
                -(*magnitude as i128)
            } else {
                *magnitude as i128
            };
            if let Some(alone) = Int::between(value, value) {
                let fits = if other_left {
                    exact(op, *own, alone)
                } else {
                    exact(op, alone, *own)
                };
                if fits.is_some() {
                    return Want::Ty(Ty::Int(alone));
                }
            }
        }
        Self::side_want(other)
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
        let value = if negative {
            -(magnitude as i128)
        } else {
            magnitude as i128
        };
        let fits = int.min() <= value && value <= int.max();
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
        let by = (params_of(&sig.params, &call.args).into_iter().flatten())
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

    /// Checks a call of `bit_and`, `bit_or`, `bit_xor`, `shift_left`, or
    /// `shift_right` of `Bits`, which gives the exact range of what it makes
    /// of numbers that are not negative, and takes the range that fills a size
    /// of numbers that may be negative. None, before anything is checked,
    /// where the arguments are not two, written by position.
    pub(super) fn bits_exact(
        &mut self,
        body: &mut Body<'a>,
        call: &Call,
        (op, k, fi): (BitFn, usize, usize),
    ) -> Option<Ty> {
        let defs = self.defs;
        if call.args.len() != 2 || call.args.iter().any(|a| a.name.is_some() || a.after) {
            return None;
        }
        let name = call.callee.as_str();
        let shifts = matches!(op, BitFn::Left | BitFn::Right);
        let (first, second) = (&call.args[0].value, &call.args[1].value);
        // A literal beside a number that is not negative is the range that
        // holds it alone, and beside any other number the type of it.
        let alone = |e: &Expr| match e {
            Expr::Number {
                magnitude,
                negative: false,
                ..
            } if *magnitude <= i64::MAX as u128 => {
                let n = *magnitude as i128;
                Int::between(n, n).map_or(Want::Any, |t| Want::Ty(Ty::Int(t)))
            }
            _ => Want::Any,
        };
        let beside = |lit: &Expr, other: &Ty| match other {
            Ty::Int(o) if o.min() >= 0 => match alone(lit) {
                Want::Any => Self::side_want(other),
                w => w,
            },
            _ => Self::side_want(other),
        };
        let (ta, tb) = if shifts {
            let ta = self.expr(body, first, &alone(first));
            let by = if flexible(second) {
                match alone(second) {
                    Want::Any => Want::Ty(Ty::Int(Int::U8)),
                    w => w,
                }
            } else {
                Want::Any
            };
            (ta, self.expr(body, second, &by))
        } else {
            match (flexible(first), flexible(second)) {
                (true, false) => {
                    let b = self.expr(body, second, &Want::Any);
                    let want = beside(first, &b);
                    (self.expr(body, first, &want), b)
                }
                (false, true) => {
                    let a = self.expr(body, first, &Want::Any);
                    let want = beside(second, &a);
                    let b = self.expr(body, second, &want);
                    (a, b)
                }
                (true, true) => (
                    self.expr(body, first, &alone(first)),
                    self.expr(body, second, &alone(second)),
                ),
                (false, false) => (
                    self.expr(body, first, &Want::Any),
                    self.expr(body, second, &Want::Any),
                ),
            }
        };
        let (a, b) = match (&ta, &tb) {
            (Ty::Unknown, _) | (_, Ty::Unknown) => return Some(Ty::Unknown),
            (Ty::Int(a), Ty::Int(b)) => (*a, *b),
            (Ty::Flex(_) | Ty::Int(_), Ty::Flex(_) | Ty::Int(_)) if !shifts => {
                use_at(call.pos);
                if !flex_pair(&ta, &tb) {
                    unreachable!("a number and a number")
                }
                return Some(if matches!(ta, Ty::Flex(_)) { tb } else { ta });
            }
            (Ty::Flex(_), _) => {
                use_at(call.pos);
                return Some(ta);
            }
            _ => {
                // Not whole numbers: what the trait asks of the type.
                let types = [ta.clone()];
                if ta != tb {
                    let (x, y) = (
                        defs.describe(&ta, body.scope),
                        defs.describe(&tb, body.scope),
                    );
                    self.errors.push(Error::new(
                        call.pos,
                        format!("`{name}` takes two values of one type, and these are {x} and {y}"),
                    ));
                    return Some(Ty::Unknown);
                }
                return match defs.find(k, &types, body.scope, &mut Vec::new()) {
                    Found::Yes(..) => Some(ta),
                    Found::No => {
                        self.errors
                            .push(self.no_implementation(body, call, k, &types));
                        Some(Ty::Unknown)
                    }
                    Found::Two(x, y) => {
                        self.errors.push(defs.ambiguous(call.pos, x, y));
                        Some(Ty::Unknown)
                    }
                    Found::Unknown => Some(Ty::Unknown),
                };
            }
        };
        let sig = &defs.trait_sigs[k][fi];
        let written = format!("{name}({}, {})", written_expr(first), written_expr(second));
        if shifts && !Int::U8.holds(b) {
            // The amount is a `Byte`, which it enters where `arith` shows it fits.
            self.enters(body, second, (b, Int::U8));
        }
        let negative = a.min() < 0 || (!shifts && b.min() < 0);
        if negative {
            if (!shifts && a != b) || !Int::ALL.contains(&a) {
                let message = if !shifts && a != b {
                    format!(
                        "`{name}` of a number that may be negative takes two numbers of one \
                         range, and these are `{}` and `{}`",
                        defs.spelling(a),
                        defs.spelling(b)
                    )
                } else {
                    format!(
                        "`{name}` of a number that may be negative takes a range that fills \
                         a size, and `{}` does not",
                        defs.spelling(a)
                    )
                };
                self.errors.push(
                    Error::new(call.pos, message)
                        .with_fix("bind the number to a type that fills a size, as `i32` does"),
                );
                return Some(Ty::Unknown);
            }
            let mistakes = self.errors.len();
            if shifts {
                self.shifts(body, second, a, call.pos);
            }
            if shifts && op == BitFn::Left && self.errors.len() == mistakes {
                let most = self.amount_at_most(body, second, b);
                if !(self.proves_product_fits(body, first, 1i128 << most, a)) {
                    self.errors.push(
                        Error::new(
                            call.pos,
                            format!(
                                "`{written}` may drop a bit, as `shift_left` drops none, and it \
                                 does not fit a `{}`",
                                defs.spelling(a)
                            ),
                        )
                        .with_fix("use `wrapping_shift_left` where the bits that leave the top are meant to go"),
                    );
                }
            }
            body.facts.note_bit_call(call, sig, name, a);
            return Some(Ty::Int(a));
        }
        let result = match op {
            BitFn::Right => {
                self.amount_below(body, second, 64, "a shift", call.pos);
                let (lo, hi) = (b.min().clamp(0, 63), b.max().clamp(0, 63));
                Int::between(a.min() >> hi, a.max() >> lo)
            }
            BitFn::Left => {
                let mistakes = self.errors.len();
                self.amount_below(body, second, 64, "a shift", call.pos);
                let most = if self.errors.len() == mistakes {
                    self.amount_at_most(body, second, b)
                } else {
                    63
                };
                let (lo, hi) = (
                    a.min().checked_mul(1i128 << b.min().clamp(0, 63)),
                    a.max().checked_mul(1i128 << most),
                );
                match lo.zip(hi).and_then(|(lo, hi)| Int::between(lo, hi)) {
                    Some(r) => Some(r),
                    // No type holds the exact range, so the result is the type
                    // of the number, as a product's is, where `arith` proves
                    // that it fits.
                    None => {
                        if self.errors.len() == mistakes
                            && !self.proves_product_fits(body, first, 1i128 << most, a)
                        {
                            self.errors.push(
                                Error::new(
                                    call.pos,
                                    format!(
                                        "cannot prove that `{written}` fits `{}`, as the number \
                                         times two to the amount may be more",
                                        defs.spelling(a)
                                    ),
                                )
                                .with_fix(
                                    "check the number and the amount first, or use `wrapping_shift_left`",
                                ),
                            );
                        }
                        body.facts.note_bit_call(call, sig, name, a);
                        return Some(Ty::Int(a));
                    }
                }
            }
            _ => {
                let ends = |t: Int| (t.min() as u64, t.max() as u64);
                let (lo, hi) = bit_bounds(op, (ends(a), ends(b)));
                Int::between(i128::from(lo), i128::from(hi))
            }
        };
        let result = result.expect("a range of numbers that are not negative");
        // Each side is the number at the size the work is done at.
        let sides: [(&Expr, Int); 2] = [(first, a), (second, b)];
        for (side, own) in sides {
            let at = match op {
                BitFn::Left if std::ptr::eq(side, first) => result,
                BitFn::Left | BitFn::Right if std::ptr::eq(side, second) => continue,
                BitFn::Right => own,
                _ => a.hull(b).expect("a range"),
            };
            if own != at {
                self.widened.insert(addr(side), at);
            }
        }
        body.facts.note_bit_call(call, sig, name, result);
        Some(Ty::Int(result))
    }

    /// The greatest amount that `by` is proven to be, of at most 63, which is
    /// the greatest of its type where nothing proves less.
    fn amount_at_most(&mut self, body: &mut Body<'a>, by: &Expr, ty: Int) -> u32 {
        if let Expr::Number { magnitude, .. } = by {
            return (*magnitude).min(63) as u32;
        }
        let (mut low, mut high) = (ty.min().clamp(0, 63), ty.max().clamp(0, 63));
        while low < high {
            let mid = low + (high - low) / 2;
            if self.proves_at_most(body, by, mid) {
                high = mid;
            } else {
                low = mid + 1;
            }
        }
        high as u32
    }

    /// Checks `narrow(x)` or one of its relatives, which take the whole
    /// number `x` to the whole number type that is expected, one that does
    /// not hold all the values of the type of `x`. `narrow` is expected to
    /// give an `Option` of it.
    pub(super) fn narrow(
        &mut self,
        body: &mut Body<'a>,
        call: &Call,
        narrowing: Narrowing,
        want: &Want,
    ) -> Ty {
        let defs = self.defs;
        let name = call.callee.as_str();
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
            "narrow" => "write the type, as in `y: Option[u8] = narrow(x)`",
            _ => "write the type, as in `y: u8 = clamp(x)`",
        };
        self.errors
            .push(Error::new(call.pos, message).with_fix(fix));
        Ty::Unknown
    }

    /// What a narrowing gives, in words.
    pub(super) fn narrowed(&self, name: &str) -> &'static str {
        match name {
            "narrow" => "an `Option` of a whole number",
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
