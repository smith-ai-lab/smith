//! The functions of the runtime that give a program the words it was started
//! with, as a list of texts, from what the system gives it.

use super::runtime::{ADDRESS, BYTE, LENGTH, LIST, after, byte, int, repeat};
use super::{ARGUMENTS, Binary, Bits, Builder, Compare, Function, Type, Value};

pub(super) fn start(main: usize, arguments: usize) -> Function {
    let mut f = Builder::new();
    let p = f.params(&ARGUMENTS);
    let list = f.call(arguments, p, &LIST);
    f.call(main, list, &[]);
    f.ret(vec![]);
    f.finish(&[])
}

pub(super) fn arguments(words: usize, line: usize, check: usize) -> Function {
    let mut f = Builder::new();
    let p = f.params(&ARGUMENTS);
    let all_ones = int(&mut f, u64::MAX);
    let on_line = f.compare(Compare::Equal, p[0], all_ones);
    let (from_words, _) = f.block(&[]);
    let (from_line, _) = f.block(&[]);
    f.switch(on_line, vec![(from_words, vec![]), (from_line, vec![])]);
    f.at(from_words);
    let list = f.call(words, vec![p[0], p[1]], &LIST);
    f.call(check, list.clone(), &[]);
    f.ret(list);
    f.at(from_line);
    let list = f.call(line, vec![p[2]], &LIST);
    f.call(check, list.clone(), &[]);
    f.ret(list);
    f.finish(&LIST)
}

/// The texts after the first, each as its address and its length, in a list
/// that has room for exactly them.
pub(super) fn words(measure: usize) -> Function {
    let mut f = Builder::new();
    let p = f.params(&[LENGTH, ADDRESS]);
    let (given, at) = (p[0], p[1]);
    let zero = int(&mut f, 0);
    let one = int(&mut f, 1);
    let none = f.compare(Compare::Equal, given, zero);
    let after_name = f.binary(Binary::Sub, given, one);
    let count = f.choose(none, zero, after_name);
    let empty = f.compare(Compare::Equal, count, zero);
    let (some, _) = f.block(&[]);
    let (nothing, _) = f.block(&[]);
    f.switch(empty, vec![(some, vec![]), (nothing, vec![])]);
    f.at(nothing);
    let null = f.null();
    f.ret(vec![null, zero, zero]);
    f.at(some);
    let width = int(&mut f, 16);
    let bytes = f.binary(Binary::Mul, count, width);
    let list = f.allocate(bytes);
    let eight = int(&mut f, 8);
    repeat(&mut f, count, vec![], |f, i, _| {
        let one = int(f, 1);
        let next = f.binary(Binary::Add, i, one);
        let by = f.binary(Binary::Mul, next, eight);
        let place = after(f, at, by);
        let text = f.load(ADDRESS, place);
        let length = f.call(measure, vec![text], &[LENGTH])[0];
        let by = f.binary(Binary::Mul, i, width);
        let element = after(f, list, by);
        f.store(element, text);
        let place = after(f, element, eight);
        f.store(place, length);
        vec![]
    });
    f.ret(vec![list, count, count]);
    f.finish(&LIST)
}

pub(super) fn measure() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[ADDRESS]);
    let zero = int(&mut f, 0);
    let (head, i) = f.block(&[LENGTH]);
    f.jump(head, vec![zero]);
    f.at(head);
    let place = after(&mut f, p[0], i[0]);
    let b = f.load(BYTE, place);
    let nul = byte(&mut f, 0);
    let end = f.compare(Compare::Equal, b, nul);
    let (more, _) = f.block(&[]);
    let (done, _) = f.block(&[]);
    f.switch(end, vec![(more, vec![]), (done, vec![])]);
    f.at(more);
    let one = int(&mut f, 1);
    let next = f.binary(Binary::Add, i[0], one);
    f.jump(head, vec![next]);
    f.at(done);
    f.ret(vec![i[0]]);
    f.finish(&[LENGTH])
}

