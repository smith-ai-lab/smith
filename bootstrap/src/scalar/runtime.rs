//! What a program needs at run time, written once in the scalar form, so
//! that no target has a copy of it: joining texts, writing a text as a
//! string literal, writing an integer as its digits, and saying whether two
//! texts have the same bytes. A text is an address and a length, and a
//! program holds only the functions it reaches.

use super::words;
use super::{Binary, Bits, Builder, Compare, Function, Type, Value};

pub(super) const ADDRESS: Type = Type::Address;
pub(super) const LENGTH: Type = Type::Int(Bits::B64);
pub(super) const BYTE: Type = Type::Int(Bits::B8);
/// What a text is.
const TEXT: [Type; 2] = [ADDRESS, LENGTH];

/// A function of the runtime.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Runtime {
    /// Takes an address to write to, an address to read from, and a count,
    /// and copies that many bytes. It gives nothing.
    Copy,
    /// Takes that many texts, each as an address and a length, and gives a
    /// new text that is them one after another. Memory is asked of the
    /// system only when the text is not empty.
    Join(usize),
    /// Takes a text, and gives a new text that is it written as a string
    /// literal: in double quotes, with a backslash before each `"`, `\`,
    /// `{`, and `}`, and each line break and tab written `\n` and `\t`.
    Escape,
    /// Takes two texts, and gives a byte that is 1 if they have the same
    /// bytes and 0 if they do not.
    Equal,
    /// Takes a 64-bit integer and a byte, and gives a new text that is the
    /// integer's digits in decimal, after a minus sign if the byte is not 0.
    Digits,
    /// Takes an integer of the size, and gives a new text that is its digits
    /// in decimal, read as a signed number or as an unsigned one.
    Decimal { bits: Bits, signed: bool },
    /// Takes a list whose elements are the scalars of the types, as the
    /// address of its elements, its size, and how many elements the memory at
    /// that address has room for, then the scalars of the element to add, and
    /// a byte that is not 0 when the list
    /// may grow where it is. It gives the list with the element after the
    /// last. The element is written in place when the list may grow there
    /// and there is room, and otherwise the elements are copied into memory
    /// with room for twice as many, or for one when there are none.
    Push(Vec<Type>),
    /// Is where a program that takes the words it was started with starts. It
    /// takes what the system gives, as `Arguments` does, and calls the
    /// function `main` with the list of words that `Arguments` gives. It gives
    /// nothing.
    Start { main: usize },
    /// Takes what the system gives a program that starts: a number, the
    /// address of that many addresses of texts that each end with a byte 0,
    /// and the address of a command line, and gives the list of the words the
    /// program was started with, each a text, as a list is held. The first
    /// word the system gives is the program's name, which is not one of them.
    /// A number of all ones says the system gave a command line and no words,
    /// as Windows does. Memory is asked of the system only when there is a
    /// word. A word that is not valid UTF-8 ends the program, as `Check` says,
    /// before the list is given.
    Arguments,
    /// Takes a number and the address of that many addresses of texts, each
    /// ending with a byte 0, and gives the list of every text after the first.
    Words,
    /// Takes the address of a command line, which is units of 16 bits that end
    /// with a unit 0, and gives the list of its words, each a text of UTF-8,
    /// split as the C runtime of Windows splits a command line. The first word
    /// is the program's name, which is not one of them.
    Line,
    /// Takes the address of a text that ends with a byte 0, and gives how many
    /// bytes it has before it.
    Measure,
    /// Takes an address, a count, and a byte, and writes the byte that many
    /// times from the address. It gives nothing.
    Fill,
    /// Takes an address, a unit of 16 bits of UTF-16 as a 64-bit integer, and
    /// the unit that follows it, and writes the unit as UTF-8 from the address,
    /// the two units together as one character when they are the two halves of
    /// one. It gives how many bytes it wrote and how many units it took. A unit
    /// that is half of a pair and has no other half is written as it is, in
    /// three bytes.
    Utf8,
    /// Takes the address of bytes and how many there are, and gives a byte that
    /// is 1 if they are valid UTF-8 and 0 if they are not.
    Valid,
    /// Takes a list of texts, and gives nothing when each is valid UTF-8. When
    /// one is not, it writes one line to standard error that says which, counted
    /// from 1, and ends the program with a failing status.
    Check,
}

