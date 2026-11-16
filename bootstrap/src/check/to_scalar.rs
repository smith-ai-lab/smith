//! Lowers what `main` reaches to the scalar form, from the checked program,
//! where every value's type is known.
//!
//! Every value is its Smith type's scalars, in order: a text is an address
//! and a length, a struct and a tuple are their parts one after another,
//! and an enum is the number of its case in one byte and then places that
//! its cases share by type. For each scalar type the enum has as many
//! places of that type as the case that needs the most of them, and the
//! k-th scalar of a type of a case's parts, which are split into scalars in
//! the order written, goes to the k-th place of that type. The types are
//! taken in the order they first appear among the cases' scalars, cases in
//! order. A place that the case a value holds does not use holds the zero of
//! its type, and nothing reads it.
//!
//! A number is one scalar of its type's size: 8, 16, 32, or 64 bits, with no
//! sign, since whether it is signed is in the operation that reads it. A
//! struct, a tuple, or an enum with a number in it follows the rules above,
//! the number being one more scalar type when an enum's places are counted.
//!
//! Text built at run time, a number written as its digits, and a comparison
//! of two texts are calls of the functions of `scalar::runtime`, which a
//! program holds only when it reaches them.

use super::*;
use crate::scalar as form;
use crate::scalar::runtime::{self, Runtime};

const BYTE: form::Type = form::Type::Int(form::Bits::B8);
/// What a text is.
const TEXT: [form::Type; 2] = [form::Type::Address, form::Type::Int(form::Bits::B64)];

/// How many bits a number of the type has, in the scalar form.
fn bits_of(t: Int) -> form::Bits {
    match t.bits() {
        8 => form::Bits::B8,
        16 => form::Bits::B16,
        32 => form::Bits::B32,
        _ => form::Bits::B64,
    }
}

/// The program that `start` runs, in the scalar form.
pub(super) fn lower(typed: &Typed, start: Instance) -> form::Program {
    let mut lowering = Lowering {
        typed,
        bodies: vec![Body::Copy(start)],
        data: Vec::new(),
    };
    let mut functions = Vec::new();
    while functions.len() < lowering.bodies.len() {
        let body = lowering.bodies[functions.len()].clone();
        functions.push(match body {
            Body::Copy(instance) => lowering.function(&instance),
            Body::Runtime(kind) => runtime::build(kind, &mut |k| lowering.place(Body::Runtime(k))),
        });
    }
    form::Program {
        data: lowering.data,
        functions,
        main: 0,
    }
}

/// A function of the program: a copy of one the source defines, or one the
/// program needs at run time, which the scalar form holds once.
#[derive(Clone, PartialEq)]
enum Body {
    Copy(Instance),
    Runtime(Runtime),
}

struct Lowering<'a> {
    typed: &'a Typed<'a>,
    /// The functions, each at its place in the program, in the order they
    /// are first called.
    bodies: Vec<Body>,
    data: Vec<Vec<u8>>,
}

/// A function being lowered.
struct Out<'a> {
    /// The names defined so far in the function, which is the first frame,
    /// and in each function lowered in place inside it, one frame each.
    frames: Vec<Vec<Bound<'a>>>,
    /// The frame of the code being lowered.
    at: usize,
    types: Vec<form::Type>,
    blocks: Vec<form::Block>,
    /// The block being filled.
    current: usize,
}

/// A name being lowered: what it gives, the values it stands for, and the
/// function it holds, if it holds one.
#[derive(Clone)]
struct Bound<'a> {
    name: String,
    ty: Ty,
    values: Vec<form::Value>,
    func: Option<Func<'a>>,
}

/// The function a name holds.
#[derive(Clone)]
enum Func<'a> {
    /// A lambda, the frame it is written in, and the types that stand for
    /// the type parameters of the definition it is written in.
    Lambda(&'a Lambda, usize, Vec<Ty>),
    /// A copy of a function with a name.
    Named(Instance),
}

impl<'a> Out<'a> {
    fn new() -> Self {
        let mut out = Out {
            frames: vec![Vec::new()],
            at: 0,
            types: Vec::new(),
            blocks: Vec::new(),
            current: 0,
        };
        out.new_block();
        out
    }

