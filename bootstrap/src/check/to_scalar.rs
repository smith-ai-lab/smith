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

use std::rc::Rc;

use super::*;
use crate::scalar as form;
use crate::scalar::runtime::{self, Runtime};

mod functions_calls_and_lambdas;
mod lists_and_text;
mod numbers_and_operators;
mod traits;

use super::lists_and_text::as_list;
use functions_calls_and_lambdas::{can_stop, has_return};
use lists_and_text::indexed;
use numbers_and_operators::{bits_of, signed};

const BYTE: form::Type = form::Type::Int(form::Bits::B8);
/// What a text is.
const TEXT: [form::Type; 2] = [form::Type::Address, form::Type::Int(form::Bits::B64)];

/// The names of what a `loop` over a value keeps of a `return` in its block,
/// which no name written in a program can be.
const KEPT_RAN: &str = "return@ran";
const KEPT_VALUE: &str = "return@kept";

/// A name for the map of an executable, which is one word.
fn spaceless(name: String) -> String {
    name.chars().filter(|c| !c.is_whitespace()).collect()
}

/// The program that `start` runs, in the scalar form.
pub(super) fn lower(typed: &Typed, start: Instance) -> Result<form::Program, Vec<Error>> {
    // A `main` that takes the words it was started with is called by the
    // runtime's `Start`, which the system's start-up code calls.
    let takes = !typed.defs.view(start.template).params.is_empty();
    let mut lowering = Lowering {
        typed,
        bodies: Vec::new(),
        copies: Map::default(),
        runtimes: Vec::new(),
        data: Vec::new(),
        files: Map::default(),
        unders: Map::default(),
        handler_ids: Map::default(),
        contexts: Map::default(),
        errors: Vec::new(),
    };
    lowering.add(Body::Copy(start));
    if takes {
        lowering.add(Body::Runtime(Runtime::Start { main: 0 }));
    }
    let mut functions = Vec::new();
    while functions.len() < lowering.bodies.len() {
        let body = lowering.bodies[functions.len()].clone();
        functions.push(match body {
            Body::Copy(instance) => lowering.function(&instance),
            Body::Under(instance, ids) => lowering.function_under(&instance, &ids),
            Body::Runtime(kind) => runtime::build(kind, &mut |k| lowering.place(Body::Runtime(k))),
        });
    }
    if !lowering.errors.is_empty() {
        return Err(lowering.errors);
    }
    // A function that calls itself as the last thing it does is a loop, and one
    // that calls itself otherwise has a stack of its own, before the stacks of
    // `alloc` are passed, so that the stack is asked for as any memory is.
    // A product of floats that one sum or difference reads is fused with it
    // as the program is made, before any pass, as the rules of floats say.
    let program = form::fuse(&form::Program {
        data: lowering.data,
        functions,
        main: usize::from(takes),
    });
    let program = form::tail(program);
    Ok(form::handler::install(form::recur(program)))
}

/// A function of the program: a copy of one the source defines, or one the
/// program needs at run time, which the scalar form holds once.
#[derive(Clone, PartialEq)]
enum Body {
    Copy(Instance),
    /// A copy of a function that carries an effect a handler answers, for the
    /// handlers it runs under.
    Under(Instance, Vec<HandlerId>),
    Runtime(Runtime),
}

struct Lowering<'a> {
    typed: &'a Typed<'a>,
    /// The functions, each at its place in the program, in the order they
    /// are first called.
    bodies: Vec<Body>,
    /// The place of each copy among `bodies`, so that a copy is found by
    /// its hash and not by a walk of all that are placed.
    copies: Map<Instance, usize>,
    /// The same for the copies built for the handlers they run under.
    unders: Map<(Instance, Vec<HandlerId>), usize>,
    /// The number of each handler, by where it is installed and the handlers in
    /// force there.
    handler_ids: Map<(usize, Vec<HandlerId>), HandlerId>,
    /// What each choice of handlers a copy runs under is made of.
    contexts: Map<Vec<HandlerId>, Vec<(HandlerId, Rc<HandlerDef<'a>>)>>,
    /// What is not built yet and a program uses.
    errors: Vec<Error>,
    /// The place of each function of the runtime, which are few: a kind of
    /// them has no hash, as it is defined where the runtime is.
    runtimes: Vec<(Runtime, usize)>,
    data: Vec<Vec<u8>>,
    /// The number of the name of the file of each module, once it is asked for.
    files: Map<usize, u32>,
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
    /// The `loop`s being lowered, the innermost last.
    loops: Vec<Round>,
    /// The functions and lambdas being lowered, the innermost last, each
    /// with the blocks that leave it by a `return`, and the values they give.
    returns: Vec<Returns>,
    /// The blocks that follow a `return`, which nothing reaches.
    dead: Vec<usize>,
    /// The place in the source of what is being lowered, which each
    /// instruction made holds.
    place: form::Place,
    /// The number of the name of the function the code being lowered is written
    /// in, and of the name of its file's module, for the places made.
    written: u32,
    /// The handlers in force at the code being lowered, the innermost last.
    handlers: Vec<Handler<'a>>,
}

/// A handler's number: one for each place a call of an effect's name is
/// lowered, under the handlers in force there.
type HandlerId = u32;

/// A name that the functions of a handler use, and what it stands for where the
/// handler is installed.
#[derive(Clone)]
struct Captured<'a> {
    name: String,
    ty: Ty,
    /// Whether a function of the handler assigns it.
    assigned: bool,
    /// The scalars it is, when it holds a value.
    scalars: Vec<form::Type>,
    /// The function it holds, when it holds one.
    func: Option<Func<'a>>,
}

/// What a handler is made of: the effect it handles, the function that
/// answers each of its operations, and, for each frame of the function being
/// lowered that those functions are written in or reach, the names of it that
/// they use. A copy of a function that runs under the handler is given the
/// values of those names, and nothing else of the frames.
struct HandlerDef<'a> {
    effect: usize,
    operations: Vec<Func<'a>>,
    frames: Vec<(usize, Vec<Captured<'a>>)>,
}