/// The function of the runtime, where `place` gives the place in the
/// program of each function it calls.
pub fn build(kind: Runtime, place: &mut dyn FnMut(Runtime) -> usize) -> Function {
    match kind {
        Runtime::Copy => copy(),
        Runtime::Join(parts) => join(parts, place(Runtime::Copy)),
        Runtime::Escape => escape(),
        Runtime::Equal => equal(),
        Runtime::Digits => digits(),
        Runtime::Decimal { bits, signed } => decimal(bits, signed, place(Runtime::Digits)),
        Runtime::Push(scalars) => push(&scalars, place(Runtime::Copy)),
        Runtime::Start { main } => words::start(main, place(Runtime::Arguments)),
        Runtime::Arguments => words::arguments(
            place(Runtime::Words),
            place(Runtime::Line),
            place(Runtime::Check),
        ),
        Runtime::Words => words::words(place(Runtime::Measure)),
        Runtime::Line => words::line(place(Runtime::Fill), place(Runtime::Utf8)),
        Runtime::Measure => words::measure(),
        Runtime::Fill => words::fill(),
        Runtime::Utf8 => words::utf8(),
        Runtime::Valid => words::valid(),
        Runtime::Check => words::check(
            place(Runtime::Valid),
            place(Runtime::Decimal {
                bits: Bits::B64,
                signed: false,
            }),
            place(Runtime::Join(3)),
        ),
    }
}

pub(super) fn int(f: &mut Builder, n: u64) -> Value {
    f.int(Bits::B64, n)
}

pub(super) fn byte(f: &mut Builder, n: u8) -> Value {
    f.int(Bits::B8, n as u64)
}

/// The address `by` bytes after `address`.
pub(super) fn after(f: &mut Builder, address: Value, by: Value) -> Value {
    f.binary(Binary::Add, address, by)
}

/// A loop over the places `0..count`, which runs `body` for each with the
/// place, and the values it carries, and gives the values after the last.
/// `body` gives the values to carry to the next place.
pub(super) fn repeat(
    f: &mut Builder,
    count: Value,
    carried: Vec<Value>,
    body: impl FnOnce(&mut Builder, Value, Vec<Value>) -> Vec<Value>,
) -> Vec<Value> {
    let zero = int(f, 0);
    let mut types = vec![LENGTH];
    types.extend(carried.iter().map(|_| LENGTH));
    // The carried values are all lengths, which is all the runtime carries.
    let (head, params) = f.block(&types);
    let (inside, _) = f.block(&[]);
    let (done, results) = f.block(&types[1..]);
    let mut start = vec![zero];
    start.extend(carried);
    f.jump(head, start);
    f.at(head);
    let finished = f.compare(Compare::Equal, params[0], count);
    f.switch(
        finished,
        vec![(inside, vec![]), (done, params[1..].to_vec())],
    );
    f.at(inside);
    let next = body(f, params[0], params[1..].to_vec());
    let one = int(f, 1);
    let place = f.binary(Binary::Add, params[0], one);
    let mut args = vec![place];
    args.extend(next);
    f.jump(head, args);
    f.at(done);
    results
}

fn copy() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[ADDRESS, ADDRESS, LENGTH]);
    let (to, from, count) = (p[0], p[1], p[2]);
    repeat(&mut f, count, vec![], |f, i, _| {
        let source = after(f, from, i);
        let b = f.load(BYTE, source);
        let target = after(f, to, i);
        f.store(target, b);
        vec![]
    });
    f.ret(vec![]);
    f.finish(&[])
}

