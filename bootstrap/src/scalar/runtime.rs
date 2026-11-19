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
const WORD: Type = Type::Int(Bits::B64);
/// What a text is.
const TEXT: [Type; 2] = [ADDRESS, LENGTH];

/// A function of the runtime.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Runtime {
    /// Takes an address to write to, an address to read from, and a count,
    /// and copies that many bytes, 8 at a time and then the fewer than 8
    /// that are left, one at a time. The two places do not overlap. It gives
    /// nothing.
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
    /// that address has room for, then the scalars of the element to add. It
    /// gives the list with the element after the last. The element is written
    /// in place when there is room, and otherwise the elements are copied into
    /// new memory with room for twice as many, or for one when there are none.
    Push(Vec<Type>),
    /// Takes the address of a list's elements, its size, and its room, which
    /// is not more than its size, and gives the address of new memory that
    /// holds the elements, and how many elements it has room for: twice the
    /// size, or one when the size is 0. Each element takes that many bytes.
    /// It is cold, as `Function::cold` says.
    Grow(u64),
    /// Takes what `Grow` takes and a number of elements, and gives what
    /// `Grow` gives, with room for twice the size, or for that number when it
    /// is more. It is cold, as `Grow` is.
    Reserve(u64),
    /// Takes a list whose elements are the scalars of the types, as `Push`
    /// takes it, then the address and the size of another list of the same
    /// elements, which it only reads. It gives the list with those elements
    /// after the last, with one test for room, and when there is none, one
    /// request for memory that holds them all, and one copy of them.
    Append(Vec<Type>),
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
    /// is 1 if they are valid UTF-8 and 0 if they are not, and where the first
    /// character that is not valid begins, or how many bytes there are.
    Valid,
    /// Takes a list of texts, and gives nothing when each is valid UTF-8. When
    /// one is not, it writes one line to standard error that says which, counted
    /// from 1, and ends the program with status 1.
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
        Runtime::Push(scalars) => {
            let (_, stride) = super::element(&scalars);
            push(&scalars, place(Runtime::Grow(stride)))
        }
        Runtime::Append(scalars) => {
            let (_, stride) = super::element(&scalars);
            append(
                stride,
                place(Runtime::Reserve(stride)),
                place(Runtime::Copy),
            )
        }
        Runtime::Grow(stride) => grow(stride, place(Runtime::Copy), false),
        Runtime::Reserve(stride) => grow(stride, place(Runtime::Copy), true),
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