pub(super) fn fill() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[ADDRESS, LENGTH, BYTE]);
    let (to, count, b) = (p[0], p[1], p[2]);
    repeat(&mut f, count, vec![], |f, i, _| {
        let place = after(f, to, i);
        f.store(place, b);
        vec![]
    });
    f.ret(vec![]);
    f.finish(&[])
}

/// The number `x` shifted right by `by` bits, for `x` of 64 bits.
fn shift_right(f: &mut Builder, x: Value, by: u32) -> Value {
    let factor = int(f, 1 << (64 - by));
    f.binary(Binary::MulHighU, x, factor)
}

/// Writes at `at` plus `place` the byte that is `base`, which has the bits
/// above `mask` set, and the bits of `x` that `mask` keeps.
fn put(f: &mut Builder, at: Value, place: u64, base: u64, x: Value, mask: u64) {
    let kept = int(f, mask);
    let bits = f.binary(Binary::And, x, kept);
    let lead = int(f, base);
    let n = f.binary(Binary::Add, lead, bits);
    let b = f.narrow(n, Bits::B8);
    let by = int(f, place);
    let to = after(f, at, by);
    f.store(to, b);
}

pub(super) fn utf8() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[ADDRESS, LENGTH, LENGTH]);
    let (at, unit, next) = (p[0], p[1], p[2]);
    let one = int(&mut f, 1);
    let two = int(&mut f, 2);
    let (ascii, _) = f.block(&[]);
    let (above_ascii, _) = f.block(&[]);
    let (two_bytes, _) = f.block(&[]);
    let (above_two, _) = f.block(&[]);
    let (three_bytes, _) = f.block(&[]);
    let (four_bytes, _) = f.block(&[]);
    let limit = int(&mut f, 0x80);
    let small = f.compare(Compare::LessU, unit, limit);
    f.switch(small, vec![(above_ascii, vec![]), (ascii, vec![])]);
    f.at(ascii);
    let b = f.narrow(unit, Bits::B8);
    f.store(at, b);
    f.ret(vec![one, one]);
    f.at(above_ascii);
    let limit = int(&mut f, 0x800);
    let small = f.compare(Compare::LessU, unit, limit);
    f.switch(small, vec![(above_two, vec![]), (two_bytes, vec![])]);
    f.at(two_bytes);
    let high = shift_right(&mut f, unit, 6);
    put(&mut f, at, 0, 0xc0, high, 0x1f);
    put(&mut f, at, 1, 0x80, unit, 0x3f);
    f.ret(vec![two, one]);
    f.at(above_two);
    // Two units are one character when the first is a high half and the
    // second a low half.
    let halves = int(&mut f, 0xfc00);
    let first = f.binary(Binary::And, unit, halves);
    let second = f.binary(Binary::And, next, halves);
    let high_mark = int(&mut f, 0xd800);
    let low_mark = int(&mut f, 0xdc00);
    let is_high = f.compare(Compare::Equal, first, high_mark);
    let is_low = f.compare(Compare::Equal, second, low_mark);
    let pair = f.binary(Binary::And, is_high, is_low);
    f.switch(pair, vec![(three_bytes, vec![]), (four_bytes, vec![])]);
    f.at(three_bytes);
    let top = shift_right(&mut f, unit, 12);
    put(&mut f, at, 0, 0xe0, top, 0xf);
    let middle = shift_right(&mut f, unit, 6);
    put(&mut f, at, 1, 0x80, middle, 0x3f);
    put(&mut f, at, 2, 0x80, unit, 0x3f);
    let three = int(&mut f, 3);
    f.ret(vec![three, one]);
    f.at(four_bytes);
    let above_high = f.binary(Binary::Sub, unit, high_mark);
    let ten = int(&mut f, 1 << 10);
    let top_bits = f.binary(Binary::Mul, above_high, ten);
    let above_low = f.binary(Binary::Sub, next, low_mark);
    let plane = int(&mut f, 0x1_0000);
    let sum = f.binary(Binary::Add, top_bits, above_low);
    let code = f.binary(Binary::Add, plane, sum);
    let top = shift_right(&mut f, code, 18);
    put(&mut f, at, 0, 0xf0, top, 0x7);
    let upper = shift_right(&mut f, code, 12);
    put(&mut f, at, 1, 0x80, upper, 0x3f);
    let lower = shift_right(&mut f, code, 6);
    put(&mut f, at, 2, 0x80, lower, 0x3f);
    put(&mut f, at, 3, 0x80, code, 0x3f);
    let four = int(&mut f, 4);
    f.ret(vec![four, two]);
    f.finish(&[LENGTH, LENGTH])
}