fn join(parts: usize, copy: usize) -> Function {
    let mut f = Builder::new();
    let params = f.params(&TEXT.repeat(parts));
    let mut total = int(&mut f, 0);
    for part in params.chunks(2) {
        total = f.binary(Binary::Add, total, part[1]);
    }
    let zero = int(&mut f, 0);
    let empty = f.compare(Compare::Equal, total, zero);
    let (some, _) = f.block(&[]);
    let (none, _) = f.block(&[]);
    f.switch(empty, vec![(some, vec![]), (none, vec![])]);
    f.at(none);
    let nothing = f.null();
    f.ret(vec![nothing, total]);
    f.at(some);
    let start = f.allocate(total);
    let mut at = start;
    for part in params.chunks(2) {
        f.call(copy, vec![at, part[0], part[1]], &[]);
        at = after(&mut f, at, part[1]);
    }
    f.ret(vec![start, total]);
    f.finish(&TEXT)
}

/// Whether the byte is one of the characters, as 1 or 0.
fn is(f: &mut Builder, c: Value, of: u8) -> Value {
    let k = byte(f, of);
    f.compare(Compare::Equal, c, k)
}

/// How many of the characters the byte is, which is 1 or 0.
fn is_one_of(f: &mut Builder, c: Value, of: &[u8]) -> Value {
    let mut sum = byte(f, 0);
    for &k in of {
        let same = is(f, c, k);
        sum = f.binary(Binary::Add, sum, same);
    }
    sum
}

/// The characters that are written after a backslash, which are the
/// characters themselves, and `n` and `t` for a line break and a tab.
const ESCAPED: &[u8] = b"\"\\{}\n\t";

fn escape() -> Function {
    let mut f = Builder::new();
    let p = f.params(&TEXT);
    let (from, count) = (p[0], p[1]);
    // How many bytes more than the text the literal has: one for each
    // character that is escaped, and the quotes.
    let extra = {
        let zero = int(&mut f, 0);
        repeat(&mut f, count, vec![zero], |f, i, carried| {
            let source = after(f, from, i);
            let c = f.load(BYTE, source);
            let one = is_one_of(f, c, ESCAPED);
            let more = f.widen(false, one, Bits::B64);
            vec![f.binary(Binary::Add, carried[0], more)]
        })[0]
    };
    let quotes = int(&mut f, 2);
    let written = f.binary(Binary::Add, count, extra);
    let total = f.binary(Binary::Add, written, quotes);
    let start = f.allocate(total);
    let quote = byte(&mut f, b'"');
    f.store(start, quote);
    let one = int(&mut f, 1);
    let first = after(&mut f, start, one);
    // The next place to write to is carried as an offset from `first`.
    let zero = int(&mut f, 0);
    let end = repeat(&mut f, count, vec![zero], |f, i, carried| {
        let source = after(f, from, i);
        let c = f.load(BYTE, source);
        let at = after(f, first, carried[0]);
        let special = is_one_of(f, c, ESCAPED);
        let (plain, _) = f.block(&[]);
        let (marked, _) = f.block(&[]);
        let (next, moved) = f.block(&[LENGTH]);
        f.switch(special, vec![(plain, vec![]), (marked, vec![])]);
        f.at(plain);
        f.store(at, c);
        let one = int(f, 1);
        let by_one = f.binary(Binary::Add, carried[0], one);
        f.jump(next, vec![by_one]);
        f.at(marked);
        let slash = byte(f, b'\\');
        f.store(at, slash);
        let line = is(f, c, b'\n');
        let tab = is(f, c, b'\t');
        let (n, t) = (byte(f, b'n'), byte(f, b't'));
        let for_tab = f.choose(tab, t, c);
        let named = f.choose(line, n, for_tab);
        let one = int(f, 1);
        let second = after(f, at, one);
        f.store(second, named);
        let two = int(f, 2);
        let by_two = f.binary(Binary::Add, carried[0], two);
        f.jump(next, vec![by_two]);
        f.at(next);
        vec![moved[0]]
    })[0];
    let last = after(&mut f, first, end);
    f.store(last, quote);
    f.ret(vec![start, total]);
    f.finish(&TEXT)
}