/// A handler in force: its number, the effect it handles, the function that
/// answers each operation in the order the effect lists them, and how many
/// handlers were in force outside it, which are those its own functions run
/// under. `def` is made when a copy first needs it, and `frames` says where
/// each of its frames is in the function being lowered, or is empty where they
/// are the ones it was installed in.
struct Handler<'a> {
    id: HandlerId,
    effect: usize,
    operations: Vec<Func<'a>>,
    def: Option<Rc<HandlerDef<'a>>>,
    frames: Vec<usize>,
    below: usize,
}

/// A name of a frame whose value a copy gives back, because a function of a
/// handler it runs under assigns it, with the scalars it is.
struct Slot {
    frame: usize,
    name: String,
    types: Vec<form::Type>,
}

/// What a call of a copy takes besides its arguments, and what it gives back
/// besides its results.
struct Prepared {
    function: usize,
    /// The values of the names the functions of its handlers use, which it
    /// takes after its own arguments.
    extra: Vec<form::Value>,
    /// The names it gives back after its own results.
    slots: Vec<Slot>,
}

/// A `loop` being lowered: the names its body assigns, which each round
/// carries to the next, and the blocks that leave it, each with the values
/// of those names there. `body` is set for the block of a `loop` over a
/// value, which is lowered as the function `each` is given, and a `break` of
/// which answers `.stop`.
struct Round {
    carried: Vec<(usize, usize)>,
    leaving: Vec<(usize, Vec<form::Value>)>,
    body: bool,
}

/// A function or a lambda being lowered, with the type it gives, how many
/// names each frame held that outlive it, and the blocks that leave it by a
/// `return`. `kept` is set for the block of a `loop` over a value.
struct Returns {
    gives: Ty,
    limits: Vec<usize>,
    ends: Vec<Leaving>,
    kept: Option<Kept>,
    /// The address of the `loop` whose block this is.
    of: Option<usize>,
}

/// A block that leaves a function or a lambda early: what it gives, and the
/// values of the names that outlive it, by frame and place.
struct Leaving {
    block: usize,
    args: Vec<form::Value>,
    names: Vec<Vec<Vec<form::Value>>>,
}

/// Where the block of a `loop` over a value keeps what a `return` in it
/// gives: whether one ran, and the value, as places in the frame of the
/// function the `loop` is written in. The function gives the value once
/// `each` is over.
#[derive(Clone, Copy)]
struct Kept {
    frame: usize,
    ran: usize,
    value: usize,
}

