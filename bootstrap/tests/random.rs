//! `std.random`: a `Key` is made from a seed or from `entropy`, used once, and
//! gives bits and two keys that are the `knowledge/library/random.md` says,
//! which is the generator Threefry-2x64 with 20 rounds, checked here against
//! the published vectors and against a model of it written in Rust.

use smith::{check, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "threefry_gives_the_published_vectors",
        threefry_gives_the_published_vectors,
    ),
    (
        "bits_and_split_and_seeded_give_what_the_model_of_the_generator_gives_for_every_seed",
        bits_and_split_and_seeded_give_what_the_model_of_the_generator_gives_for_every_seed,
    ),
    (
        "a_key_is_used_once",
        a_key_is_used_once,
    ),
];

pub const MACHINE: &[Test] = &[(
    "every_executable_for_this_machine_that_draws_from_a_fresh_key_prints_different_bits_each_run",
    every_executable_for_this_machine_that_draws_from_a_fresh_key_prints_different_bits_each_run,
)];


fn rotate(x: u64, by: u32) -> u64 {
    x.rotate_left(by)
}

/// Threefry-2x64 with 20 rounds, as Random123 gives it, for a counter and a key.
fn threefry(counter: [u64; 2], key: [u64; 2]) -> [u64; 2] {
    const ROTATIONS: [u32; 8] = [16, 42, 12, 31, 16, 32, 24, 21];
    let third = key[0] ^ key[1] ^ 0x1BD1_1BDA_A9FC_1A22;
    let keys = [key[0], key[1], third];
    let mut x = [
        counter[0].wrapping_add(key[0]),
        counter[1].wrapping_add(key[1]),
    ];
    for round in 0..20 {
        x[0] = x[0].wrapping_add(x[1]);
        x[1] = rotate(x[1], ROTATIONS[round % 8]) ^ x[0];
        if round % 4 == 3 {
            let step = round / 4 + 1;
            x[0] = x[0].wrapping_add(keys[step % 3]);
            x[1] = x[1].wrapping_add(keys[(step + 1) % 3]).wrapping_add(step as u64);
        }
    }
    x
}

/// What `random.md` says `seeded`, `bits`, and `split` give, as a model.
fn seeded(seed: u64) -> [u64; 2] {
    threefry([3, 0], [seed, 0])
}

fn bits(key: [u64; 2]) -> u64 {
    threefry([0, 0], key)[0]
}

fn split(key: [u64; 2]) -> ([u64; 2], [u64; 2]) {
    (threefry([1, 0], key), threefry([2, 0], key))
}

/// What a program prints, by the evaluator, or the errors it is refused with.
fn printed(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    String::from_utf8(scalar::run(&program).stdout).unwrap()
}

const HEAD: &str = "use std.bits (u64)\nuse std.console (console, print)\nuse std.fmt (hex)\nuse std.random (Words, bits, seeded, split, threefry)\n\n";

/// The vectors of `kat_vectors` of Random123 for `threefry2x64 20`: the
/// counter and key of all zeros, of all ones, and of the digits of pi.
const VECTORS: [([u64; 2], [u64; 2], [u64; 2]); 3] = [
    (
        [0, 0],
        [0, 0],
        [0xc2b6_e3a8_c2c6_9865, 0x6f81_ed42_f350_084d],
    ),
    (
        [u64::MAX, u64::MAX],
        [u64::MAX, u64::MAX],
        [0xe02c_b7c4_d95d_277a, 0xd066_33d0_893b_8b68],
    ),
    (
        [0x243f_6a88_85a3_08d3, 0x1319_8a2e_0370_7344],
        [0xa409_3822_299f_31d0, 0x082e_fa98_ec4e_6c89],
        [0x263c_7d30_bb0f_0af1, 0x56be_8361_d331_1526],
    ),
];