fn equal() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[ADDRESS, LENGTH, ADDRESS, LENGTH]);
    let (a, a_count, b, b_count) = (p[0], p[1], p[2], p[3]);
    let same_count = f.compare(Compare::Equal, a_count, b_count);
    let (differ, _) = f.block(&[]);
    let (look, _) = f.block(&[]);
    f.switch(same_count, vec![(differ, vec![]), (look, vec![])]);
    f.at(differ);
    let no = byte(&mut f, 0);
    f.ret(vec![no]);
    f.at(look);
    // The loop runs over the places, and a place where the bytes differ
    // leaves it, so it gives a byte that says whether none did.
    let zero = int(&mut f, 0);
    let (head, i) = f.block(&[LENGTH]);
    let (inside, _) = f.block(&[]);
    let (yes, _) = f.block(&[]);
    let (no_block, _) = f.block(&[]);
    let (next, _) = f.block(&[]);
    f.jump(head, vec![zero]);
    f.at(head);
    let finished = f.compare(Compare::Equal, i[0], a_count);
    f.switch(finished, vec![(inside, vec![]), (yes, vec![])]);
    f.at(inside);
    let (x, y) = (after(&mut f, a, i[0]), after(&mut f, b, i[0]));
    let (x, y) = (f.load(BYTE, x), f.load(BYTE, y));
    let same = f.compare(Compare::Equal, x, y);
    f.switch(same, vec![(no_block, vec![]), (next, vec![])]);
    f.at(next);
    let one = int(&mut f, 1);
    let place = f.binary(Binary::Add, i[0], one);
    f.jump(head, vec![place]);
    f.at(yes);
    let truth = byte(&mut f, 1);
    f.ret(vec![truth]);
    f.at(no_block);
    let falsehood = byte(&mut f, 0);
    f.ret(vec![falsehood]);
    f.finish(&[BYTE])
}

/// The integer divided by 10, rounded down, as a multiplication by a number
/// that is 2^67 over 10 rounded up, and a shift right by 67 places.
fn tenth(f: &mut Builder, n: Value) -> Value {
    let reciprocal = int(f, 0xCCCC_CCCC_CCCC_CCCD);
    let high = f.binary(Binary::MulHighU, n, reciprocal);
    // A multiplication by 2^61 that keeps the high half is a shift right by
    // 3 places.
    let eighth = int(f, 1 << 61);
    f.binary(Binary::MulHighU, high, eighth)
}

fn digits() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[LENGTH, BYTE]);
    let (number, negative) = (p[0], p[1]);
    // Counts the digits: at least one, and one more for each time the number
    // can be divided by 10 and leave something.
    let zero = int(&mut f, 0);
    let (count_head, counting) = f.block(&[LENGTH, LENGTH]);
    let (sized, size) = f.block(&[LENGTH]);
    f.jump(count_head, vec![number, zero]);
    f.at(count_head);
    let one = int(&mut f, 1);
    let more = f.binary(Binary::Add, counting[1], one);
    let rest = tenth(&mut f, counting[0]);
    let none_left = f.compare(Compare::Equal, rest, zero);
    f.switch(
        none_left,
        vec![(count_head, vec![rest, more]), (sized, vec![more])],
    );
    f.at(sized);
    let sign = f.widen(false, negative, Bits::B64);
    let total = f.binary(Binary::Add, size[0], sign);
    let start = f.allocate(total);
    // Writes the digits from the last, which is the number's remainder by 10.
    let (write_head, writing) = f.block(&[LENGTH, LENGTH]);
    let (signed, _) = f.block(&[]);
    let (finish, _) = f.block(&[]);
    let (done, _) = f.block(&[]);
    let one = int(&mut f, 1);
    let last = f.binary(Binary::Sub, total, one);
    f.jump(write_head, vec![number, last]);
    f.at(write_head);
    let rest = tenth(&mut f, writing[0]);
    let ten = int(&mut f, 10);
    let tens = f.binary(Binary::Mul, rest, ten);
    let digit = f.binary(Binary::Sub, writing[0], tens);
    let low = f.narrow(digit, Bits::B8);
    let zero_char = byte(&mut f, b'0');
    let character = f.binary(Binary::Add, low, zero_char);
    let at = after(&mut f, start, writing[1]);
    f.store(at, character);
    let one = int(&mut f, 1);
    let before = f.binary(Binary::Sub, writing[1], one);
    let none_left = f.compare(Compare::Equal, rest, zero);
    f.switch(
        none_left,
        vec![(write_head, vec![rest, before]), (finish, vec![])],
    );
    f.at(finish);
    f.switch(negative, vec![(done, vec![]), (signed, vec![])]);
    f.at(signed);
    let minus = byte(&mut f, b'-');
    f.store(start, minus);
    f.ret(vec![start, total]);
    f.at(done);
    f.ret(vec![start, total]);
    f.finish(&TEXT)
}