    fn names(&self) -> &Vec<Bound<'a>> {
        &self.frames[self.at]
    }

    fn names_mut(&mut self) -> &mut Vec<Bound<'a>> {
        &mut self.frames[self.at]
    }

    /// The place of a name among those of the frame being lowered.
    fn find(&self, name: &str) -> Option<usize> {
        self.names().iter().rposition(|l| l.name == name)
    }

    /// Defines a name that stands for values.
    fn define(&mut self, name: &str, ty: Ty, values: Vec<form::Value>) {
        self.names_mut().push(Bound {
            name: name.to_string(),
            ty,
            values,
            func: None,
        });
    }

    fn new_block(&mut self) -> usize {
        self.blocks.push(form::Block {
            params: Vec::new(),
            instructions: Vec::new(),
            end: form::End::Return(Vec::new()),
        });
        self.blocks.len() - 1
    }

    fn fresh(&mut self, ty: form::Type) -> form::Value {
        self.types.push(ty);
        form::Value(self.types.len() - 1)
    }

    /// Adds an instruction to the block being filled, and gives its results.
    fn emit(&mut self, op: form::Op, results: Vec<form::Type>) -> Vec<form::Value> {
        let results: Vec<form::Value> = results.into_iter().map(|t| self.fresh(t)).collect();
        self.blocks[self.current]
            .instructions
            .push(form::Instruction {
                results: results.clone(),
                op,
            });
        results
    }

    fn constant(&mut self, constant: form::Constant, ty: form::Type) -> form::Value {
        self.emit(form::Op::Constant(constant), vec![ty])[0]
    }

    /// A constant integer of the size, which stands for the number, as its
    /// bits in two's complement when it is negative.
    fn int(&mut self, bits: form::Bits, number: i128) -> form::Value {
        let mask = if bits == form::Bits::B64 {
            u64::MAX
        } else {
            (1 << bits.count()) - 1
        };
        let n = number as u64 & mask;
        self.constant(form::Constant::Int(n), form::Type::Int(bits))
    }

    fn binary(&mut self, op: form::Binary, a: form::Value, b: form::Value) -> form::Value {
        let ty = self.types[a.0];
        self.emit(form::Op::Binary { op, a, b }, vec![ty])[0]
    }

    /// Whether `a` and `b` stand in the relation, as a `bool`: 1 if they do
    /// and 0 if they do not, which are the numbers of its cases.
    fn compare(&mut self, op: form::Compare, a: form::Value, b: form::Value) -> form::Value {
        let ty = form::Type::Int(form::Bits::B8);
        self.emit(form::Op::Compare { op, a, b }, vec![ty])[0]
    }

    fn widen(&mut self, signed: bool, value: form::Value) -> form::Value {
        let ty = form::Type::Int(form::Bits::B64);
        self.emit(form::Op::Widen { signed, value }, vec![ty])[0]
    }

    fn narrow(&mut self, value: form::Value, to: form::Bits) -> form::Value {
        self.emit(form::Op::Narrow(value), vec![form::Type::Int(to)])[0]
    }

    fn choose(
        &mut self,
        on: form::Value,
        then: form::Value,
        otherwise: form::Value,
    ) -> form::Value {
        let ty = self.types[then.0];
        let op = form::Op::Choose {
            on,
            then,
            otherwise,
        };
        self.emit(op, vec![ty])[0]
    }

    /// The value that fills a place nothing reads.
    fn zero(&mut self, ty: form::Type) -> form::Value {
        match ty {
            form::Type::Address => self.constant(form::Constant::Null, ty),
            form::Type::Int(_) => self.constant(form::Constant::Int(0), ty),
        }
    }

    /// New parameters of the first block, for a function's parameter.
    fn params(&mut self, types: &[form::Type]) -> Vec<form::Value> {
        let values: Vec<form::Value> = types.iter().map(|t| self.fresh(*t)).collect();
        self.blocks[0].params.extend(values.iter().copied());
        values
    }

    /// Ends the block being filled by choosing between new blocks, one for
    /// each case of the integer `on`, and gives them.
    fn switch(&mut self, on: form::Value, cases: usize) -> Vec<usize> {
        let blocks: Vec<usize> = (0..cases).map(|_| self.new_block()).collect();
        let jumps = blocks
            .iter()
            .map(|&block| form::Jump {
                block,
                args: Vec::new(),
            })
            .collect();
        self.blocks[self.current].end = form::End::Switch { on, jumps };
        blocks
    }

    /// Makes a block where the blocks that end each of `ends`, which give it
    /// values, meet, and gives its parameters. It is then the block being
    /// filled.
    fn join(
        &mut self,
        types: &[form::Type],
        ends: Vec<(usize, Vec<form::Value>)>,
    ) -> Vec<form::Value> {
        let join = self.new_block();
        let params: Vec<form::Value> = types.iter().map(|t| self.fresh(*t)).collect();
        self.blocks[join].params = params.clone();
        for (block, args) in ends {
            self.blocks[block].end = form::End::Jump(form::Jump { block: join, args });
        }
        self.current = join;
        params
    }
}

/// How an enum holds its cases' parts: its places after the case number,
/// and, for each case, the place of each scalar of what it holds.
struct Layout {
    places: Vec<form::Type>,
    cases: Vec<Vec<usize>>,
}

/// Where each block of a `match` ends, and what the names defined before it
/// were then.
struct Leaf {
    block: usize,
    gives: Vec<form::Value>,
    names: Vec<Vec<Vec<form::Value>>>,
}

/// Patterns still to be matched, one for each value, where `None` matches
/// anything.
type Patterns<'p> = Vec<Option<&'p Pattern>>;

/// A pattern still to be matched: the block it leads to, what is left of the
/// pattern, one for each value still to look at, and the names bound so far,
/// each with its type and its values.
#[derive(Clone)]
struct Row<'p> {
    arm: usize,
    patterns: Patterns<'p>,
    binds: Vec<(String, Ty, Vec<form::Value>)>,
}

/// What every block of a `match` being lowered shares: its blocks, the types
/// that stand for the type parameters, the types of what it gives, and how
/// many names each frame held before it.
struct Arms<'a, 'p> {
    arms: &'a [Arm],
    env: &'p [Ty],
    types: Vec<form::Type>,
    before: Vec<usize>,
}

impl<'a> Lowering<'a> {
    fn text(&mut self, out: &mut Out<'a>, text: &[u8]) -> Vec<form::Value> {
        let at = match self.data.iter().position(|t| t == text) {
            Some(at) => at,
            None => {
                self.data.push(text.to_vec());
                self.data.len() - 1
            }
        };
        let address = out.constant(form::Constant::Data(at), form::Type::Address);
        let length = out.constant(
            form::Constant::Int(text.len() as u64),
            form::Type::Int(form::Bits::B64),
        );
        vec![address, length]
    }

    /// The types of the scalars a value of the type is lowered to: an
    /// address and a length for each text, those of each part for a struct
    /// and each element for a tuple, for an enum the number of its case and
    /// its places, and none for a function, whose calls are lowered in
    /// place.
    fn scalars(&self, ty: &Ty) -> Vec<form::Type> {
        let defs = self.typed.defs;
        match ty {
            Ty::Text => vec![form::Type::Address, form::Type::Int(form::Bits::B64)],
            Ty::Int(t) => vec![form::Type::Int(bits_of(*t))],
            Ty::Struct(s, args) => {
                let mut all = Vec::new();
                for f in &defs.fields[*s] {
                    all.extend(self.scalars(&f.subst(args)));
                }
                all
            }
            Ty::Tuple(items) => {
                let mut all = Vec::new();
                for t in items {
                    all.extend(self.scalars(t));
                }
                all
            }
            Ty::Enum(e, args) => {
                let mut all = vec![form::Type::Int(form::Bits::B8)];
                all.extend(self.layout(*e, args).places);
                all
            }
            Ty::Param(_) | Ty::Fn(_) | Ty::Nothing | Ty::Unknown | Ty::Flex(_) => Vec::new(),
        }
    }