/// The generator the page gives is the one Random123 publishes, for the three
/// vectors that file holds, and the Rust model is it too.
fn threefry_gives_the_published_vectors() {
    let mut body = String::from("fn main() ! console, alloc {\n");
    for (i, (counter, key, _)) in VECTORS.iter().enumerate() {
        body += &format!(
            "  r{i} = threefry(Words(first: {}, second: {}), Words(first: {}, second: {}))\n  print(\"{{hex(r{i}.first, 16, false)}} {{hex(r{i}.second, 16, false)}}\")\n",
            counter[0], counter[1], key[0], key[1]
        );
    }
    body += "}\n";
    let got = printed(&format!("{HEAD}{body}"));
    let want: String = VECTORS
        .iter()
        .map(|(_, _, out)| format!("{:016x} {:016x}\n", out[0], out[1]))
        .collect();
    assert_eq!(got, want);
    for (counter, key, out) in VECTORS {
        assert_eq!(threefry(counter, key), out, "the model");
    }
}

/// Seeds, as eight numbers of 64 bits.
fn seeds(g: &mut Gen) -> Vec<u64> {
    (0..8)
        .map(|_| {
            (0..4).fold(0u64, |n, _| n << 16 | g.below(1 << 16) as u64)
        })
        .collect()
}

/// For every seed, the bits of the key `seeded` makes, and of the two keys
/// `split` makes of it, are those the model gives; and the three differ.
fn bits_and_split_and_seeded_give_what_the_model_of_the_generator_gives_for_every_seed() {
    generate::check(160, 12, seeds, |seeds| {
        let mut body = String::from("fn main() ! console, alloc {\n");
        for (i, seed) in seeds.iter().enumerate() {
            body += &format!(
                "  (a{i}, b{i}) = split(seeded({seed}))\n  print(\"{{bits(seeded({seed}))}} {{bits(a{i})}} {{bits(b{i})}}\")\n"
            );
        }
        body += "}\n";
        let got = printed(&format!("{HEAD}{body}"));
        let want: String = seeds
            .iter()
            .map(|&seed| {
                let key = seeded(seed);
                let (a, b) = split(key);
                let line = [bits(key), bits(a), bits(b)];
                assert!(
                    line[0] != line[1] && line[0] != line[2] && line[1] != line[2],
                    "{seed}: {line:?}"
                );
                format!("{} {} {}\n", line[0], line[1], line[2])
            })
            .collect();
        assert_eq!(got, want);
        // The same seed twice is the same key.
        for &seed in seeds {
            assert_eq!(seeded(seed), seeded(seed));
        }
    });
}

/// A key given to `bits` or `split` is given away, so a second use is refused.
fn a_key_is_used_once() {
    let twice = format!("{HEAD}fn main() ! console, alloc {{\n  key = seeded(1)\n  print(\"{{bits(key)}} {{bits(key)}}\")\n}}\n");
    let Err(errors) = check(twice.as_bytes()) else {
        panic!("a key was used twice");
    };
    assert!(
        errors.iter().any(|e| e.message.contains("key")),
        "{errors:?}"
    );
    let once = format!("{HEAD}fn main() ! console, alloc {{\n  key = seeded(1)\n  print(\"{{bits(key)}}\")\n}}\n");
    assert!(check(once.as_bytes()).is_ok());
}

/// A program that prints the bits of a fresh key twice, by two keys.
const FRESH: &str = "use std.bits (u64)\nuse std.console (console, print)\nuse std.entropy (entropy)\nuse std.random (bits, fresh)\n\n\
fn main() ! console, entropy, alloc {\n  print(\"{bits(fresh())} {bits(fresh())}\")\n}\n";

/// For every target this machine runs, a fresh key is not the same twice in one
/// run, and not the same in two runs.
fn every_executable_for_this_machine_that_draws_from_a_fresh_key_prints_different_bits_each_run() {
    for target in targets::runnable().into_iter().filter(|t| t.is_machine_code()) {
        let bytes = targets::build(FRESH.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        targets::record(target, &[&bytes]);
        if targets::fingerprinting() {
            continue;
        }
        let said = |ran: scalar::Outcome| String::from_utf8(ran.stdout).unwrap();
        let (first, second) = (said(targets::run(&bytes)), said(targets::run(&bytes)));
        for line in [&first, &second] {
            let numbers: Vec<&str> = line.split_whitespace().collect();
            assert_eq!(numbers.len(), 2, "{}: {line:?}", target.name());
            assert_ne!(numbers[0], numbers[1], "{}", target.name());
        }
        assert_ne!(first, second, "{}", target.name());
    }
}