pub(super) fn copy() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[ADDRESS, ADDRESS, LENGTH]);
    let (to, from, count) = (p[0], p[1], p[2]);
    // Two words in each round, which is 5 instructions for each 8 bytes: the
    // comparison, one step of the place, and for each word an address for the
    // read and one for the write, a load, and a store. A round that moved one
    // word would take 6, as two addresses cannot be compared and each is
    // found from the place. Every load and store may be at any address.
    let wide = {
        let mask = int(&mut f, !15);
        f.binary(Binary::And, count, mask)
    };
    let (head, params) = f.block(&[LENGTH]);
    let (inside, _) = f.block(&[]);
    let (tail, _) = f.block(&[]);
    let zero = int(&mut f, 0);
    let eight = int(&mut f, 8);
    let sixteen = int(&mut f, 16);
    f.jump(head, vec![zero]);
    f.at(head);
    let finished = f.compare(Compare::Equal, params[0], wide);
    f.switch(finished, vec![(inside, vec![]), (tail, vec![])]);
    f.at(inside);
    let (read, write) = (after(&mut f, from, params[0]), after(&mut f, to, params[0]));
    let first = f.load(WORD, read);
    f.store(write, first);
    let (read, write) = (after(&mut f, read, eight), after(&mut f, write, eight));
    let second = f.load(WORD, read);
    f.store(write, second);
    let next = f.binary(Binary::Add, params[0], sixteen);
    f.jump(head, vec![next]);
    // What is left is fewer than 16 bytes: a word when it has 8, and then
    // the bytes, which are fewer than 8.
    f.at(tail);
    let (read, write) = (after(&mut f, from, wide), after(&mut f, to, wide));
    let has_word = {
        let bit = f.binary(Binary::And, count, eight);
        f.compare(Compare::Equal, bit, zero)
    };
    let (word, _) = f.block(&[]);
    let (bytes, rest) = f.block(&[ADDRESS, ADDRESS]);
    f.switch(has_word, vec![(word, vec![]), (bytes, vec![write, read])]);
    f.at(word);
    let last = f.load(WORD, read);
    f.store(write, last);
    let (read, write) = (after(&mut f, read, eight), after(&mut f, write, eight));
    f.jump(bytes, vec![write, read]);
    f.at(bytes);
    let seven = int(&mut f, 7);
    let left = f.binary(Binary::And, count, seven);
    repeat(&mut f, left, vec![], |f, i, _| {
        let source = after(f, rest[1], i);
        let b = f.load(BYTE, source);
        let target = after(f, rest[0], i);
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

fn push(scalars: &[Type], grow: usize) -> Function {
    let (places, stride) = super::element(scalars);
    let mut f = Builder::new();
    let mut types = vec![ADDRESS, LENGTH, LENGTH];
    types.extend_from_slice(scalars);
    let p = f.params(&types);
    let (address, size, room) = (p[0], p[1], p[2]);
    let item = &p[3..];
    let left = f.compare(Compare::LessU, size, room);
    let (moved, _) = f.block(&[]);
    let (written, there) = f.block(&[ADDRESS, LENGTH]);
    f.switch(left, vec![(moved, vec![]), (written, vec![address, room])]);
    // A list is held by one name, so memory with room to spare is written
    // where it is. Memory with none is asked for by a function that is
    // never replaced by its instructions, so that what it needs is not held
    // in registers through the rounds of a loop that pushes.
    f.at(moved);
    let grown = f.call(grow, vec![address, size, room], &[ADDRESS, LENGTH]);
    f.jump(written, grown);
    f.at(written);
    let (address, room) = (there[0], there[1]);
    let width = int(&mut f, stride);
    let one = int(&mut f, 1);
    let used = f.binary(Binary::Mul, size, width);
    let at = after(&mut f, address, used);
    write(&mut f, at, item, &places);
    let bigger = f.binary(Binary::Add, size, one);
    f.ret(vec![address, bigger, room]);
    f.finish(&LIST)
}

/// Appends the elements of one list to another, as `Runtime::Append` says.
fn append(stride: u64, reserve: usize, copy: usize) -> Function {
    let mut f = Builder::new();
    let mut types = vec![ADDRESS, LENGTH, LENGTH];
    types.extend_from_slice(&[ADDRESS, LENGTH]);
    let p = f.params(&types);
    let (address, size, room, from, count) = (p[0], p[1], p[2], p[3], p[4]);
    let total = f.binary(Binary::Add, size, count);
    let short = f.compare(Compare::LessU, room, total);
    let (written, there) = f.block(&[ADDRESS, LENGTH]);
    let (moved, _) = f.block(&[]);
    f.switch(short, vec![(written, vec![address, room]), (moved, vec![])]);
    f.at(moved);
    let grown = f.call(
        reserve,
        vec![address, size, room, total],
        &[ADDRESS, LENGTH],
    );
    f.jump(written, grown);
    f.at(written);
    let (address, room) = (there[0], there[1]);
    let width = int(&mut f, stride);
    let used = f.binary(Binary::Mul, size, width);
    let at = after(&mut f, address, used);
    let bytes = f.binary(Binary::Mul, count, width);
    // What is added is often a few bytes, which a call of `Copy` and its
    // loops cost more than they move.
    let limit = int(&mut f, 17);
    let few = f.compare(Compare::LessU, bytes, limit);
    let (many, _) = f.block(&[]);
    let (little, _) = f.block(&[]);
    let (done, _) = f.block(&[]);
    f.switch(few, vec![(many, vec![]), (little, vec![])]);
    f.at(many);
    f.call(copy, vec![at, from, bytes], &[]);
    f.jump(done, vec![]);
    f.at(little);
    copy_few(&mut f, at, from, bytes, done);
    f.at(done);
    f.ret(vec![address, total, room]);
    f.finish(&LIST)
}

/// Copies `n` bytes, which are not more than 16, with at most two loads and
/// two stores that lie over each other, so that no loop is run, and goes on
/// at `done`. The two places do not overlap.
fn copy_few(f: &mut Builder, to: Value, from: Value, n: Value, done: usize) {
    let zero = int(f, 0);
    let (some, _) = f.block(&[]);
    let (wide, _) = f.block(&[]);
    let (narrow, _) = f.block(&[]);
    let (middle, _) = f.block(&[]);
    let (tiny, _) = f.block(&[]);
    let none = f.compare(Compare::Equal, n, zero);
    f.switch(none, vec![(some, vec![]), (done, vec![])]);
    f.at(some);
    let eight = int(f, 8);
    let under_eight = f.compare(Compare::LessU, n, eight);
    f.switch(under_eight, vec![(wide, vec![]), (narrow, vec![])]);
    // From 8 to 16 bytes: the first 8 and the last 8.
    f.at(wide);
    overlapping(f, to, from, n, 8, WORD);
    f.jump(done, vec![]);
    f.at(narrow);
    let four = int(f, 4);
    let under_four = f.compare(Compare::LessU, n, four);
    f.switch(under_four, vec![(middle, vec![]), (tiny, vec![])]);
    // From 4 to 7 bytes: the first 4 and the last 4.
    f.at(middle);
    overlapping(f, to, from, n, 4, Type::Int(Bits::B32));
    f.jump(done, vec![]);
    // From 1 to 3 bytes: the first, the middle, and the last.
    f.at(tiny);
    let one = int(f, 1);
    let half = f.binary(Binary::ShiftRightU, n, one);
    let last = f.binary(Binary::Sub, n, one);
    for place in [zero, half, last] {
        let source = after(f, from, place);
        let b = f.load(BYTE, source);
        let target = after(f, to, place);
        f.store(target, b);
    }
    f.jump(done, vec![]);
}

/// Copies the first `width` bytes and the last `width` of `n`, which is at
/// least `width`.
fn overlapping(f: &mut Builder, to: Value, from: Value, n: Value, width: u64, ty: Type) {
    let w = int(f, width);
    let tail = f.binary(Binary::Sub, n, w);
    let (read_end, write_end) = (after(f, from, tail), after(f, to, tail));
    let first = f.load(ty, from);
    let last = f.load(ty, read_end);
    f.store(to, first);
    f.store(write_end, last);
}

/// Gives a list whose memory has room for twice as many elements of the
/// width as it holds, or for one when it holds none, or for the number it is
/// given when `wanted` and that is more, with its elements in it, and how many
/// it has room for. When the list's memory is the last that
/// was taken from the stack the request is made in, the room it lacks is
/// taken after it, and nothing is copied.
fn grow(stride: u64, copy: usize, wanted: bool) -> Function {
    let mut f = Builder::new();
    f.cold();
    let mut types = vec![ADDRESS, LENGTH, LENGTH];
    if wanted {
        types.push(LENGTH);
    }
    let p = f.params(&types);
    let (address, size, room) = (p[0], p[1], p[2]);
    let width = int(&mut f, stride);
    let one = int(&mut f, 1);
    let zero = int(&mut f, 0);
    let none = f.compare(Compare::Equal, size, zero);
    let twice = f.binary(Binary::Add, size, size);
    let places_of_room = f.choose(none, one, twice);
    // A list that is given more than it has room for is given room for them.
    let places_of_room = match wanted {
        true => {
            let short = f.compare(Compare::LessU, places_of_room, p[3]);
            f.choose(short, p[3], places_of_room)
        }
        false => places_of_room,
    };
    let asked = f.binary(Binary::Mul, places_of_room, width);
    let held = f.binary(Binary::Mul, room, width);
    let (plain, _) = f.block(&[]);
    let (peek, _) = f.block(&[]);
    let (extend, _) = f.block(&[]);
    let (done, _) = f.block(&[]);
    // Two places cannot be compared, so they are compared as numbers, which
    // takes the last 16 bytes of the list's memory for a moment. A list of
    // fewer is copied, which costs nothing.
    let sixteen = int(&mut f, 16);
    let roomy = f.compare(Compare::LessU, sixteen, held);
    f.switch(roomy, vec![(plain, vec![]), (peek, vec![])]);
    f.at(peek);
    let end = after(&mut f, address, held);
    let top = f.allocate(zero);
    let last = same_place(&mut f, address, held, top, end);
    f.switch(last, vec![(plain, vec![]), (extend, vec![])]);
    f.at(extend);
    let extra = f.binary(Binary::Sub, asked, held);
    let flag = int(&mut f, super::handler::TRY);
    let tried = f.binary(Binary::Or, extra, flag);
    let more = f.allocate(tried);
    let joined = same_place(&mut f, address, held, more, end);
    f.switch(joined, vec![(plain, vec![]), (done, vec![])]);
    f.at(done);
    // Where the memory taken is the end of the list, the list is as it was.
    let base = f.choose(joined, address, more);
    f.ret(vec![base, places_of_room]);
    f.at(plain);
    let data = f.allocate(asked);
    let used = f.binary(Binary::Mul, size, width);
    f.call(copy, vec![data, address, used], &[]);
    f.ret(vec![data, places_of_room]);
    f.finish(&[ADDRESS, LENGTH])
}

/// Whether two addresses are the same, given a list's memory of `held` bytes,
/// at least 16, whose last two words are held and put back.
fn same_place(f: &mut Builder, address: Value, held: Value, a: Value, b: Value) -> Value {
    let sixteen = int(f, 16);
    let eight = int(f, 8);
    let back = f.binary(Binary::Sub, held, sixteen);
    let first = after(f, address, back);
    let second = after(f, first, eight);
    let (kept_a, kept_b) = (f.load(WORD, first), f.load(WORD, second));
    f.store(first, a);
    f.store(second, b);
    let (x, y) = (f.load(WORD, first), f.load(WORD, second));
    f.store(first, kept_a);
    f.store(second, kept_b);
    f.compare(Compare::Equal, x, y)
}

/// Writes the scalars of an element, each at its place, from the address.
fn write(f: &mut Builder, at: Value, item: &[Value], places: &[u64]) {
    for (value, place) in item.iter().zip(places) {
        let by = int(f, *place);
        let to = after(f, at, by);
        f.store(to, *value);
    }
}