fn decimal(bits: Bits, signed: bool, digits: usize) -> Function {
    let mut f = Builder::new();
    let n = f.params(&[Type::Int(bits)])[0];
    let word = if bits == Bits::B64 {
        n
    } else {
        f.widen(signed, n, Bits::B64)
    };
    let (magnitude, negative) = if signed {
        let zero = int(&mut f, 0);
        let below = f.compare(Compare::LessS, word, zero);
        let opposite = f.binary(Binary::Sub, zero, word);
        (f.choose(below, opposite, word), below)
    } else {
        (word, byte(&mut f, 0))
    };
    let text = f.call(digits, vec![magnitude, negative], &TEXT);
    f.ret(text);
    f.finish(&TEXT)
}

/// What a list is: the address of its elements, its size, and its room.
pub(super) const LIST: [Type; 3] = [ADDRESS, LENGTH, LENGTH];

fn push(scalars: &[Type], copy: usize) -> Function {
    let (places, stride) = super::element(scalars);
    let mut f = Builder::new();
    let mut types = vec![ADDRESS, LENGTH, LENGTH];
    types.extend_from_slice(scalars);
    types.push(BYTE);
    let p = f.params(&types);
    let (address, size, room, in_place) = (p[0], p[1], p[2], p[p.len() - 1]);
    let item = &p[3..p.len() - 1];
    let width = int(&mut f, stride);
    let one = int(&mut f, 1);
    let left = f.compare(Compare::LessU, size, room);
    let here = f.binary(Binary::And, left, in_place);
    let (moved, _) = f.block(&[]);
    let (kept, _) = f.block(&[]);
    f.switch(here, vec![(moved, vec![]), (kept, vec![])]);
    f.at(kept);
    let used = f.binary(Binary::Mul, size, width);
    let at = after(&mut f, address, used);
    write(&mut f, at, item, &places);
    let bigger = f.binary(Binary::Add, size, one);
    f.ret(vec![address, bigger, room]);
    f.at(moved);
    let zero = int(&mut f, 0);
    let none = f.compare(Compare::Equal, size, zero);
    let twice = f.binary(Binary::Add, size, size);
    let places_of_room = f.choose(none, one, twice);
    let bytes = f.binary(Binary::Mul, places_of_room, width);
    let start = f.allocate(bytes);
    let used = f.binary(Binary::Mul, size, width);
    f.call(copy, vec![start, address, used], &[]);
    let at = after(&mut f, start, used);
    write(&mut f, at, item, &places);
    let bigger = f.binary(Binary::Add, size, one);
    f.ret(vec![start, bigger, places_of_room]);
    f.finish(&LIST)
}

/// Writes the scalars of an element, each at its place, from the address.
fn write(f: &mut Builder, at: Value, item: &[Value], places: &[u64]) {
    for (value, place) in item.iter().zip(places) {
        let by = int(f, *place);
        let to = after(f, at, by);
        f.store(to, *value);
    }
}