    /// How many scalars a value of the type is lowered to.
    fn size(&self, ty: &Ty) -> usize {
        self.scalars(ty).len()
    }

    /// The places of an enum, as the rule in the head of this file gives.
    fn layout(&self, e: usize, args: &[Ty]) -> Layout {
        let defs = self.typed.defs;
        let mut held = Vec::new();
        for t in &defs.cases[e] {
            held.push(match t {
                Some(t) => self.scalars(&t.subst(args)),
                None => Vec::new(),
            });
        }
        // Each scalar type in the order it first appears, with the most
        // places of it that a case needs.
        let mut kinds: Vec<(form::Type, usize)> = Vec::new();
        for scalars in &held {
            let mut counts: Vec<(form::Type, usize)> = Vec::new();
            for s in scalars {
                match counts.iter_mut().find(|(t, _)| t == s) {
                    Some((_, n)) => *n += 1,
                    None => counts.push((*s, 1)),
                }
            }
            for (t, n) in counts {
                match kinds.iter_mut().find(|(k, _)| *k == t) {
                    Some((_, most)) => *most = (*most).max(n),
                    None => kinds.push((t, n)),
                }
            }
        }
        let mut places = Vec::new();
        let mut start = Vec::new();
        for (t, n) in &kinds {
            start.push(places.len());
            places.extend(std::iter::repeat_n(*t, *n));
        }
        let cases = held
            .iter()
            .map(|scalars| {
                let mut used = vec![0; kinds.len()];
                scalars
                    .iter()
                    .map(|s| {
                        let k = kinds.iter().position(|(t, _)| t == s).expect("a kind");
                        used[k] += 1;
                        start[k] + used[k] - 1
                    })
                    .collect()
            })
            .collect();
        Layout { places, cases }
    }

    /// The values of each element of a tuple of the types `items`, from the
    /// values of the tuple.
    fn elements(&self, items: &[Ty], values: &[form::Value]) -> Vec<Vec<form::Value>> {
        let mut at = 0;
        let mut all = Vec::new();
        for t in items {
            let size = self.size(t);
            at += size;
            all.push(values[at - size..at].to_vec());
        }
        all
    }

    /// Makes each bare name in a pattern of a binding stand for the values
    /// it matches: a name defined before it, which is a `var`, from then on,
    /// and any other as a new name.
    fn bind(&self, out: &mut Out<'a>, pattern: &Pattern, ty: &Ty, values: Vec<form::Value>) {
        match pattern {
            Pattern::Bind { name, .. } => match out.find(name) {
                Some(at) => out.names_mut()[at].values = values,
                None => out.define(name, ty.clone(), values),
            },
            Pattern::Tuple { items, .. } => {
                let Ty::Tuple(of) = ty else {
                    unreachable!("checked")
                };
                for ((item, t), values) in items.iter().zip(of).zip(self.elements(of, &values)) {
                    self.bind(out, item, t, values);
                }
            }
            Pattern::Case { .. } => unreachable!("checked"),
        }
    }

    /// Where the values of a field, or of a field of a field for each
    /// further name, are among the values of a value of the type, and how
    /// many there are.
    fn offset(&self, ty: &Ty, fields: &[(String, Pos)]) -> (usize, usize) {
        let mut ty = ty.clone();
        let mut start = 0;
        for (name, _) in fields {
            let Ty::Struct(s, args) = ty else {
                unreachable!("checked")
            };
            let defs = self.typed.defs;
            let mut found = None;
            for (field, t) in defs.file.structs[s].fields.iter().zip(&defs.fields[s]) {
                let t = t.subst(&args);
                if &field.name == name {
                    found = Some(t);
                    break;
                }
                start += self.size(&t);
            }
            ty = found.expect("checked");
        }
        (start, self.size(&ty))
    }

    /// The place of a copy in the program, which it takes if it has none.
    fn place(&mut self, body: Body) -> usize {
        match self.bodies.iter().position(|b| *b == body) {
            Some(at) => at,
            None => {
                self.bodies.push(body);
                self.bodies.len() - 1
            }
        }
    }

    /// Calls the function of the runtime, which gives the values it gives.
    fn call_runtime(
        &mut self,
        out: &mut Out<'a>,
        kind: Runtime,
        args: Vec<form::Value>,
        results: Vec<form::Type>,
    ) -> Vec<form::Value> {
        let function = self.place(Body::Runtime(kind));
        out.emit(form::Op::Call { function, args }, results)
    }

    fn function(&mut self, instance: &Instance) -> form::Function {
        let defs = self.typed.defs;
        if let Template::Given(k) = instance.template {
            return self.given(k, &instance.args[0]);
        }
        let view = defs.view(instance.template);
        let mut out = Out::new();
        for (name, ty) in &view.params {
            let ty = ty.subst(&instance.args);
            let types = self.scalars(&ty);
            let values = out.params(&types);
            out.define(name, ty, values);
        }
        let mut result = self.block(&mut out, &view.f.body, &instance.args);
        if view.gives == Ty::Nothing {
            result.clear();
        }
        let results = self.scalars(&view.gives.subst(&instance.args));
        out.blocks[out.current].end = form::End::Return(result);
        form::Function {
            types: out.types,
            results,
            blocks: out.blocks,
        }
    }