const UNIT: Type = Type::Int(Bits::B16);

/// The unit of 16 bits `k` places after the address, as a 64-bit integer.
fn unit_at(f: &mut Builder, line: Value, k: Value) -> Value {
    let two = int(f, 2);
    let by = f.binary(Binary::Mul, k, two);
    let place = after(f, line, by);
    let u = f.load(UNIT, place);
    f.widen(false, u, Bits::B64)
}

/// Whether the unit is a space or a tab, which separate words.
fn is_blank(f: &mut Builder, c: Value) -> Value {
    let space = int(f, 32);
    let tab = int(f, 9);
    let a = f.compare(Compare::Equal, c, space);
    let b = f.compare(Compare::Equal, c, tab);
    f.binary(Binary::Add, a, b)
}

/// Writes `count` backslashes from the place `out` in `text`.
fn slashes(f: &mut Builder, fill: usize, text: Value, out: Value, count: Value) {
    let place = after(f, text, out);
    let b = int(f, 92);
    let b = f.narrow(b, Bits::B8);
    f.call(fill, vec![place, count, b], &[]);
}

/// How a command line of Windows is split, as the C runtime of Windows
/// splits it. The first word ends at the next space or tab, or, when it
/// begins with a quote, at the next quote, and has no escapes. In every
/// other word, a space or a tab outside quotes ends the word, and a quote
/// opens or closes quotes, unless backslashes come before it: each pair of
/// them is a backslash, and one that is left over makes the quote a
/// character of the word. Inside quotes, two quotes in a row are one quote of
/// the word, and stay inside. Backslashes before anything else are
/// themselves. A word that was quoted is a word even when it is empty.
pub(super) fn line(fill: usize, utf8: usize) -> Function {
    let mut f = Builder::new();
    let p = f.params(&[ADDRESS]);
    let text_of = p[0];
    let zero = int(&mut f, 0);
    let one = int(&mut f, 1);
    let quote = int(&mut f, 34);
    let slash = int(&mut f, 92);

    // How many units there are.
    let (count_head, k) = f.block(&[LENGTH]);
    f.jump(count_head, vec![zero]);
    f.at(count_head);
    let u = unit_at(&mut f, text_of, k[0]);
    let ends = f.compare(Compare::Equal, u, zero);
    let (count_more, _) = f.block(&[]);
    let (counted, n) = f.block(&[LENGTH]);
    f.switch(ends, vec![(count_more, vec![]), (counted, vec![k[0]])]);
    f.at(count_more);
    let next = f.binary(Binary::Add, k[0], one);
    f.jump(count_head, vec![next]);
    f.at(counted);
    let n = n[0];

    // Past the program's name.
    let first = unit_at(&mut f, text_of, zero);
    let quoted = f.compare(Compare::Equal, first, quote);
    let quoted_wide = f.widen(false, quoted, Bits::B64);
    let (name_head, j) = f.block(&[LENGTH]);
    f.jump(name_head, vec![quoted_wide]);
    f.at(name_head);
    let at_end = f.compare(Compare::Equal, j[0], n);
    let (name_look, _) = f.block(&[]);
    let (named, start) = f.block(&[LENGTH]);
    f.switch(at_end, vec![(name_look, vec![]), (named, vec![j[0]])]);
    f.at(name_look);
    let c = unit_at(&mut f, text_of, j[0]);
    let closes = f.compare(Compare::Equal, c, quote);
    let blank = is_blank(&mut f, c);
    let stops = f.choose(quoted, closes, blank);
    let (name_more, _) = f.block(&[]);
    let (name_stop, _) = f.block(&[]);
    f.switch(stops, vec![(name_more, vec![]), (name_stop, vec![])]);
    f.at(name_more);
    let next = f.binary(Binary::Add, j[0], one);
    f.jump(name_head, vec![next]);
    f.at(name_stop);
    // The closing quote belongs to the name.
    let past = f.binary(Binary::Add, j[0], quoted_wide);
    f.jump(named, vec![past]);
    f.at(named);
    let first_unit = start[0];

    // Room for every word and every byte: a unit makes at most three bytes,
    // and a word takes at least a unit.
    let none = f.compare(Compare::Equal, n, zero);
    let (some, _) = f.block(&[]);
    let (nothing, _) = f.block(&[]);
    f.switch(none, vec![(some, vec![]), (nothing, vec![])]);
    f.at(nothing);
    let null = f.null();
    f.ret(vec![null, zero, zero]);
    f.at(some);
    let three = int(&mut f, 3);
    let text_bytes = f.binary(Binary::Mul, n, three);
    let text = f.allocate(text_bytes);
    let width = int(&mut f, 16);
    let list_bytes = f.binary(Binary::Mul, n, width);
    let list = f.allocate(list_bytes);

    // The units, one at a time, carrying where the next is, where the next
    // byte goes and where the word began (both as places in the text), how
    // many words there are, whether the word is in quotes, how many
    // backslashes wait, and whether there is a word.
    let (head, s) = f.block(&[LENGTH; 7]);
    f.jump(head, vec![first_unit, zero, zero, zero, zero, zero, zero]);
    f.at(head);
    let (i, out, began, words, inside, waiting, has) = (s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
    let all = f.compare(Compare::Equal, i, n);
    let (step, _) = f.block(&[]);
    let (finish, _) = f.block(&[]);
    f.switch(all, vec![(step, vec![]), (finish, vec![])]);

    // Ends the word if there is one: the entry for it is written whether or
    // not there is, and the count grows by whether there is. Every case that
    // follows is in braces of its own, so that what it works out is not read
    // by the next.
    let flush = |f: &mut Builder, out: Value, began: Value, words: Value, has: Value| {
        let by = f.binary(Binary::Mul, words, width);
        let entry = after(f, list, by);
        let starts = after(f, text, began);
        f.store(entry, starts);
        let eight = int(f, 8);
        let place = after(f, entry, eight);
        let length = f.binary(Binary::Sub, out, began);
        f.store(place, length);
        f.binary(Binary::Add, words, has)
    };

    f.at(finish);
    {
        // Backslashes left at the end are themselves.
        slashes(&mut f, fill, text, out, waiting);
        let out = f.binary(Binary::Add, out, waiting);
        let words = flush(&mut f, out, began, words, has);
        f.ret(vec![list, words, n]);
    }

    f.at(step);
    let c = unit_at(&mut f, text_of, i);
    let next_i = f.binary(Binary::Add, i, one);
    let following = unit_at(&mut f, text_of, next_i);
    let is_slash = f.compare(Compare::Equal, c, slash);
    let is_quote = f.compare(Compare::Equal, c, quote);
    let outside = f.compare(Compare::Equal, inside, zero);
    let blank = is_blank(&mut f, c);
    let is_gap = f.binary(Binary::And, blank, outside);
    let kind = byte(&mut f, 0);
    let (k1, k2, k3) = (byte(&mut f, 1), byte(&mut f, 2), byte(&mut f, 3));
    let kind = f.choose(is_gap, k3, kind);
    let kind = f.choose(is_quote, k2, kind);
    let kind = f.choose(is_slash, k1, kind);
    let (other, _) = f.block(&[]);
    let (slash_case, _) = f.block(&[]);
    let (quote_case, _) = f.block(&[]);
    let (gap_case, _) = f.block(&[]);
    f.switch(
        kind,
        vec![
            (other, vec![]),
            (slash_case, vec![]),
            (quote_case, vec![]),
            (gap_case, vec![]),
        ],
    );

    // A backslash waits.
    f.at(slash_case);
    {
        let more = f.binary(Binary::Add, waiting, one);
        f.jump(head, vec![next_i, out, began, words, inside, more, one]);
    }

    // A quote: half the backslashes, and then it is a character of the word
    // when a backslash was left over, or when it comes inside quotes and
    // another quote follows, and otherwise it opens or closes quotes.
    f.at(quote_case);
    {
        let half = shift_right(&mut f, waiting, 1);
        let odd = f.binary(Binary::And, waiting, one);
        slashes(&mut f, fill, text, out, half);
        let out = f.binary(Binary::Add, out, half);
        let follows = f.compare(Compare::Equal, following, quote);
        let follows = f.widen(false, follows, Bits::B64);
        let doubled = f.binary(Binary::And, inside, follows);
        let even = f.compare(Compare::Equal, odd, zero);
        let even = f.widen(false, even, Bits::B64);
        let doubled = f.binary(Binary::And, doubled, even);
        let literal = f.binary(Binary::Add, odd, doubled);
        // The quote is written where the next byte goes, and the next byte
        // goes there again unless the quote is a character of the word.
        let place = after(&mut f, text, out);
        let q = f.narrow(quote, Bits::B8);
        f.store(place, q);
        let out = f.binary(Binary::Add, out, literal);
        // A quote that opens or closes quotes flips them, and one that is a
        // character of the word does not.
        let flips = f.binary(Binary::Sub, one, literal);
        let inside = f.binary(Binary::Xor, inside, flips);
        // A doubled quote takes the unit after it too.
        let past = f.binary(Binary::Add, next_i, doubled);
        f.jump(head, vec![past, out, began, words, inside, zero, one]);
    }

    // A space or a tab outside quotes ends the word.
    f.at(gap_case);
    {
        slashes(&mut f, fill, text, out, waiting);
        let out = f.binary(Binary::Add, out, waiting);
        let words = flush(&mut f, out, began, words, has);
        f.jump(head, vec![next_i, out, out, words, inside, zero, zero]);
    }

    // Anything else is a character of the word, after the backslashes.
    f.at(other);
    {
        slashes(&mut f, fill, text, out, waiting);
        let out = f.binary(Binary::Add, out, waiting);
        let place = after(&mut f, text, out);
        let wrote = f.call(utf8, vec![place, c, following], &[LENGTH, LENGTH]);
        let out = f.binary(Binary::Add, out, wrote[0]);
        let next = f.binary(Binary::Add, i, wrote[1]);
        f.jump(head, vec![next, out, began, words, inside, zero, one]);
    }
    f.finish(&LIST)
}

/// Whether the unit is a continuation byte of UTF-8, which is `0x80` to
/// `0xbf`.
fn continues(f: &mut Builder, b: Value) -> Value {
    let first = int(f, 0x80);
    let count = int(f, 0x40);
    let from = f.binary(Binary::Sub, b, first);
    f.compare(Compare::LessU, from, count)
}

/// Whether `x` is at least `from` and at most `to`, which are numbers.
fn between(f: &mut Builder, x: Value, from: u64, to: u64) -> Value {
    let low = int(f, from);
    let count = int(f, to - from + 1);
    let offset = f.binary(Binary::Sub, x, low);
    f.compare(Compare::LessU, offset, count)
}

/// The byte at `k` places after `i` in the bytes at `at`, as a 64-bit integer.
fn byte_at(f: &mut Builder, at: Value, i: Value, k: u64) -> Value {
    let by = int(f, k);
    let place = f.binary(Binary::Add, i, by);
    let address = after(f, at, place);
    let b = f.load(BYTE, address);
    f.widen(false, b, Bits::B64)
}

/// Whether the bytes are valid UTF-8, and where the first character that is not
/// valid begins, or how many bytes there are when all are. Valid UTF-8 is a sequence of characters, each of one to four bytes: a byte
/// below `0x80`, or a lead byte and then as many continuation bytes as it
/// says, with the second kept from the ranges that make a character that is
/// not written longer than it must be, is not half of a pair of UTF-16, and is
/// not above `0x10ffff`.
pub(super) fn valid() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[ADDRESS, LENGTH]);
    let (at, length) = (p[0], p[1]);
    let zero = int(&mut f, 0);
    let (head, i) = f.block(&[LENGTH]);
    let i = i[0];
    f.jump(head, vec![zero]);
    let (look, _) = f.block(&[]);
    let (good, _) = f.block(&[]);
    let (bad, _) = f.block(&[]);
    f.at(head);
    let at_end = f.compare(Compare::Equal, i, length);
    f.switch(at_end, vec![(look, vec![]), (good, vec![])]);
    f.at(good);
    let yes = byte(&mut f, 1);
    f.ret(vec![yes, length]);
    f.at(bad);
    let no = byte(&mut f, 0);
    f.ret(vec![no, i]);

    f.at(look);
    let one = int(&mut f, 1);
    let b = byte_at(&mut f, at, i, 0);
    let (ascii, _) = f.block(&[]);
    let (lead, _) = f.block(&[]);
    let small = between(&mut f, b, 0, 0x7f);
    f.switch(small, vec![(lead, vec![]), (ascii, vec![])]);
    f.at(ascii);
    let next = f.binary(Binary::Add, i, one);
    f.jump(head, vec![next]);

    // How many continuation bytes the lead byte asks for, and what the second
    // may be.
    f.at(lead);
    let two = between(&mut f, b, 0xc2, 0xdf);
    let three = between(&mut f, b, 0xe0, 0xef);
    let four = between(&mut f, b, 0xf0, 0xf4);
    let (n1, n2, n3) = (int(&mut f, 1), int(&mut f, 2), int(&mut f, 3));
    let need = f.choose(four, n3, zero);
    let need = f.choose(three, n2, need);
    let need = f.choose(two, n1, need);
    let mut low = int(&mut f, 0x80);
    let mut high = int(&mut f, 0xbf);
    for (lead_byte, from, to) in [
        (0xe0, 0xa0, 0xbf),
        (0xed, 0x80, 0x9f),
        (0xf0, 0x90, 0xbf),
        (0xf4, 0x80, 0x8f),
    ] {
        let is = is_byte(&mut f, b, lead_byte);
        let (a, z) = (int(&mut f, from), int(&mut f, to));
        low = f.choose(is, a, low);
        high = f.choose(is, z, high);
    }
    let none = f.compare(Compare::Equal, need, zero);
    let (room, _) = f.block(&[]);
    f.switch(none, vec![(room, vec![]), (bad, vec![])]);

    f.at(room);
    let last = f.binary(Binary::Add, i, need);
    let fits = f.compare(Compare::LessU, last, length);
    let (second, _) = f.block(&[]);
    f.switch(fits, vec![(bad, vec![]), (second, vec![])]);

    f.at(second);
    let s = byte_at(&mut f, at, i, 1);
    let span = f.binary(Binary::Sub, high, low);
    let count = f.binary(Binary::Add, span, one);
    let offset = f.binary(Binary::Sub, s, low);
    let inside = f.compare(Compare::LessU, offset, count);
    let (third, _) = f.block(&[]);
    f.switch(inside, vec![(bad, vec![]), (third, vec![])]);

    f.at(third);
    let past2 = f.binary(Binary::Add, i, n2);
    let (more, _) = f.block(&[]);
    let (fourth, _) = f.block(&[]);
    let (last_is_third, _) = f.block(&[]);
    let one_more = f.compare(Compare::Equal, need, one);
    f.switch(one_more, vec![(more, vec![]), (head, vec![past2])]);
    f.at(more);
    let c = byte_at(&mut f, at, i, 2);
    let ok = continues(&mut f, c);
    f.switch(ok, vec![(bad, vec![]), (last_is_third, vec![])]);
    f.at(last_is_third);
    let past3 = f.binary(Binary::Add, i, n3);
    let two_more = f.compare(Compare::Equal, need, n2);
    f.switch(two_more, vec![(fourth, vec![]), (head, vec![past3])]);
    f.at(fourth);
    let c = byte_at(&mut f, at, i, 3);
    let ok = continues(&mut f, c);
    let n4 = int(&mut f, 4);
    let past4 = f.binary(Binary::Add, i, n4);
    f.switch(ok, vec![(bad, vec![]), (head, vec![past4])]);
    f.finish(&[BYTE, LENGTH])
}