/// What a body that is lowered to give values is: the statements of a
/// function or a lambda, or the block of a `loop` over a value.
#[derive(Clone, Copy)]
enum Code<'a> {
    Stmts(&'a [Stmt]),
    /// The block of a `loop` over a value, which gives a `Flow` unless it
    /// cannot answer `.stop` and nothing reads the `Flow`.
    Each(&'a Loop, bool),
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
    /// The block of a `loop` over a value, the frame it is written in, and
    /// the types that stand for the type parameters of the definition it is
    /// written in: the function `each` is given.
    Each(&'a Loop, usize, Vec<Ty>),
}

impl<'a> Out<'a> {
    fn new() -> Self {
        let mut out = Out {
            frames: vec![Vec::new()],
            at: 0,
            types: Vec::new(),
            blocks: Vec::new(),
            current: 0,
            loops: Vec::new(),
            returns: Vec::new(),
            dead: Vec::new(),
            place: form::Place::NONE,
            written: 0,
            handlers: Vec::new(),
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
                at: self.place,
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

    /// A float of the size whose bits are `n`.
    fn float(&mut self, bits: form::FloatBits, n: u64) -> form::Value {
        self.constant(form::Constant::Float(n), form::Type::Float(bits))
    }

    fn float_binary(
        &mut self,
        op: form::FloatBinary,
        a: form::Value,
        b: form::Value,
    ) -> form::Value {
        let ty = self.types[a.0];
        self.emit(form::Op::FloatBinary { op, a, b }, vec![ty])[0]
    }

    fn float_negate(&mut self, value: form::Value) -> form::Value {
        let ty = self.types[value.0];
        self.emit(form::Op::FloatNegate(value), vec![ty])[0]
    }

    /// Whether `a` and `b` stand in the relation, as `bool`, by what floats
    /// say: a float that is not a number is none of them, but is not equal.
    fn float_compare(
        &mut self,
        op: form::FloatCompare,
        a: form::Value,
        b: form::Value,
    ) -> form::Value {
        let ty = form::Type::Int(form::Bits::B8);
        self.emit(form::Op::FloatCompare { op, a, b }, vec![ty])[0]
    }

    /// Whether the float `x` is not a number, as `x != x`.
    fn float_is_nan(&mut self, x: form::Value) -> form::Value {
        self.float_compare(form::FloatCompare::NotEqual, x, x)
    }

    /// `1 / x`, which tells `0.0` from `-0.0`, as one is an infinity and the
    /// other the negative of it.
    fn float_inverse(&mut self, x: form::Value) -> form::Value {
        let form::Type::Float(bits) = self.types[x.0] else {
            unreachable!("a float")
        };
        let one = match bits {
            form::FloatBits::F32 => 0x3f80_0000,
            form::FloatBits::F64 => 0x3ff0_0000_0000_0000,
        };
        let one = self.float(bits, one);
        self.float_binary(form::FloatBinary::Div, one, x)
    }

    /// Whether the floats `a` and `b` are the same value, as `==` says of
    /// floats: a float that is not a number equals itself, and `0.0` and
    /// `-0.0` are two values. It is worked out from `==` and `!=` of the
    /// floats alone, so that no target's choice of the bits of a float that is
    /// not a number is seen.
    fn float_equal(&mut self, a: form::Value, b: form::Value) -> form::Value {
        use form::Binary::{And, Or};
        let same = self.float_compare(form::FloatCompare::Equal, a, b);
        // Where they are equal, they are one value unless they are zeros of two
        // signs, whose inverses are two infinities.
        let (ia, ib) = (self.float_inverse(a), self.float_inverse(b));
        let inverses = self.float_compare(form::FloatCompare::Equal, ia, ib);
        let (na, nb) = (self.float_is_nan(a), self.float_is_nan(b));
        let both_nan = self.binary(And, na, nb);
        let value = self.binary(And, same, inverses);
        self.binary(Or, value, both_nan)
    }

    /// Whether the float `a` is below `b`, as `<` says of floats: `-0.0` is
    /// below `0.0`, and a float that is not a number is above every other.
    fn float_less(&mut self, a: form::Value, b: form::Value) -> form::Value {
        use form::Binary::{And, Or, Xor};
        let one = self.int(form::Bits::B8, 1);
        let below = self.float_compare(form::FloatCompare::Less, a, b);
        let same = self.float_compare(form::FloatCompare::Equal, a, b);
        let (ia, ib) = (self.float_inverse(a), self.float_inverse(b));
        let inverse_below = self.float_compare(form::FloatCompare::Less, ia, ib);
        let zeros = self.binary(And, same, inverse_below);
        let (na, nb) = (self.float_is_nan(a), self.float_is_nan(b));
        let a_number = self.binary(Xor, na, one);
        let number_before_nan = self.binary(And, a_number, nb);
        let value = self.binary(Or, below, zeros);
        self.binary(Or, value, number_before_nan)
    }

    fn float_from_int(
        &mut self,
        signed: bool,
        value: form::Value,
        to: form::FloatBits,
    ) -> form::Value {
        self.emit(
            form::Op::FloatFromInt { signed, value },
            vec![form::Type::Float(to)],
        )[0]
    }

    /// The number `op` gives of `a` and `b` with the bits that do not fit left
    /// off, and whether it did not fit, as a `bool`.
    fn overflows(
        &mut self,
        op: form::Binary,
        signed: bool,
        a: form::Value,
        b: form::Value,
    ) -> (form::Value, form::Value) {
        let ty = self.types[a.0];
        let results = self.emit(
            form::Op::Overflows { op, signed, a, b },
            vec![ty, form::Type::Int(form::Bits::B8)],
        );
        (results[0], results[1])
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

    /// The value with the size `to`, which stands for the same number when
    /// it fits, read as signed or as unsigned, and keeps the low bits when
    /// it does not. It is the value itself when the size is the same.
    fn resize(&mut self, signed: bool, value: form::Value, to: form::Bits) -> form::Value {
        let form::Type::Int(from) = self.types[value.0] else {
            unreachable!("a whole number")
        };
        match to.count().cmp(&from.count()) {
            std::cmp::Ordering::Less => self.narrow(value, to),
            std::cmp::Ordering::Equal => value,
            std::cmp::Ordering::Greater => {
                let ty = form::Type::Int(to);
                self.emit(form::Op::Widen { signed, value }, vec![ty])[0]
            }
        }
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
            form::Type::Float(_) => self.constant(form::Constant::Float(0), ty),
            form::Type::Vector(_) => self.constant(form::Constant::vector(0), ty),
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

    /// Ends the block being filled by choosing between blocks that are made,
    /// the one at the place of the integer `on`.
    fn switch_to(&mut self, on: form::Value, blocks: Vec<usize>) {
        let jumps = blocks
            .into_iter()
            .map(|block| form::Jump {
                block,
                args: Vec::new(),
            })
            .collect();
        self.blocks[self.current].end = form::End::Switch { on, jumps };
    }

    /// Ends the block being filled by going to the block of the literal that
    /// `value`, a number of `bits` bits, equals, or to `miss` when it equals
    /// none. `literals` are the literals with their blocks, ascending and
    /// all different. The number of instructions that run does not depend on
    /// how many there are when they lie close together, which is when at
    /// most 4 places of a table are used for each, and grows with the
    /// logarithm of how many there are when they do not, as a comparison
    /// splits them into two sets that are chosen between in the same way.
    fn pick(
        &mut self,
        value: form::Value,
        signed: bool,
        bits: form::Bits,
        literals: &[(i128, usize)],
        miss: usize,
    ) {
        let count = literals.len();
        let Some(&(least, _)) = literals.first() else {
            let jump = form::Jump {
                block: miss,
                args: Vec::new(),
            };
            self.blocks[self.current].end = form::End::Jump(jump);
            return;
        };
        let span = (literals[count - 1].0 - least + 1) as u128;
        if count <= 3 {
            // The place of the literal that equals the value, or `count`.
            let mut place = self.int(form::Bits::B8, count as i128);
            for (i, (literal, _)) in literals.iter().enumerate().rev() {
                let literal = self.int(bits, *literal);
                let same = self.compare(form::Compare::Equal, value, literal);
                let here = self.int(form::Bits::B8, i as i128);
                place = self.choose(same, here, place);
            }
            let mut blocks: Vec<usize> = literals.iter().map(|(_, b)| *b).collect();
            blocks.push(miss);
            self.switch_to(place, blocks);
        } else if span <= 4 * count as u128 {
            // The distance from the least, which is the place when it is
            // below the number of places, and the place after them when not.
            let all = span == 1 << bits.count();
            let most = if all { span - 1 } else { span };
            let place_bits = if most < 1 << 8 {
                form::Bits::B8
            } else if most < 1 << 16 {
                form::Bits::B16
            } else {
                form::Bits::B32
            };
            let least_value = self.int(bits, least);
            let distance = self.binary(form::Binary::Sub, value, least_value);
            let mut place = self.resize(false, distance, place_bits);
            if !all {
                let limit = self.int(bits, span as i128);
                let inside = self.compare(form::Compare::LessU, distance, limit);
                let after = self.int(place_bits, span as i128);
                place = self.choose(inside, place, after);
            }
            let mut blocks = vec![miss; span as usize + usize::from(!all)];
            for (literal, block) in literals {
                blocks[(literal - least) as usize] = *block;
            }
            self.switch_to(place, blocks);
        } else {
            // Splits where the literals are furthest apart of the middle
            // half of them, so that each side has at most 3 in 4.
            let split = (count / 4).max(1)..=(count - count / 4).min(count - 1);
            let at = split
                .max_by_key(|&i| {
                    // The furthest apart, and the nearest the middle of those.
                    let nearness = count.abs_diff(2 * i);
                    (
                        literals[i].0 - literals[i - 1].0,
                        std::cmp::Reverse(nearness),
                    )
                })
                .expect("a split");
            let pivot = self.int(bits, literals[at].0);
            let less = if signed {
                form::Compare::LessS
            } else {
                form::Compare::LessU
            };
            let below = self.compare(less, value, pivot);
            let (low, high) = (self.new_block(), self.new_block());
            self.switch_to(below, vec![high, low]);
            self.current = low;
            self.pick(value, signed, bits, &literals[..at], miss);
            self.current = high;
            self.pick(value, signed, bits, &literals[at..], miss);
        }
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
    /// What the names defined before it stood for when it began.
    was: &'p [Vec<Vec<form::Value>>],
}

impl<'a> Lowering<'a> {
    /// The types of the scalars a value of the type is lowered to: an
    /// address and a length for each text, those of each part for a struct
    /// and each element for a tuple, for an enum the number of its case and
    /// its places, and none for a function, whose calls are lowered in
    /// place.
    fn scalars(&self, ty: &Ty) -> Vec<form::Type> {
        let mut all = Vec::new();
        self.scalars_into(ty, &mut |t| all.push(t));
        all
    }

    /// Gives each scalar of the type to `put`, in order.
    fn scalars_into(&self, ty: &Ty, put: &mut dyn FnMut(form::Type)) {
        let defs = self.typed.defs;
        match ty {
            Ty::Text | Ty::Slice(_) => {
                put(form::Type::Address);
                put(form::Type::Int(form::Bits::B64));
            }
            Ty::List(_) => {
                put(form::Type::Address);
                put(form::Type::Int(form::Bits::B64));
                put(form::Type::Int(form::Bits::B64));
            }
            Ty::Int(t) => put(form::Type::Int(bits_of(*t))),
            Ty::Float(f) => put(form::Type::Float(float_bits(*f))),
            Ty::Struct(s, args) => {
                for f in &defs.fields[*s] {
                    self.scalars_into(&f.subst(args), put);
                }
            }
            Ty::Tuple(items) => {
                for t in items {
                    self.scalars_into(t, put);
                }
            }
            Ty::Enum(e, args) => {
                put(form::Type::Int(form::Bits::B8));
                for place in self.layout(*e, args).places {
                    put(place);
                }
            }
            Ty::Param(_) | Ty::Fn(_) | Ty::Named(_) | Ty::Nothing | Ty::Unknown | Ty::Flex(_) => {}
        }
    }

    /// How many scalars a value of the type is lowered to.
    fn size(&self, ty: &Ty) -> usize {
        let mut n = 0;
        self.scalars_into(ty, &mut |_| n += 1);
        n
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
            Pattern::Case { .. } | Pattern::Number { .. } | Pattern::Text { .. } => {
                unreachable!("checked")
            }
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
        let found = match &body {
            Body::Copy(instance) => self.copies.get(instance).copied(),
            Body::Under(instance, ids) => {
                self.unders.get(&(instance.clone(), ids.clone())).copied()
            }
            Body::Runtime(kind) => (self.runtimes.iter())
                .find(|(k, _)| k == kind)
                .map(|&(_, at)| at),
        };
        found.unwrap_or_else(|| self.add(body))
    }

    /// Puts a body at the end of the program's functions.
    fn add(&mut self, body: Body) -> usize {
        let at = self.bodies.len();
        match &body {
            Body::Copy(instance) => {
                self.copies.insert(instance.clone(), at);
            }
            Body::Under(instance, ids) => {
                self.unders.insert((instance.clone(), ids.clone()), at);
            }
            Body::Runtime(kind) => self.runtimes.push((kind.clone(), at)),
        }
        self.bodies.push(body);
        at
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

    /// What a given implementation is, as the map of an executable names it.
    fn given_name(&self, k: usize, fi: usize) -> &'static str {
        let defs = self.typed.defs;
        if k == defs.function {
            ["function_name", "function_about", "function_call"][fi]
        } else if defs.serialize == Some(k) {
            "serialize"
        } else if defs.deserialize == Some(k) {
            "deserialize"
        } else if defs.describe == Some(k) {
            "describe"
        } else if defs.from_text == Some(k) {
            "from_text"
        } else if k == defs.convert() {
            "convert"
        } else if k == defs.fmt {
            "fmt"
        } else if k == defs.repr {
            "repr"
        } else if k == defs.ord() {
            "ord"
        } else if k == defs.hash() {
            "hash"
        } else {
            "eq"
        }
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

    /// The values of the names that outlive a body, `limits` of each frame.
    fn names_now(&self, out: &Out<'a>, limits: &[usize]) -> Vec<Vec<Vec<form::Value>>> {
        (limits.iter().enumerate())
            .map(|(f, &n)| {
                out.frames[f][..n]
                    .iter()
                    .map(|b| b.values.clone())
                    .collect()
            })
            .collect()
    }

    /// `.go` or `.stop`, of the prelude's `Flow`.
    fn flow(&self, out: &mut Out<'a>, stop: bool) -> form::Value {
        let defs = self.typed.defs;
        let name = if stop { "stop" } else { "go" };
        let case = defs.case_of(defs.flow, name).expect("a case");
        out.constant(form::Constant::Int(case as u64), BYTE)
    }

    /// Where the block of a `loop` over a value keeps what a `return` in it
    /// gives, if it has one.
    fn kept_of(&self, out: &Out<'a>, l: &Loop) -> Option<Kept> {
        has_return(&l.body).then(|| Kept {
            frame: out.at,
            ran: out.find(KEPT_RAN).expect("kept"),
            value: out.find(KEPT_VALUE).expect("kept"),
        })
    }

    /// Lowers the statements of a block, and gives the values of the last
    /// if it is an expression.
    fn block(&mut self, out: &mut Out<'a>, stmts: &'a [Stmt], env: &[Ty]) -> Vec<form::Value> {
        let mut result = Vec::new();
        for stmt in stmts {
            let outer = out.place;
            out.place = self.place_of(
                out,
                match stmt {
                    Stmt::Define { pos, .. } | Stmt::Assign { pos, .. } => *pos,
                    Stmt::Destructure { pattern, .. } => pattern.pos(),
                    Stmt::Expr(value) => value.pos(),
                },
            );
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
            out.place = outer;
        }
        result
    }

    /// The values an expression gives, and their type, where `env` stands
    /// for the type parameters of the definition it is written in.
    fn expr(&mut self, out: &mut Out<'a>, expr: &'a Expr, env: &[Ty]) -> (Ty, Vec<form::Value>) {
        // What the expression makes is placed where it is written, and what
        // follows it in the expression it is part of is placed there again.
        let outer = out.place;
        out.place = self.place_of(out, expr.pos());
        let (ty, values) = self.as_written(out, expr, env);
        let given = match (&ty, self.typed.widened.get(&addr(expr))) {
            (Ty::Int(from), Some(&to)) => {
                // A range taken where a wider one is needed is the same number
                // at the size the wider one is kept at.
                let wide = out.resize(from.signed(), values[0], bits_of(to));
                (Ty::Int(to), vec![wide])
            }
            _ => (ty, values),
        };
        out.place = outer;
        given
    }

    /// The place in the source a position is, in the function being lowered.
    /// The code of the prelude has none.
    fn place_of(&mut self, out: &Out<'a>, pos: Pos) -> form::Place {
        // The code of the prelude has no line to name: what it makes is placed
        // where the call or the loop that it is lowered for is.
        if of_prelude(pos) {
            return out.place;
        }
        let module = module_of(pos);
        let defs = self.typed.defs;
        let file = *self.files.entry(module).or_insert_with(|| {
            let name = (defs.file.modules.iter())
                .find(|(n, _)| *n == module)
                .map_or("main", |(_, name)| name.as_str());
            form::names::intern(&format!("{}.smith", name.replace('.', "/")))
        });
        form::Place {
            file,
            function: out.written,
            line: (pos.line % STEP) as u32,
            col: pos.col as u32,
        }
    }

    /// The values an expression gives as it is written, and their type.
    fn as_written(
        &mut self,
        out: &mut Out<'a>,
        expr: &'a Expr,
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let typed = self.typed;
        let defs = typed.defs;
        match expr {
            Expr::Unbuilt { .. } => unreachable!("what is not built is an error"),
            Expr::Text { text, .. } => (Ty::Text, self.text(out, text)),
            Expr::Number {
                magnitude,
                negative,
                ..
            } => self.number(out, expr, (*magnitude, *negative), env),
            Expr::Float { text, negative, .. } => {
                let Ty::Float(float) = typed.type_of(expr, env) else {
                    unreachable!("checked")
                };
                let bits = float.bits_of(text, *negative).expect("checked");
                (Ty::Float(float), vec![out.float(float_bits(float), bits)])
            }
            Expr::Interp { parts, .. } => self.interpolation(out, parts, env),
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
            Expr::Call(call) if self.bits_function(call).is_some() => {
                let op = self.bits_function(call).expect("a function of `Bits`");
                self.bits_call(out, expr, call, op, env)
            }
            Expr::Call(call) if self.compares_scalars(call, "eq", env) => {
                self.equal_scalars(out, call, env)
            }
            Expr::Call(call) if self.compares_mixed(call, env) => {
                self.compare_mixed(out, call, env)
            }
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
                Callee::Print | Callee::Eprint => {
                    let stream = match typed.callee(call, env) {
                        Callee::Print => form::Stream::Stdout,
                        _ => form::Stream::Stderr,
                    };
                    let (_, text) = self.expr(out, &call.args[0].value, env);
                    let newline = self.text(out, b"\n");
                    for args in [text, newline] {
                        out.emit(
                            form::Op::System {
                                call: form::System::Write(stream),
                                args,
                            },
                            Vec::new(),
                        );
                    }
                    (Ty::Nothing, Vec::new())
                }
                // A handler in force answers the operation, as it answers any
                // other, and the system answers it when none is.
                Callee::Fill
                    if let Some(f) = defs.function(call.name_pos, &call.callee)
                        && out
                            .handlers
                            .iter()
                            .any(|h| h.effect == defs.operation_of(f).0) =>
                {
                    let instance = Instance {
                        template: Template::Free(f),
                        args: Vec::new(),
                    };
                    let gives = typed.type_of(expr, env);
                    self.operation_call(out, call, instance, gives, env)
                }
                Callee::Fill => {
                    let (ty, list) = self.expr(out, &call.args[0].value, env);
                    self.call_runtime(out, Runtime::Entropy, vec![list[0], list[1]], Vec::new());
                    (ty, list)
                }
                Callee::End => {
                    let (_, status) = self.expr(out, &call.args[0].value, env);
                    out.emit(
                        form::Op::System {
                            call: form::System::Exit,
                            args: status,
                        },
                        Vec::new(),
                    );
                    (Ty::Nothing, Vec::new())
                }
                Callee::Push => self.push(out, call, env),
                Callee::Freeze => self.freeze(out, expr, call, env),
                Callee::Utf8 => self.utf8(out, expr, call, env),
                Callee::Narrow(narrowing) => self.narrow_call(out, expr, call, narrowing, env),
                Callee::Float => self.float_call(out, expr, call, env),
                Callee::Value => self.call_value(out, call, env),
                Callee::Handle(effect) => self.handle(out, expr, call, effect, env),
                Callee::Intrinsic => self.intrinsic_call(out, expr, call, env),
                Callee::Run(instance, _)
                    if !matches!(instance.template, Template::Given(..))
                        && numbers_and_operators::number_function(
                            defs.view(instance.template).f,
                        )
                        .is_some() =>
                {
                    let f = defs.view(instance.template).f;
                    let named = numbers_and_operators::number_function(f).expect("checked");
                    let places =
                        params_of(&defs.params(instance.template), &call.args).expect("checked");
                    self.number_call(out, expr, call, named, &places, env)
                }
                Callee::Run(instance, gives) if defs.is_operation_of(&instance) => {
                    self.operation_call(out, call, instance, gives, env)
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
                    // `Function.call` of a function that carries an effect a
                    // handler answers calls the function here, under the
                    // handlers in force, and not through a function of its
                    // own that has none.
                    let prepared = match self.handled_function_call(&instance) {
                        Some(f) => self.copy_for(
                            out,
                            &Instance {
                                template: Template::Free(f),
                                args: Vec::new(),
                            },
                        ),
                        None => self.copy_for(out, &instance),
                    };
                    // Arguments are worked out in the order they are
                    // written, and then passed in the order of the
                    // parameters.
                    let mut args: Vec<Vec<form::Value>> = vec![Vec::new(); params.len()];
                    for (arg, at) in call
                        .args
                        .iter()
                        .zip(params_of(&params, &call.args).expect("checked"))
                    {
                        let param = params[at].1.subst(&instance.args);
                        args[at] = self.passed(out, &arg.value, &param, env);
                    }
                    // Then the defaults it leaves out, in the order of the
                    // parameters, as if they were written after the rest.
                    for (at, default) in defs.left_out(call) {
                        args[at] = self.expr(out, default, &inner).1;
                    }
                    let results = self.finish_call(out, prepared, args.concat(), &gives);
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
                if indexed(of) {
                    let (ty, address, scalars) = self.element_at(out, expr, env);
                    return (ty, self.load(out, address, &scalars));
                }
                let (of_ty, values) = self.expr(out, of, env);
                if matches!(of_ty, Ty::Text) && name == "bytes" {
                    return (typed.type_of(expr, env), values);
                }
                if matches!(of_ty, Ty::List(_) | Ty::Slice(_) | Ty::Text) {
                    return (typed.type_of(expr, env), vec![values[1]]);
                }
                let Ty::Struct(s, args) = of_ty else {
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
            // The result was proven to fit, so the operation is the one
            // instruction, and no result is tested.
            Expr::Arith {
                op,
                left,
                right,
                op_pos,
            } => self.operation(out, expr, (*op, left, right, *op_pos), env),
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
            Expr::List { items, .. } => self.list_literal(out, expr, items, env),
            Expr::Slice { of, from, to, .. } => {
                self.piece(out, expr, (of, from.as_deref(), to.as_deref()), env)
            }
            Expr::Index { .. } => {
                let (ty, address, scalars) = self.element_at(out, expr, env);
                (ty, self.load(out, address, &scalars))
            }
            Expr::Lambda(_) => unreachable!("checked"),
            Expr::Loop(l) => {
                match &l.over {
                    Some(o) => self.each_loop(out, l, o, env),
                    None => self.round(out, l, env),
                }
                (Ty::Nothing, Vec::new())
            }
            Expr::Return { value, .. } => {
                let values = match value {
                    Some(value) => self.expr(out, value, env).1,
                    None => Vec::new(),
                };
                match out.returns.last().and_then(|r| r.kept) {
                    // The block of a `loop` over a value keeps what it gives
                    // and answers `.stop`, and the function gives it once
                    // `each` is over.
                    Some(kept) => {
                        let ran = self.truth(out, true);
                        out.frames[kept.frame][kept.ran].values = vec![ran];
                        out.frames[kept.frame][kept.value].values = values;
                        let stop = self.flow(out, true);
                        self.leave(out, vec![stop]);
                    }
                    None => self.leave(out, values),
                }
                (Ty::Nothing, Vec::new())
            }
            Expr::Break { .. } => {
                if out.loops.last().expect("checked").body {
                    let stop = self.flow(out, true);
                    self.leave(out, vec![stop]);
                    return (Ty::Nothing, Vec::new());
                }
                let round = out.loops.last().expect("checked");
                let values = round
                    .carried
                    .iter()
                    .flat_map(|&(f, at)| out.frames[f][at].values.clone())
                    .collect();
                let from = out.current;
                out.loops
                    .last_mut()
                    .expect("checked")
                    .leaving
                    .push((from, values));
                // What follows a `break` is reached by nothing.
                out.current = out.new_block();
                (Ty::Nothing, Vec::new())
            }
        }
    }

    /// Lowers a `loop`. Each round is the block `head`, which takes the
    /// number it is on, when the `loop` runs over a range, and the values
    /// of the names the body assigns, and goes to the next round, or leaves
    /// when the range is used up. A `break` leaves too, and the blocks that
    /// leave meet in one, which gives the names their values after the
    /// `loop`.
    fn round(&mut self, out: &mut Out<'a>, l: &'a Loop, env: &[Ty]) {
        // A name is a `var` of the frame it is written in, so a name that
        // the body assigns is of this frame, and one that a lambda assigns
        // is of the frame the lambda is written in. A lambda written outside
        // the `loop` may be called inside it, so what the lambdas in any
        // frame assign is carried too.
        let names_of = |body: &'a [Stmt]| -> Vec<&'a str> {
            statements(body)
                .into_iter()
                .flat_map(|stmt| match stmt {
                    Stmt::Define { name, .. } | Stmt::Assign { name, .. } => vec![name.as_str()],
                    Stmt::Destructure { pattern, .. } => {
                        pattern_names(pattern).into_iter().map(|(n, _)| n).collect()
                    }
                    Stmt::Expr(_) => Vec::new(),
                })
                .collect()
        };
        let mut assigned: Vec<(usize, &str)> = (names_of(&l.body).into_iter())
            .map(|name| (out.at, name))
            .collect();
        for bound in out.frames.iter().flatten() {
            match &bound.func {
                Some(Func::Lambda(lambda, frame, _)) => {
                    assigned.extend(names_of(&lambda.body).into_iter().map(|n| (*frame, n)));
                }
                // The block of a `loop` over a value assigns what it keeps
                // of a `return` as well.
                // The block of a `loop` over a value assigns what it keeps of
                // a `return`, and the names it assigns itself, which a loop
                // in the block that is being lowered does not carry for it,
                // as the block is not called in itself.
                Some(Func::Each(each, frame, _)) => {
                    let lowering = out.returns.iter().any(|r| r.of == Some(addr(*each)));
                    if !lowering {
                        assigned.extend(names_of(&each.body).into_iter().map(|n| (*frame, n)));
                    }
                    if has_return(&each.body) {
                        assigned.push((*frame, KEPT_RAN));
                        assigned.push((*frame, KEPT_VALUE));
                    }
                }
                _ => {}
            }
        }
        // What the functions of the handlers in force assign, which a call of
        // an operation inside the `loop` runs.
        for handler in &out.handlers {
            for func in &handler.operations {
                if let Func::Lambda(lambda, frame, _) = func {
                    assigned.extend(names_of(&lambda.body).into_iter().map(|n| (*frame, n)));
                }
            }
        }
        let mut carried = Vec::new();
        for (f, frame) in out.frames.iter().enumerate() {
            for (at, bound) in frame.iter().enumerate() {
                if !bound.values.is_empty() && assigned.contains(&(f, bound.name.as_str())) {
                    carried.push((f, at));
                }
            }
        }
        let held = |out: &Out<'a>| -> Vec<form::Value> {
            (carried.iter())
                .flat_map(|&(f, at)| out.frames[f][at].values.clone())
                .collect()
        };
        let range = match &l.range {
            Some(r) => {
                let Ty::Int(t) = self.typed.type_of(&r.from, env) else {
                    unreachable!("checked")
                };
                let from = self.expr(out, &r.from, env).1[0];
                let to = self.expr(out, &r.to, env).1[0];
                Some((r.name.as_str(), t, from, to))
            }
            None => None,
        };
        let start = held(out);
        let mut types: Vec<form::Type> = start.iter().map(|v| out.types[v.0]).collect();
        let counter_type = range.map(|(_, t, ..)| form::Type::Int(bits_of(t)));
        types.splice(0..0, counter_type);
        let head = out.new_block();
        let params: Vec<form::Value> = types.iter().map(|t| out.fresh(*t)).collect();
        out.blocks[head].params = params.clone();
        let mut args = start;
        args.splice(0..0, range.map(|(.., from, _)| from));
        let before = out.current;
        out.blocks[before].end = form::End::Jump(form::Jump { block: head, args });
        out.current = head;
        let mut at_param = usize::from(range.is_some());
        for &(f, at) in &carried {
            let size = out.frames[f][at].values.len();
            out.frames[f][at].values = params[at_param..at_param + size].to_vec();
            at_param += size;
        }
        let mut round = Round {
            carried: carried.clone(),
            leaving: Vec::new(),
            body: false,
        };
        let names = out.names().len();
        if let Some((name, t, _, to)) = range {
            // A number below the end is on this round, and the rest leave.
            let signed = t.signed();
            let below = out.compare(
                if signed {
                    form::Compare::LessS
                } else {
                    form::Compare::LessU
                },
                params[0],
                to,
            );
            let [leave, inside] = out.switch(below, 2)[..] else {
                unreachable!("two cases")
            };
            round.leaving.push((leave, held(out)));
            out.current = inside;
            out.define(name, Ty::Int(t), vec![params[0]]);
        }
        out.loops.push(round);
        self.block(out, &l.body, env);
        let round = out.loops.pop().expect("pushed");
        out.names_mut().truncate(names);
        let mut args = held(out);
        if let Some((_, t, ..)) = range {
            let one = out.int(bits_of(t), 1);
            args.insert(0, out.binary(form::Binary::Add, params[0], one));
        }
        let last = out.current;
        out.blocks[last].end = form::End::Jump(form::Jump { block: head, args });
        let after: Vec<form::Type> = types[usize::from(range.is_some())..].to_vec();
        let joined = out.join(&after, round.leaving);
        let mut at_param = 0;
        for (f, at) in carried {
            let size = out.frames[f][at].values.len();
            out.frames[f][at].values = joined[at_param..at_param + size].to_vec();
            at_param += size;
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
        for (arg, at) in call
            .args
            .iter()
            .zip(params_of(&params, &call.args).expect("checked"))
        {
            args[at] = Some(self.argument(out, &arg.value, &params[at], env));
        }
        for (at, default) in defs.left_out(call) {
            args[at] = Some(self.argument(out, default, &params[at], &inner));
        }
        let frame = args.into_iter().map(|a| a.expect("given")).collect();
        let limits = out.frames.iter().map(Vec::len).collect();
        out.frames.push(frame);
        let caller = std::mem::replace(&mut out.at, out.frames.len() - 1);
        let written = out.written;
        if !of_prelude(view.f.pos) {
            out.written = form::names::intern(&self.name_of(&instance));
        }
        let code = Code::Stmts(&view.f.body);
        let mut result = self.leaving(out, code, &instance.args, (&gives, false), limits);
        out.written = written;
        out.frames.pop();
        out.at = caller;
        if gives == Ty::Nothing {
            result.clear();
        }
        (gives, result)
    }

    /// Lowers a `loop` over a value: the `each` of its `Iterate`, in place,
    /// given the block of the loop as the function it calls on each
    /// element. A `return` in the block is kept, and the function gives it
    /// once `each` is over.
    fn each_loop(&mut self, out: &mut Out<'a>, l: &'a Loop, o: &'a Over, env: &[Ty]) {
        let typed = self.typed;
        let defs = typed.defs;
        let instance = typed.each_of(o, env);
        let view = defs.view(instance.template);
        let params: Vec<(String, Ty)> = (view.params.iter())
            .map(|(n, t)| (n.clone(), t.subst(&instance.args)))
            .collect();
        let flow = Ty::Enum(defs.flow, vec![Ty::Nothing, Ty::Nothing]);
        let outer = out.names().len();
        if has_return(&l.body) {
            let gives = out.returns.last().expect("checked").gives.clone();
            let ran = self.truth(out, false);
            out.define(KEPT_RAN, Ty::Enum(defs.boolean, Vec::new()), vec![ran]);
            let zeros = self
                .scalars(&gives)
                .into_iter()
                .map(|t| out.zero(t))
                .collect();
            out.define(KEPT_VALUE, gives, zeros);
        }
        let walked = Bound {
            name: params[0].0.clone(),
            ty: params[0].1.clone(),
            values: self.passed(out, &o.value, &params[0].1, env),
            func: None,
        };
        let body = Bound {
            name: params[1].0.clone(),
            ty: params[1].1.clone(),
            values: Vec::new(),
            func: Some(Func::Each(l, out.at, env.to_vec())),
        };
        let limits: Vec<usize> = out.frames.iter().map(Vec::len).collect();
        out.frames.push(vec![walked, body]);
        let caller = std::mem::replace(&mut out.at, out.frames.len() - 1);
        let written = out.written;
        if !of_prelude(view.f.pos) {
            out.written = form::names::intern(&self.name_of(&instance));
        }
        let code = Code::Stmts(&view.f.body);
        let result = self.leaving(out, code, &instance.args, (&flow, false), limits);
        out.written = written;
        out.frames.pop();
        out.at = caller;
        // A loop reads nothing of the `Flow` that `each` gives, so a constant
        // that was only built to give it is not kept.
        if let [value] = result[..] {
            let last = out.blocks[out.current].instructions.last();
            let built =
                last.is_some_and(|i| i.results == [value] && matches!(i.op, form::Op::Constant(_)));
            if built && value.0 + 1 == out.types.len() {
                out.blocks[out.current].instructions.pop();
                out.types.pop();
            }
        }
        if out.names().len() > outer {
            // The function gives what a `return` in the block kept, when
            // one ran.
            let ran = out.names()[outer].values[0];
            let kept = out.names()[outer + 1].values.clone();
            let [no, yes] = out.switch(ran, 2)[..] else {
                unreachable!("two cases")
            };
            let limits = out.returns.last().expect("checked").limits.clone();
            out.current = yes;
            let names = self.names_now(out, &limits);
            let returns = out.returns.last_mut().expect("checked");
            returns.ends.push(Leaving {
                block: yes,
                args: kept,
                names,
            });
            out.current = no;
            out.names_mut().truncate(outer);
        }
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
        if let Some(values) = self.after_block(out, m, env) {
            return values;
        }
        if let Some(values) = self.comparison(out, m, env) {
            return values;
        }
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
            was: &was,
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
                // A block that leaves by a `return` gives nothing, and what
                // it jumps with is never read.
                let current = std::mem::replace(&mut out.current, leaf.block);
                while args.len() < types.len() {
                    args.push(out.zero(types[args.len()]));
                }
                out.current = current;
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

    /// Lowers a `match` on what the block of a `loop` over a value gives,
    /// when the block cannot answer `.stop`: it answers `.go`, which the
    /// compiler knows, so no `Flow` is built or tested, and only the block
    /// of `.go` is lowered, after the block of the loop.
    fn after_block(
        &mut self,
        out: &mut Out<'a>,
        m: &'a Match,
        env: &[Ty],
    ) -> Option<Vec<form::Value>> {
        let Expr::Call(call) = &m.of else {
            return None;
        };
        if !self.typed.value_calls.contains(&addr(call)) {
            return None;
        }
        let Some(Func::Each(each, ..)) = out
            .find(&call.callee)
            .and_then(|at| out.names()[at].func.clone())
        else {
            return None;
        };
        let go = (m.arms.iter()).find(
            |arm| matches!(&arm.pattern, Pattern::Case { name, inner: None, .. } if name == "go"),
        );
        let go = go.filter(|_| !can_stop(&each.body))?;
        self.call_value_as(out, call, env, true);
        // A name the block defines is not there after it.
        let before: Vec<usize> = out.frames.iter().map(Vec::len).collect();
        let values = self.block(out, &go.body, env);
        for (frame, n) in out.frames.iter_mut().zip(before) {
            frame.truncate(n);
        }
        Some(values)
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
            Ty::Int(t) => {
                // One block for each literal, in the order they are written,
                // and one for a value that equals none.
                let literals = distinct(rows.iter().filter_map(|r| match r.patterns[c] {
                    Some(Pattern::Number {
                        magnitude,
                        negative,
                        ..
                    }) => Some(signed(*magnitude, *negative)),
                    _ => None,
                }));
                let count = literals.len();
                let blocks: Vec<usize> = (0..=count).map(|_| out.new_block()).collect();
                let mut sorted: Vec<(i128, usize)> = literals
                    .iter()
                    .copied()
                    .zip(blocks.iter().copied())
                    .collect();
                sorted.sort();
                out.pick(value[0], t.signed(), bits_of(*t), &sorted, blocks[count]);
                for (j, block) in blocks.iter().enumerate() {
                    let (rows, cols) = around(Vec::new(), &|p| match p {
                        None => Some(Vec::new()),
                        Some(Pattern::Number {
                            magnitude,
                            negative,
                            ..
                        }) if literals.get(j) == Some(&signed(*magnitude, *negative)) => {
                            Some(Vec::new())
                        }
                        Some(_) => None,
                    });
                    out.current = *block;
                    self.decide(out, arms, leaves, rows, &cols);
                }
            }
            Ty::Text => {
                // The literals are tried in order, each by comparing bytes.
                let literals = distinct(rows.iter().filter_map(|r| match r.patterns[c] {
                    Some(Pattern::Text { text, .. }) => Some(text.clone()),
                    _ => None,
                }));
                for literal in &literals {
                    let mut args = value.clone();
                    args.extend(self.text(out, literal));
                    let same = self.call_runtime(
                        out,
                        Runtime::Equal,
                        args,
                        vec![form::Type::Int(form::Bits::B8)],
                    )[0];
                    let blocks = out.switch(same, 2);
                    let (rows, cols) = around(Vec::new(), &|p| match p {
                        None => Some(Vec::new()),
                        Some(Pattern::Text { text, .. }) if text == literal => Some(Vec::new()),
                        Some(_) => None,
                    });
                    out.current = blocks[1];
                    self.decide(out, arms, leaves, rows, &cols);
                    out.current = blocks[0];
                }
                let (rows, cols) = around(Vec::new(), &|p| match p {
                    None => Some(Vec::new()),
                    Some(_) => None,
                });
                self.decide(out, arms, leaves, rows, &cols);
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
        for (name, ty, value) in row.binds {
            out.define(&name, ty, value);
        }
        let gives = self.block(out, &arms.arms[row.arm].body, arms.env);
        let names = self.names(out, arms);
        // A block defines names and assigns to those before it, and nothing
        // else: put each frame back as it was.
        for (f, (frame, was)) in out.frames.iter_mut().zip(arms.was).enumerate() {
            frame.truncate(arms.before[f]);
            for (bound, was) in frame.iter_mut().zip(was) {
                if bound.values != *was {
                    bound.values.clone_from(was);
                }
            }
        }
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

/// The items, each once, in the order they first come.
fn distinct<T: PartialEq>(items: impl Iterator<Item = T>) -> Vec<T> {
    let mut out: Vec<T> = Vec::new();
    for item in items {
        if !out.contains(&item) {
            out.push(item);
        }
    }
    out
}

/// The size of the float that a type of the checker says.
fn float_bits(float: Float) -> form::FloatBits {
    match float {
        Float::F32 => form::FloatBits::F32,
        Float::F64 => form::FloatBits::F64,
    }
}