    /// What the function the compiler builds for an operation on numbers of
    /// type `t` gives for the values `args`: whether they are equal, how
    /// they are ordered, or the result of `wrapping_`, `saturating_`, or
    /// `checked_` `add`, `sub`, or `mul`.
    fn intrinsic(
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
    fn arithmetic(
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

    /// The function of the implementation of trait `k` given to `ty`. `Fmt`
    /// gives what the type's `Repr` gives, `Repr` writes the type's value as
    /// the source that builds it, with each part written by its own `Repr`,
    /// and `Eq` says whether two values are the same case with equal parts,
    /// by each part's own `Eq`.
    fn given(&mut self, k: usize, ty: &Ty) -> form::Function {
        let defs = self.typed.defs;
        let mut out = Out::new();
        let types = self.scalars(ty);
        let a = out.params(&types);
        let (result, results) = if k == defs.fmt {
            let text = self.run(&mut out, defs.repr, ty, a);
            (text, self.scalars(&Ty::Text))
        } else if k == defs.repr {
            (self.repr(&mut out, ty, &a), self.scalars(&Ty::Text))
        } else {
            let b = out.params(&types);
            let same = self.equal(&mut out, ty, &a, &b);
            (vec![same], self.scalars(&defs.bool_ty()))
        };
        out.blocks[out.current].end = form::End::Return(result);
        form::Function {
            types: out.types,
            results,
            blocks: out.blocks,
        }
    }

    /// Runs the function of trait `k`, which gives text or a `bool`, for
    /// values of the type, and gives what it gives.
    fn run(
        &mut self,
        out: &mut Out<'a>,
        k: usize,
        ty: &Ty,
        args: Vec<form::Value>,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let (instance, _) = defs.chosen(k, 0, vec![ty.clone()]);
        let function = self.place(Body::Copy(instance));
        let types = self.scalars(&defs.trait_sigs[k][0].gives);
        out.emit(form::Op::Call { function, args }, types)
    }

    /// `true` or `false`.
    fn truth(&self, out: &mut Out<'a>, value: bool) -> form::Value {
        let defs = self.typed.defs;
        let name = if value { "true" } else { "false" };
        let case = defs.case_of(defs.boolean, name).expect("a case");
        out.constant(
            form::Constant::Int(case as u64),
            form::Type::Int(form::Bits::B8),
        )
    }

    /// Whether the values `a` and `b` of the type are equal, as a `bool`.
    fn equal(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        a: &[form::Value],
        b: &[form::Value],
    ) -> form::Value {
        let defs = self.typed.defs;
        let parts: Vec<Ty> = match ty {
            Ty::Text => {
                let args = [a, b].concat();
                let kind = Runtime::Equal;
                return self.call_runtime(out, kind, args, vec![BYTE])[0];
            }
            Ty::Enum(e, args) => {
                let layout = self.layout(*e, args);
                let bool_types = self.scalars(&defs.bool_ty());
                let holds: Vec<Option<Ty>> = (defs.cases[*e].iter())
                    .map(|t| t.as_ref().map(|t| t.subst(args)))
                    .collect();
                let mut ends = Vec::new();
                let on_a = out.switch(a[0], holds.len());
                for (k, held) in holds.iter().enumerate() {
                    out.current = on_a[k];
                    let on_b = out.switch(b[0], holds.len());
                    for (j, &block) in on_b.iter().enumerate() {
                        out.current = block;
                        let same = if j != k {
                            self.truth(out, false)
                        } else {
                            match held {
                                Some(t) => {
                                    let take = |values: &[form::Value]| -> Vec<form::Value> {
                                        layout.cases[k].iter().map(|&p| values[1 + p]).collect()
                                    };
                                    let args = [take(a), take(b)].concat();
                                    self.run(out, defs.eq, t, args)[0]
                                }
                                None => self.truth(out, true),
                            }
                        };
                        ends.push((out.current, vec![same]));
                    }
                }
                return out.join(&bool_types, ends)[0];
            }
            Ty::Struct(s, args) => defs.fields[*s].iter().map(|t| t.subst(args)).collect(),
            Ty::Tuple(of) => of.clone(),
            Ty::Nothing => Vec::new(),
            _ => unreachable!("a type given `Eq`"),
        };
        let mut at = 0;
        let mut pairs = Vec::new();
        for t in &parts {
            let size = self.size(t);
            pairs.push((t, a[at..at + size].to_vec(), b[at..at + size].to_vec()));
            at += size;
        }
        self.all_equal(out, &pairs)
    }

    /// Whether every pair is equal, each by its type's `Eq`, where the pairs
    /// after one run only when it is equal.
    fn all_equal(
        &mut self,
        out: &mut Out<'a>,
        pairs: &[(&Ty, Vec<form::Value>, Vec<form::Value>)],
    ) -> form::Value {
        let defs = self.typed.defs;
        let Some(((t, x, y), rest)) = pairs.split_first() else {
            return self.truth(out, true);
        };
        let same = self.run(out, defs.eq, t, [x.clone(), y.clone()].concat())[0];
        if rest.is_empty() {
            return same;
        }
        let types = self.scalars(&defs.bool_ty());
        let arms = out.switch(same, 2);
        let (no, yes) = (
            defs.case_of(defs.boolean, "false").expect("a case"),
            defs.case_of(defs.boolean, "true").expect("a case"),
        );
        out.current = arms[no];
        let false_ = self.truth(out, false);
        let mut ends = vec![(out.current, vec![false_])];
        out.current = arms[yes];
        let rest = self.all_equal(out, rest);
        ends.push((out.current, vec![rest]));
        out.join(&types, ends)[0]
    }

    /// The text `Repr` gives for the values of a value of the type, as
    /// the implementation it is given writes it.
    fn repr(&mut self, out: &mut Out<'a>, ty: &Ty, values: &[form::Value]) -> Vec<form::Value> {
        let defs = self.typed.defs;
        match ty {
            Ty::Nothing => self.text(out, b"()"),
            Ty::Text => self.call_runtime(out, Runtime::Escape, values.to_vec(), TEXT.to_vec()),
            Ty::Struct(s, args) => {
                let def = &defs.file.structs[*s];
                let mut at = 0;
                let mut items = Vec::new();
                for (field, t) in def.fields.iter().zip(&defs.fields[*s]) {
                    let t = t.subst(args);
                    let size = self.size(&t);
                    items.push((
                        format!("{}: ", field.name),
                        t,
                        values[at..at + size].to_vec(),
                    ));
                    at += size;
                }
                self.listed(out, &format!("{}(", def.name), items)
            }
            Ty::Tuple(of) => {
                let items = (of.iter().zip(self.elements(of, values)))
                    .map(|(t, values)| (String::new(), t.clone(), values))
                    .collect();
                self.listed(out, "(", items)
            }
            Ty::Enum(e, args) => {
                let def = &defs.file.enums[*e];
                let layout = self.layout(*e, args);
                let arms = out.switch(values[0], def.cases.len());
                let mut ends = Vec::new();
                for (k, ((case, holds), block)) in
                    def.cases.iter().zip(&defs.cases[*e]).zip(arms).enumerate()
                {
                    out.current = block;
                    let text = match holds {
                        Some(t) => {
                            let held = layout.cases[k].iter().map(|&p| values[1 + p]).collect();
                            let item = (String::new(), t.subst(args), held);
                            self.listed(out, &format!(".{}(", case.name), vec![item])
                        }
                        // The cases of `bool` are written as bare names.
                        None if *e == defs.boolean => self.text(out, case.name.as_bytes()),
                        None => self.text(out, format!(".{}", case.name).as_bytes()),
                    };
                    ends.push((out.current, text));
                }
                out.join(&TEXT, ends)
            }
            _ => unreachable!("a type given `Repr`"),
        }
    }

    /// The text `open`, then the label of each item and the text its value's
    /// `Repr` gives, the items separated by commas, and then `)`.
    fn listed(
        &mut self,
        out: &mut Out<'a>,
        open: &str,
        items: Vec<(String, Ty, Vec<form::Value>)>,
    ) -> Vec<form::Value> {
        let repr = self.typed.defs.repr;
        let mut parts = Vec::new();
        let mut known = open.as_bytes().to_vec();
        for (i, (label, t, values)) in items.into_iter().enumerate() {
            if i > 0 {
                known.extend(b", ");
            }
            known.extend(label.bytes());
            let text = self.run(out, repr, &t, values);
            parts.push(self.text(out, &std::mem::take(&mut known)));
            parts.push(text);
        }
        known.push(b')');
        let close = self.text(out, &known);
        if parts.is_empty() {
            return close;
        }
        parts.push(close);
        let count = parts.len();
        let args = parts.concat();
        self.call_runtime(out, Runtime::Join(count), args, TEXT.to_vec())
    }

    /// Lowers the statements of a block, and gives the values of the last
    /// if it is an expression.
    fn block(&mut self, out: &mut Out<'a>, stmts: &'a [Stmt], env: &[Ty]) -> Vec<form::Value> {
        let mut result = Vec::new();
        for stmt in stmts {
            result = match stmt {
                Stmt::Define { name, value, .. } => {
                    // A name stands for the values it is bound to, so a
                    // binding needs no instruction of its own. A block runs
                    // its statements once each, in order, so an assignment
                    // makes the name stand for the new values from then
                    // on, and a name bound to the old ones keeps them.
                    let (ty, value) = self.expr(out, value, env);
                    match out.find(name) {
                        Some(at) => out.names_mut()[at].values = value,
                        None => out.define(name, ty, value),
                    }
                    Vec::new()
                }
                Stmt::Assign {
                    name,
                    fields,
                    value,
                    ..
                } => {
                    let (_, value) = self.expr(out, value, env);
                    let at = out.find(name).expect("checked");
                    let (start, size) = self.offset(&out.names()[at].ty, fields);
                    out.names_mut()[at]
                        .values
                        .splice(start..start + size, value);
                    Vec::new()
                }
                Stmt::Destructure { pattern, value, .. } => {
                    let (ty, value) = self.expr(out, value, env);
                    self.bind(out, pattern, &ty, value);
                    Vec::new()
                }
                Stmt::Expr(value) => self.expr(out, value, env).1,
            };
        }
        result
    }

    /// The values an expression gives, and their type, where `env` stands
    /// for the type parameters of the definition it is written in.
    fn expr(&mut self, out: &mut Out<'a>, expr: &'a Expr, env: &[Ty]) -> (Ty, Vec<form::Value>) {
        let typed = self.typed;
        let defs = typed.defs;
        match expr {
            Expr::Text { text, .. } => (Ty::Text, self.text(out, text)),
            Expr::Number {
                magnitude,
                negative,
                ..
            } => {
                let ty = typed.type_of(expr, env);
                let Ty::Int(t) = ty else {
                    unreachable!("checked")
                };
                let magnitude = *magnitude as i128;
                let value = if *negative { -magnitude } else { magnitude };
                (ty, vec![out.int(bits_of(t), value)])
            }
            Expr::Interp { parts, .. } => {
                let count = parts.len();
                let mut args = Vec::new();
                for part in parts {
                    args.extend(match part {
                        Part::Text(text) => self.text(out, text),
                        Part::Expr(e) => self.expr(out, e, env).1,
                    });
                }
                let values = self.call_runtime(out, Runtime::Join(count), args, TEXT.to_vec());
                (Ty::Text, values)
            }
            Expr::Name { name, .. } => match out.find(name) {
                Some(at) => (out.names()[at].ty.clone(), out.names()[at].values.clone()),
                // A function passed as a value is lowered to no values.
                None if typed.fn_values.contains_key(&addr(expr)) => {
                    (typed.type_of(expr, env), Vec::new())
                }
                None => {
                    let k = defs.case_of(defs.boolean, name).expect("checked");
                    (
                        defs.bool_ty(),
                        vec![out.constant(
                            form::Constant::Int(k as u64),
                            form::Type::Int(form::Bits::B8),
                        )],
                    )
                }
            },
            Expr::Call(call) => match typed.callee(call, env) {
                Callee::Construct(s, args) => {
                    let inner = args.clone();
                    let values = self.construction(out, call, s, env, &inner);
                    (Ty::Struct(s, args), values)
                }
                Callee::Case(k) => {
                    let ty = typed.type_of(expr, env);
                    let payload = call.args.first().map(|a| &a.value);
                    let values = self.case(out, &ty, k, payload, env);
                    (ty, values)
                }
                Callee::Print => {
                    let (_, text) = self.expr(out, &call.args[0].value, env);
                    let newline = self.text(out, b"\n");
                    for args in [text, newline] {
                        out.emit(
                            form::Op::System {
                                call: form::System::Write(form::Stream::Stdout),
                                args,
                            },
                            Vec::new(),
                        );
                    }
                    (Ty::Nothing, Vec::new())
                }
                Callee::Value => self.call_value(out, call, env),
                Callee::Intrinsic => {
                    let mut args = Vec::new();
                    let mut int = None;
                    for arg in &call.args {
                        let (ty, values) = self.expr(out, &arg.value, env);
                        if let Ty::Int(t) = ty {
                            int = Some(t);
                        }
                        args.push(values[0]);
                    }
                    let int = int.expect("checked");
                    let values = self.intrinsic(out, &call.callee, int, &args);
                    (typed.type_of(expr, env), values)
                }
                Callee::Run(instance, gives)
                    if (defs.params(instance.template).iter())
                        .any(|(_, t)| t.subst(&instance.args).holds_fn()) =>
                {
                    self.in_place(out, call, instance, gives, env)
                }
                Callee::Run(instance, gives) => {
                    let inner = typed.env_of(&Callee::Run(instance.clone(), gives.clone()));
                    let params = defs.params(instance.template);
                    let function = self.place(Body::Copy(instance));
                    // Arguments are worked out in the order they are
                    // written, and then passed in the order of the
                    // parameters.
                    let mut args: Vec<Vec<form::Value>> = vec![Vec::new(); params.len()];
                    for (arg, at) in call.args.iter().zip(params_of(&params, &call.args)) {
                        args[at] = self.expr(out, &arg.value, env).1;
                    }
                    // Then the defaults it leaves out, in the order of the
                    // parameters, as if they were written after the rest.
                    for (at, default) in defs.left_out(call) {
                        args[at] = self.expr(out, default, &inner).1;
                    }
                    let types = self.scalars(&gives);
                    let results = out.emit(
                        form::Op::Call {
                            function,
                            args: args.concat(),
                        },
                        types,
                    );
                    (gives, results)
                }
            },
            Expr::Field { of, name, .. } => {
                let names: Vec<(String, Ty)> = (out.names().iter())
                    .map(|l| (l.name.clone(), l.ty.clone()))
                    .collect();
                if defs.qualified_case(of, &names).is_some() {
                    let ty = typed.type_of(expr, env);
                    let Ty::Enum(e, _) = ty else {
                        unreachable!("checked")
                    };
                    let k = defs.case_of(e, name).expect("checked");
                    let values = self.case(out, &ty, k, None, env);
                    return (ty, values);
                }
                let (Ty::Struct(s, args), values) = self.expr(out, of, env) else {
                    unreachable!("checked")
                };
                let mut at = 0;
                for (field, ty) in defs.file.structs[s].fields.iter().zip(&defs.fields[s]) {
                    let ty = ty.subst(&args);
                    let size = self.size(&ty);
                    if &field.name == name {
                        return (ty, values[at..at + size].to_vec());
                    }
                    at += size;
                }
                unreachable!("checked")
            }
            Expr::Case { name, payload, .. } => {
                let ty = typed.type_of(expr, env);
                let Ty::Enum(e, _) = ty else {
                    unreachable!("checked")
                };
                let k = defs.case_of(e, name).expect("checked");
                let values = self.case(out, &ty, k, payload.as_deref(), env);
                (ty, values)
            }
            Expr::Match(m) => {
                let ty = typed.type_of(expr, env);
                let values = self.match_expr(out, m, &ty, env);
                (ty, values)
            }
            Expr::Tuple { items, .. } => {
                let mut types = Vec::new();
                let mut values = Vec::new();
                for item in items {
                    let (ty, value) = self.expr(out, item, env);
                    types.push(ty);
                    values.extend(value);
                }
                (Ty::tuple(types), values)
            }
            Expr::Lambda(_) => unreachable!("checked"),
        }
    }

    /// Lowers a call of a function that takes a function, in place: its
    /// body, with its parameters standing for the arguments, in a frame of
    /// its own.
    fn in_place(
        &mut self,
        out: &mut Out<'a>,
        call: &'a Call,
        instance: Instance,
        gives: Ty,
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let typed = self.typed;
        let defs = typed.defs;
        let inner = typed.env_of(&Callee::Run(instance.clone(), gives.clone()));
        let view = defs.view(instance.template);
        let params: Vec<(String, Ty)> = (view.params.iter())
            .map(|(n, t)| (n.clone(), t.subst(&instance.args)))
            .collect();
        // Arguments are worked out in the order they are written, and then
        // the defaults the call leaves out, as for any call.
        let mut args: Vec<Option<Bound<'a>>> = vec![None; params.len()];
        for (arg, at) in call.args.iter().zip(params_of(&params, &call.args)) {
            args[at] = Some(self.argument(out, &arg.value, &params[at], env));
        }
        for (at, default) in defs.left_out(call) {
            args[at] = Some(self.argument(out, default, &params[at], &inner));
        }
        let frame = args.into_iter().map(|a| a.expect("given")).collect();
        out.frames.push(frame);
        let caller = std::mem::replace(&mut out.at, out.frames.len() - 1);
        let mut result = self.block(out, &view.f.body, &instance.args);
        out.frames.pop();
        out.at = caller;
        if gives == Ty::Nothing {
            result.clear();
        }
        (gives, result)
    }

    /// An argument for a parameter, as the name the parameter is then.
    fn argument(
        &mut self,
        out: &mut Out<'a>,
        value: &'a Expr,
        (name, ty): &(String, Ty),
        env: &[Ty],
    ) -> Bound<'a> {
        let (values, func) = if let Ty::Fn(_) = ty {
            let func = match value {
                Expr::Lambda(l) => Func::Lambda(l, out.at, env.to_vec()),
                Expr::Name { name, .. } => match out.find(name) {
                    Some(at) => out.names()[at].func.clone().expect("a function"),
                    None => Func::Named(Instance {
                        template: Template::Free(self.typed.fn_values[&addr(value)]),
                        args: Vec::new(),
                    }),
                },
                _ => unreachable!("checked"),
            };
            (Vec::new(), Some(func))
        } else {
            (self.expr(out, value, env).1, None)
        };
        Bound {
            name: name.clone(),
            ty: ty.clone(),
            values,
            func,
        }
    }

    /// Lowers a call of a name that holds a function. A lambda's body is
    /// lowered in place, in the frame it is written in, with its parameters
    /// standing for the arguments.
    fn call_value(
        &mut self,
        out: &mut Out<'a>,
        call: &'a Call,
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let at = out.find(&call.callee).expect("checked");
        let local = &out.names()[at];
        let func = local.func.clone().expect("a function");
        let Ty::Fn(items) = local.ty.clone() else {
            unreachable!("checked")
        };
        let (gives, params) = items.split_last().expect("a result");
        let mut args: Vec<Option<Bound<'a>>> = vec![None; params.len()];
        let mut next = 0;
        for arg in &call.args {
            let at = if arg.after {
                params.len() - 1
            } else {
                next += 1;
                next - 1
            };
            let param = (String::new(), params[at].clone());
            args[at] = Some(self.argument(out, &arg.value, &param, env));
        }
        let args = args.into_iter().map(|a| a.expect("given"));
        let mut result = match func {
            Func::Named(instance) => {
                let function = self.place(Body::Copy(instance));
                let args = args.flat_map(|a| a.values).collect();
                let types = self.scalars(gives);
                out.emit(form::Op::Call { function, args }, types)
            }
            Func::Lambda(lambda, frame, lambda_env) => {
                let caller = std::mem::replace(&mut out.at, frame);
                let before = out.names().len();
                for ((name, _), mut arg) in lambda.params.iter().zip(args) {
                    arg.name = name.clone();
                    out.names_mut().push(arg);
                }
                let result = self.block(out, &lambda.body, &lambda_env);
                out.names_mut().truncate(before);
                out.at = caller;
                result
            }
        };
        if *gives == Ty::Nothing {
            result.clear();
        }
        (gives.clone(), result)
    }

    /// The values of case `k` of the enum `ty`, holding the payload's
    /// values, if it has one: the case, and the places, which the payload's
    /// scalars fill by the rule and the zero of their type fills otherwise.
    fn case(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        k: usize,
        payload: Option<&'a Expr>,
        env: &[Ty],
    ) -> Vec<form::Value> {
        let Ty::Enum(e, args) = ty else {
            unreachable!("checked")
        };
        let layout = self.layout(*e, args);
        let mut places: Vec<Option<form::Value>> = vec![None; layout.places.len()];
        if let Some(p) = payload {
            let (_, values) = self.expr(out, p, env);
            for (value, &place) in values.iter().zip(&layout.cases[k]) {
                places[place] = Some(*value);
            }
        }
        let case = out.constant(
            form::Constant::Int(k as u64),
            form::Type::Int(form::Bits::B8),
        );
        let mut values = vec![case];
        for (place, ty) in places.into_iter().zip(&layout.places) {
            values.push(match place {
                Some(v) => v,
                None => out.zero(*ty),
            });
        }
        values
    }

    /// Lowers a `match` that gives `ty`: a decision between its blocks,
    /// which meet in a block that takes what each gives, and the names
    /// defined before it that a block assigns to. Gives the values of what
    /// it gives.
    ///
    /// While a function is lowered in place, a block may run a lambda that
    /// assigns a `var` of any frame, so every name is compared after it.
    fn match_expr(
        &mut self,
        out: &mut Out<'a>,
        m: &'a Match,
        ty: &Ty,
        env: &[Ty],
    ) -> Vec<form::Value> {
        let (of_ty, of) = self.expr(out, &m.of, env);
        let types = self.scalars(ty);
        let before: Vec<usize> = out.frames.iter().map(Vec::len).collect();
        let was: Vec<Vec<Vec<form::Value>>> = (out.frames.iter())
            .map(|f| f.iter().map(|l| l.values.clone()).collect())
            .collect();
        let arms = Arms {
            arms: &m.arms,
            env,
            types: types.clone(),
            before,
        };
        let rows = (m.arms.iter().enumerate())
            .map(|(arm, a)| Row {
                arm,
                patterns: vec![Some(&a.pattern)],
                binds: Vec::new(),
            })
            .collect();
        let mut leaves = Vec::new();
        self.decide(out, &arms, &mut leaves, rows, &[(of_ty, of)]);
        // The names that a block changed, by their frame and place.
        let mut changed = Vec::new();
        for (f, frame) in was.iter().enumerate() {
            for (at, was_at) in frame.iter().enumerate() {
                if leaves.iter().any(|l| l.names[f][at] != *was_at) {
                    changed.push((f, at));
                }
            }
        }
        let mut join_types = types.clone();
        for &(f, at) in &changed {
            join_types.extend(was[f][at].iter().map(|v| out.types[v.0]));
        }
        let ends = leaves
            .into_iter()
            .map(|leaf| {
                let mut args = leaf.gives;
                args.truncate(types.len());
                for &(f, at) in &changed {
                    args.extend(leaf.names[f][at].clone());
                }
                (leaf.block, args)
            })
            .collect();
        let params = out.join(&join_types, ends);
        let mut at_param = types.len();
        for (f, at) in changed {
            let size = was[f][at].len();
            out.frames[f][at].values = params[at_param..at_param + size].to_vec();
            at_param += size;
        }
        params[..types.len()].to_vec()
    }

    /// Chooses between the blocks of the first of `rows` that matches the
    /// values in `columns`, each with its type, which the patterns of each
    /// row match in order. Each block ends a leaf.
    fn decide(
        &mut self,
        out: &mut Out<'a>,
        arms: &Arms<'a, '_>,
        leaves: &mut Vec<Leaf>,
        rows: Vec<Row>,
        columns: &[(Ty, Vec<form::Value>)],
    ) {
        let defs = self.typed.defs;
        // A bare name names what it matches, and then matches anything.
        let rows: Vec<Row> = (rows.into_iter())
            .map(|mut row| {
                for (pattern, (ty, value)) in row.patterns.iter_mut().zip(columns) {
                    if let Some(Pattern::Bind { name, .. }) = pattern {
                        row.binds.push((name.clone(), ty.clone(), value.clone()));
                        *pattern = None;
                    }
                }
                row
            })
            .collect();
        let Some(first) = rows.first() else {
            // No block matches, which a checked `match` never reaches.
            leaves.push(self.leaf_without_block(out, arms));
            return;
        };
        // The first value the first row takes apart.
        let Some(c) = first.patterns.iter().position(Option::is_some) else {
            let leaf = self.leaf(out, arms, first.clone());
            leaves.push(leaf);
            return;
        };
        // The rows and the columns with column `c` replaced by `inner`, and
        // the pattern of each row there by what `replace` gives for it, or
        // without the row if it gives `None`.
        let around = |inner: Vec<(Ty, Vec<form::Value>)>,
                      replace: &dyn Fn(Option<&Pattern>) -> Option<Patterns>| {
            let mut cols = columns[..c].to_vec();
            cols.extend(inner);
            cols.extend_from_slice(&columns[c + 1..]);
            let rows: Vec<Row> = (rows.iter())
                .filter_map(|r| {
                    let inner = replace(r.patterns[c])?;
                    let mut patterns = r.patterns[..c].to_vec();
                    patterns.extend(inner);
                    patterns.extend_from_slice(&r.patterns[c + 1..]);
                    Some(Row {
                        patterns,
                        ..r.clone()
                    })
                })
                .collect();
            (rows, cols)
        };
        let (ty, value) = &columns[c];
        match ty {
            Ty::Tuple(items) => {
                let inner = (items.iter().cloned())
                    .zip(self.elements(items, value))
                    .collect();
                let (rows, cols) = around(inner, &|p| {
                    Some(match p {
                        Some(Pattern::Tuple { items, .. }) => items.iter().map(Some).collect(),
                        _ => vec![None; items.len()],
                    })
                });
                self.decide(out, arms, leaves, rows, &cols)
            }
            Ty::Enum(e, args) => {
                let layout = self.layout(*e, args);
                let cases = defs.file.enums[*e].cases.len();
                let blocks = out.switch(value[0], cases);
                for (k, case) in defs.file.enums[*e].cases.iter().enumerate() {
                    let holds = defs.cases[*e][k].as_ref().map(|t| t.subst(args));
                    let inner: Vec<(Ty, Vec<form::Value>)> = (holds.iter())
                        .map(|t| {
                            let parts = layout.cases[k].iter().map(|&p| value[1 + p]).collect();
                            (t.clone(), parts)
                        })
                        .collect();
                    let held = holds.is_some();
                    let (rows, cols) = around(inner, &|p| match p {
                        None => Some(if held { vec![None] } else { Vec::new() }),
                        Some(Pattern::Case { name, inner, .. }) if *name == case.name => {
                            Some(if held {
                                vec![inner.as_deref()]
                            } else {
                                Vec::new()
                            })
                        }
                        Some(_) => None,
                    });
                    out.current = blocks[k];
                    self.decide(out, arms, leaves, rows, &cols);
                }
            }
            _ => unreachable!("checked"),
        }
    }

    /// What the names defined before a `match` are now.
    fn names(&self, out: &Out<'a>, arms: &Arms<'a, '_>) -> Vec<Vec<Vec<form::Value>>> {
        (arms.before.iter().enumerate())
            .map(|(f, &n)| {
                out.frames[f][..n]
                    .iter()
                    .map(|l| l.values.clone())
                    .collect()
            })
            .collect()
    }

    /// The block a row leads to, with the names it binds, which ends a leaf.
    fn leaf(&mut self, out: &mut Out<'a>, arms: &Arms<'a, '_>, row: Row) -> Leaf {
        let frames = out.frames.clone();
        for (name, ty, value) in row.binds {
            out.define(&name, ty, value);
        }
        let gives = self.block(out, &arms.arms[row.arm].body, arms.env);
        let names = self.names(out, arms);
        out.frames = frames;
        Leaf {
            block: out.current,
            gives,
            names,
        }
    }

    /// A block that nothing reaches, which gives nothing.
    fn leaf_without_block(&self, out: &mut Out<'a>, arms: &Arms<'a, '_>) -> Leaf {
        Leaf {
            block: out.current,
            gives: arms.types.iter().map(|t| out.zero(*t)).collect(),
            names: self.names(out, arms),
        }
    }

    /// Works out the fields in the order they are written, and then the
    /// defaults of those left out in the order the struct declares them,
    /// and gives their values in the order the struct declares them. `inner`
    /// stands for the struct's type parameters, which its defaults are
    /// written among.
    fn construction(
        &mut self,
        out: &mut Out<'a>,
        call: &'a Call,
        s: usize,
        env: &[Ty],
        inner: &[Ty],
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let def = &defs.file.structs[s];
        let mut fields: Vec<Vec<form::Value>> = vec![Vec::new(); def.fields.len()];
        for Arg { name, value, .. } in &call.args {
            let (_, value) = self.expr(out, value, env);
            let (name, _) = name.as_ref().expect("checked");
            let at = def.fields.iter().position(|f| &f.name == name);
            fields[at.expect("checked")] = value;
        }
        for (at, default) in defs.left_out(call) {
            fields[at] = self.expr(out, default, inner).1;
        }
        fields.concat()
    }
}