/// Whether the 64-bit integer is the number.
fn is_byte(f: &mut Builder, x: Value, n: u64) -> Value {
    let n = int(f, n);
    f.compare(Compare::Equal, x, n)
}

/// A new text of the bytes.
fn text_of(f: &mut Builder, bytes: &[u8]) -> [Value; 2] {
    let length = int(f, bytes.len() as u64);
    let at = f.allocate(length);
    for (k, b) in bytes.iter().enumerate() {
        let by = int(f, k as u64);
        let place = after(f, at, by);
        let b = byte(f, *b);
        f.store(place, b);
    }
    [at, length]
}

pub(super) fn check(valid: usize, decimal: usize, join: usize) -> Function {
    let mut f = Builder::new();
    let p = f.params(&LIST);
    let (list, size) = (p[0], p[1]);
    let zero = int(&mut f, 0);
    let one = int(&mut f, 1);
    let (head, i) = f.block(&[LENGTH]);
    let i = i[0];
    f.jump(head, vec![zero]);
    let (look, _) = f.block(&[]);
    let (done, _) = f.block(&[]);
    f.at(head);
    let at_end = f.compare(Compare::Equal, i, size);
    f.switch(at_end, vec![(look, vec![]), (done, vec![])]);
    f.at(done);
    f.ret(vec![]);

    f.at(look);
    let width = int(&mut f, 16);
    let by = f.binary(Binary::Mul, i, width);
    let element = after(&mut f, list, by);
    let text = f.load(ADDRESS, element);
    let eight = int(&mut f, 8);
    let place = after(&mut f, element, eight);
    let length = f.load(LENGTH, place);
    let ok = f.call(valid, vec![text, length], &[BYTE, LENGTH])[0];
    let (next, _) = f.block(&[]);
    let (invalid, _) = f.block(&[]);
    f.switch(ok, vec![(invalid, vec![]), (next, vec![])]);
    f.at(next);
    let after_it = f.binary(Binary::Add, i, one);
    f.jump(head, vec![after_it]);

    f.at(invalid);
    let number = f.binary(Binary::Add, i, one);
    let [first, first_length] = text_of(&mut f, b"argument ");
    let digits = f.call(decimal, vec![number], &[ADDRESS, LENGTH]);
    let [last, last_length] = text_of(&mut f, b" is not valid UTF-8\n");
    let line = f.call(
        join,
        vec![first, first_length, digits[0], digits[1], last, last_length],
        &[ADDRESS, LENGTH],
    );
    f.write_error(line[0], line[1]);
    let status = byte(&mut f, 1);
    f.exit(status);
    f.ret(vec![]);
    f.finish(&[])
}
