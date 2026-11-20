//! The tests every target passes. They take the targets from
//! `Target::ALL`, the enum's own list of cases, so adding a case runs them
//! over it, and no test names a target.
//!
//! `MACHINE` holds the tests whose result depends on the kind of machine
//! they run on. Continuous integration runs them on each kind of machine a
//! target needs, and every other test once.

use std::cell::RefCell;
use std::fmt::Debug;
use std::path::{Path, PathBuf};

use smith::diag::Error;
use smith::scalar::{self, Bits, Builder, Constant, Op, Outcome, Type, Value};
use smith::target::Target;
use smith::{compile, lower};

use crate::Test;
use crate::choices;
use crate::enums;
use crate::generate::{self, Gen};
use crate::lambdas;
use crate::numbers;
use crate::programs;
use crate::reprs;
use crate::sha256;
use crate::text;
use crate::traits;
use crate::tries;
use crate::tuples;

pub const ONCE: &[Test] = &[
    (
        "every_target_builds_identical_bytes_twice_for_reprs",
        every_target_builds_identical_bytes_twice_for_reprs,
    ),
    (
        "every_target_builds_identical_bytes_twice_for_choices",
        every_target_builds_identical_bytes_twice_for_choices,
    ),
    (
        "every_target_builds_identical_bytes_twice_for_numbers",
        every_target_builds_identical_bytes_twice_for_numbers,
    ),
    (
        "every_target_builds_identical_bytes_twice_for_enums_and_text",
        every_target_builds_identical_bytes_twice_for_enums_and_text,
    ),
    (
        "every_target_builds_identical_bytes_twice",
        every_target_builds_identical_bytes_twice,
    ),
    ("no_test_names_a_target", no_test_names_a_target),
    ("the_hash_of_an_executable_is_sha256", the_hash_of_an_executable_is_sha256),
    (
        "every_executable_uses_only_thin_wrappers_over_the_kernel",
        every_executable_uses_only_thin_wrappers_over_the_kernel,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_does_what_the_evaluator_says",
        every_executable_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_traits_does_what_the_evaluator_says",
        every_executable_of_a_program_with_traits_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_enums_does_what_the_evaluator_says",
        every_executable_of_a_program_with_enums_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_tuples_does_what_the_evaluator_says",
        every_executable_of_a_program_with_tuples_does_what_the_evaluator_says,
    ),
    (
        "every_executable_keeps_a_var_a_lambda_assigns_however_the_function_calls_it",
        every_executable_keeps_a_var_a_lambda_assigns_however_the_function_calls_it,
    ),
    (
        "every_executable_of_a_program_with_lambdas_does_what_the_evaluator_says",
        every_executable_of_a_program_with_lambdas_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_with_and_try_does_what_the_evaluator_says",
        every_executable_of_a_program_with_with_and_try_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_numbers_does_what_the_evaluator_says",
        every_executable_of_a_program_with_numbers_does_what_the_evaluator_says,
    ),
    (
        "a_jump_gives_a_block_its_parameters_as_if_all_were_copied_at_once",
        a_jump_gives_a_block_its_parameters_as_if_all_were_copied_at_once,
    ),
    (
        "a_function_with_a_frame_of_any_size_up_to_4_pages_does_what_the_evaluator_says",
        a_function_with_a_frame_of_any_size_up_to_4_pages_does_what_the_evaluator_says,
    ),
    (
        "a_call_gives_its_arguments_and_takes_its_results_as_if_all_were_copied_at_once",
        a_call_gives_its_arguments_and_takes_its_results_as_if_all_were_copied_at_once,
    ),
    (
        "a_jump_that_swaps_two_values_swaps_them",
        a_jump_that_swaps_two_values_swaps_them,
    ),
    (
        "a_struct_bound_to_two_names_is_two_equal_structs",
        a_struct_bound_to_two_names_is_two_equal_structs,
    ),
    (
        "a_long_literal_is_printed_whole",
        a_long_literal_is_printed_whole,
    ),
    (
        "every_executable_of_a_program_with_text_does_what_the_evaluator_says",
        every_executable_of_a_program_with_text_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_reprs_does_what_the_evaluator_says",
        every_executable_of_a_program_with_reprs_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_choices_does_what_the_evaluator_says",
        every_executable_of_a_program_with_choices_does_what_the_evaluator_says,
    ),
    (
        "a_program_the_system_refuses_memory_ends_with_a_failing_status_and_does_nothing_more",
        a_program_the_system_refuses_memory_ends_with_a_failing_status_and_does_nothing_more,
    ),
    (
        "every_target_builds_the_cases_to_the_hashes_another_machine_made",
        every_target_builds_the_cases_to_the_hashes_another_machine_made,
    ),
];

const HELLO: &str = "use std.console (console, print)\n\nfn main() ! console { print(\"hello world\") }\n";

/// The targets the compiler builds for, in the order of the enum.
pub fn buildable() -> Vec<Target> {
    Target::ALL.to_vec()
}

/// A target the compiler can build for, for a test that needs one.
pub fn any() -> Target {
    buildable()[0]
}

/// The targets the compiler can build for that this machine can run. A
/// fingerprint is made by no machine in particular, so for it every target
/// is run.
pub fn runnable() -> Vec<Target> {
    buildable()
        .into_iter()
        .filter(|t| fingerprinting() || Target::host() == Some(*t))
        .collect()
}

/// How the compiler builds an executable.
pub type Build = fn(&[u8], Target) -> Result<Vec<u8>, Vec<Error>>;

/// What a fingerprint is made of: how the compiler builds, and what the
/// tests that depend on the machine would run for each target.
struct Fingerprint {
    build: Build,
    entries: Vec<(Target, String)>,
}

thread_local! {
    static FINGERPRINT: RefCell<Option<Fingerprint>> = const { RefCell::new(None) };
}

/// Whether the tests that depend on the machine are being read for what they
/// would run, as `fingerprint` does, instead of run.
pub fn fingerprinting() -> bool {
    FINGERPRINT.with_borrow(|f| f.is_some())
}

/// The executable for `target` that the tests that depend on the machine
/// run. Every such test gets its executables here, so a fingerprint sees
/// each one.
pub fn build(source: &[u8], target: Target) -> Result<Vec<u8>, Vec<Error>> {
    match FINGERPRINT.with_borrow(|f| f.as_ref().map(|f| f.build)) {
        Some(build) => build(source, target),
        None => compile(source, target),
    }
}

/// Notes what a test would run for `target`, when a fingerprint is being
/// made. The parts are an executable and whatever it is checked against.
pub fn record(target: Target, parts: &[&[u8]]) {
    FINGERPRINT.with_borrow_mut(|f| {
        if let Some(f) = f.as_mut() {
            let mut bytes = Vec::new();
            for part in parts {
                bytes.extend_from_slice(&(part.len() as u64).to_le_bytes());
                bytes.extend_from_slice(part);
            }
            f.entries.push((target, sha256::hex(&bytes)));
        }
    });
}

/// Notes the program `source` for every target the compiler builds for.
pub fn record_for_every_target(source: &[u8]) {
    for target in buildable() {
        let bytes = build(source, target).unwrap_or_else(|e| panic!("{e:?}"));
        record(target, &[&bytes]);
    }
}

/// Builds a program in the scalar form for `target`, and checks that its executable does what `said` says, or, for a
/// fingerprint, notes both.
pub fn scalar_runs_as(program: &scalar::Program, target: Target, said: &Outcome) {
    let bytes = target.emit(program);
    if fingerprinting() {
        record(target, &[&bytes, &said.stdout, &said.status.to_le_bytes()]);
    } else {
        assert_eq!(run(&bytes), *said, "{}", target.name());
    }
}

/// Builds `source` for `target` and checks that its executable does what
/// `said` says, or, for a fingerprint, notes both.
pub fn runs_as(source: &[u8], target: Target, said: &Outcome) {
    let bytes = build(source, target).unwrap_or_else(|e| panic!("{e:?}"));
    if fingerprinting() {
        record(target, &[&bytes, &said.stdout, &said.status.to_le_bytes()]);
    } else {
        assert_eq!(run(&bytes), *said, "{}", target.name());
    }
}

/// For each target the compiler builds for, a hash of every executable the
/// given tests, which are all of `machine_tests()` for a real fingerprint, would run for it, with what each is
/// checked against, built as `build` builds. Two compilers that give the
/// same hash for a target give those tests nothing new to find on any
/// machine that runs it. Nothing is run, so the result is the same on
/// every machine. The tests run once for every target, and no target's hash
/// depends on another's.
pub fn fingerprint(tests: &[Test], build: Build) -> Vec<(Target, String)> {
    FINGERPRINT.with_borrow_mut(|f| {
        *f = Some(Fingerprint {
            build,
            entries: Vec::new(),
        })
    });
    for (_, test) in tests {
        test();
    }
    let entries = FINGERPRINT.with_borrow_mut(|f| f.take().unwrap().entries);
    buildable()
        .into_iter()
        .map(|target| {
            let mut own: Vec<&str> = entries
                .iter()
                .filter(|(t, _)| *t == target)
                .map(|(_, e)| e.as_str())
                .collect();
            own.sort();
            (target, sha256::hex(own.join("\n").as_bytes()))
        })
        .collect()
}

/// Each target this machine does not run, with why.
pub fn not_run() -> Vec<String> {
    let buildable = buildable();
    Target::ALL
        .iter()
        .filter(|t| !runnable().contains(t))
        .map(|t| {
            let why = if buildable.contains(t) {
                format!("this machine is {}", Target::host_description())
            } else {
                "the compiler cannot build for it yet".to_string()
            };
            format!("{}: not run, as {why}", t.name())
        })
        .chain(
            (!crate::x86_level_missing::emulator()).then(|| {
                "an executable on a chip without the level x86-64-v3: not run, as qemu-x86_64 is not installed"
                    .to_string()
            }),
        )
        .collect()
}

/// Runs an executable for this machine and gives what it did.
pub fn run(bytes: &[u8]) -> Outcome {
    run_with(bytes, |path| std::process::Command::new(path))
}

/// Runs an executable for this machine, started with the words, and gives
/// what it did.
pub fn run_with_words(bytes: &[u8], words: &[String]) -> Outcome {
    run_with(bytes, |path| {
        let mut command = std::process::Command::new(path);
        command.args(words);
        command
    })
}

/// Runs an executable for this machine with the command `command` makes of
/// its path, and gives what it did.
fn run_with(bytes: &[u8], command: impl Fn(&Path) -> std::process::Command) -> Outcome {
    let dir = crate::scratch::path("run");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("program");
    crate::executable::write(&path, bytes);
    let output = crate::clock::output_within(&mut command(&path), false).unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    Outcome {
        stdout: output.stdout,
        status: output.status.code().unwrap_or(-1),
    }
}

/// Checks that every target builds identical bytes twice, for `cases`
/// generated programs.
fn builds_identical_bytes_twice<T: Debug>(
    seed: u64,
    cases: u64,
    generate: impl Fn(&mut Gen) -> T,
    source: impl Fn(&T) -> &[u8],
) {
    let targets = buildable();
    assert!(!targets.is_empty(), "the compiler builds for some target");
    generate::check(seed, cases, generate, |case| {
        for &target in &targets {
            let first = compile(source(case), target).unwrap();
            let second = compile(source(case), target).unwrap();
            assert!(first == second, "{} differs", target.name());
        }
    });
}

fn every_target_builds_identical_bytes_twice() {
    builds_identical_bytes_twice(6, 3, programs::program, |c| &c.source);
    builds_identical_bytes_twice(19, 2, traits::program, |c| c.source.as_bytes());
}

fn every_target_builds_identical_bytes_twice_for_enums_and_text() {
    builds_identical_bytes_twice(24, 2, enums::program, |c| c.source.as_bytes());
    builds_identical_bytes_twice(36, 2, text::program, |c| c.source.as_bytes());
}

fn every_target_builds_identical_bytes_twice_for_reprs() {
    builds_identical_bytes_twice(65, 1, reprs::program, |c| c.source.as_bytes());
}

fn every_target_builds_identical_bytes_twice_for_choices() {
    builds_identical_bytes_twice(69, 1, choices::program, |c| c.source.as_bytes());
}

fn every_target_builds_identical_bytes_twice_for_numbers() {
    builds_identical_bytes_twice(73, 1, numbers::program, |c| c.source.as_bytes());
}

/// The program in the scalar form that the targets are given: what the
/// source lowers to, after the pass.
fn after_the_pass(source: &[u8]) -> scalar::Program {
    scalar::passes(&lower(source).unwrap_or_else(|e| panic!("{e:?}")))
}

/// Checks, for `cases` generated programs, that each executable does what
/// the evaluator says its program does, after the pass.
fn every_executable_agrees<T: Debug>(
    seed: u64,
    cases: u64,
    generate: impl Fn(&mut Gen) -> T,
    source: impl Fn(&T) -> &[u8],
) {
    let targets = runnable();
    generate::check(seed, cases, generate, |case| {
        let source = source(case);
        let said = scalar::run(&after_the_pass(source));
        for &target in &targets {
            runs_as(source, target, &said);
        }
    });
}

fn every_executable_does_what_the_evaluator_says() {
    every_executable_agrees(3, 40, programs::program, |case| &case.source);
}

fn every_executable_of_a_program_with_traits_does_what_the_evaluator_says() {
    every_executable_agrees(18, 30, traits::program, |case| case.source.as_bytes());
}

fn every_executable_of_a_program_with_enums_does_what_the_evaluator_says() {
    every_executable_agrees(25, 30, enums::program, |case| case.source.as_bytes());
}

fn every_executable_of_a_program_with_tuples_does_what_the_evaluator_says() {
    every_executable_agrees(28, 30, tuples::program, |case| case.source.as_bytes());
}

fn every_executable_of_a_program_with_lambdas_does_what_the_evaluator_says() {
    every_executable_agrees(32, 20, lambdas::program, |case| case.source.as_bytes());
}

fn every_executable_keeps_a_var_a_lambda_assigns_however_the_function_calls_it() {
    let targets = runnable();
    generate::check(117, 40, lambdas::loop_program, |case| {
        let said = Outcome {
            stdout: case.stdout.clone(),
            status: 0,
        };
        for &target in &targets {
            runs_as(case.source.as_bytes(), target, &said);
        }
    });
}

fn every_executable_of_a_program_with_with_and_try_does_what_the_evaluator_says() {
    every_executable_agrees(43, 12, tries::program, |case| case.source.as_bytes());
}

fn a_struct_bound_to_two_names_is_two_equal_structs() {
    let targets = runnable();
    generate::check(13, 40, programs::shared_and_copied, |(shared, copied)| {
        let said = scalar::run(&after_the_pass(shared.as_bytes()));
        let copy = scalar::run(&after_the_pass(copied.as_bytes()));
        assert_eq!(said, copy);
        for &target in &targets {
            runs_as(shared.as_bytes(), target, &copy);
        }
    });
}

fn every_executable_of_a_program_with_text_does_what_the_evaluator_says() {
    every_executable_agrees(37, 20, text::program, |case| case.source.as_bytes());
}

fn every_executable_of_a_program_with_reprs_does_what_the_evaluator_says() {
    every_executable_agrees(64, 16, reprs::program, |case| case.source.as_bytes());
}

fn every_executable_of_a_program_with_numbers_does_what_the_evaluator_says() {
    every_executable_agrees(74, 16, numbers::program, |case| case.source.as_bytes());
}

fn every_executable_of_a_program_with_choices_does_what_the_evaluator_says() {
    every_executable_agrees(68, 16, choices::program, |case| case.source.as_bytes());
}

pub const TYPES: [Type; 5] = [
    Type::Int(Bits::B8),
    Type::Int(Bits::B16),
    Type::Int(Bits::B32),
    Type::Int(Bits::B64),
    Type::Address,
];

/// Starts values of the types, as `n`s say, from constants of the program
/// whose texts are `swap` and `x`.
pub fn starting(b: &mut Builder, types: &[Type], starts: &[u64]) -> Vec<Value> {
    types
        .iter()
        .zip(starts)
        .map(|(ty, n)| match ty {
            Type::Int(bits) => b.int(*bits, *n),
            Type::Float(_) | Type::Vector(_) => unreachable!("these programs have no floats"),
            Type::Address => {
                let text = Constant::Data(*n as usize % 2);
                b.emit(Op::Constant(text), &[Type::Address])[0]
            }
        })
        .collect()
}

/// Writes the values, each as 8 bytes: an integer widened, and an address
/// as the byte it points to.
pub fn write_all(b: &mut Builder, types: &[Type], values: &[Value]) {
    let total = b.int(Bits::B64, 8 * types.len() as u64);
    let buffer = b.allocate(total);
    for (i, (ty, v)) in types.iter().zip(values).enumerate() {
        let wide = match ty {
            Type::Int(Bits::B64) => *v,
            Type::Int(_) => b.widen(false, *v, Bits::B64),
            Type::Float(_) | Type::Vector(_) => unreachable!("these programs have no floats"),
            Type::Address => {
                let byte = b.load(Type::Int(Bits::B8), *v);
                b.widen(false, byte, Bits::B64)
            }
        };
        let offset = b.int(Bits::B64, 8 * i as u64);
        let at = b.binary(scalar::Binary::Add, buffer, offset);
        b.store(at, wide);
    }
    let call = scalar::System::Write(scalar::Stream::Stdout);
    b.emit(
        Op::System {
            call,
            args: vec![buffer, total],
        },
        &[],
    );
}

/// A program in the scalar form that holds values of the types, and goes
/// round a loop `rounds` times, in which each value is given the one that
/// was at the place `sources` says in the round before, all at once. It
/// writes the values at the end, each as 8 bytes.
pub fn rotating(types: &[Type], starts: &[u64], sources: &[usize], rounds: u64) -> scalar::Program {
    let int = |b: &mut Builder, n: u64| b.int(Bits::B64, n);
    let mut b = Builder::new();
    let mut first = vec![int(&mut b, 0)];
    first.extend(starting(&mut b, types, starts));
    let mut held = vec![Type::Int(Bits::B64)];
    held.extend(types);
    let (head, in_head) = b.block(&held);
    let (again, _) = b.block(&[]);
    let (done, in_done) = b.block(types);
    b.jump(head, first);
    b.at(head);
    let limit = int(&mut b, rounds);
    let finished = b.compare(scalar::Compare::Equal, in_head[0], limit);
    b.switch(
        finished,
        vec![(again, vec![]), (done, in_head[1..].to_vec())],
    );
    b.at(again);
    let one = int(&mut b, 1);
    let next = b.binary(scalar::Binary::Add, in_head[0], one);
    let mut carried = vec![next];
    carried.extend(sources.iter().map(|s| in_head[1 + s]));
    b.jump(head, carried);
    b.at(done);
    write_all(&mut b, types, &in_done);
    b.ret(vec![]);
    let program = scalar::Program {
        data: vec![b"swap".to_vec(), b"x".to_vec()],
        functions: vec![b.finish(&[])],
        main: 0,
    };
    assert_eq!(scalar::check(&program), Ok(()));
    program
}

fn a_jump_gives_a_block_its_parameters_as_if_all_were_copied_at_once() {
    let targets = runnable();
    let generate = |g: &mut Gen| {
        let count = 2 + g.below(40);
        let types: Vec<Type> = (0..count).map(|_| *g.pick(&TYPES)).collect();
        let starts: Vec<u64> = (0..count)
            .map(|_| (g.below(1 << 20) as u64 + 1).wrapping_mul(0x9e37_79b9_7f4a_7c15))
            .collect();
        // Each value comes from one of the same type, and two may come from
        // the same one.
        let sources: Vec<usize> = (0..count)
            .map(|i| {
                let same: Vec<usize> = (0..count).filter(|j| types[*j] == types[i]).collect();
                *g.pick(&same)
            })
            .collect();
        rotating(&types, &starts, &sources, g.below(4) as u64)
    };
    generate::check(80, 100, generate, |program| {
        let said = scalar::run(program);
        for &target in &targets {
            scalar_runs_as(program, target, &said);
        }
    });
}

/// A function holds as many values at once as it takes to need a frame of
/// some pages, and writes them. A system that gives a program its stack one
/// page at a time must have the frame reserved a page at a time, so that no
/// page is passed over. The frame is 4 pages in the case that is always
/// run, and any size up to that in the others. A probe that skipped a page
/// is caught by any frame of two pages or more, and 4 keeps the test well
/// under the limit, because the allocator takes time that grows faster than
/// the number of values the function holds: 12 pages took about 230 ms to
/// build, 6 about 260 ms with the cases beside it, and 4 take about 100 ms.
fn a_function_with_a_frame_of_any_size_up_to_4_pages_does_what_the_evaluator_says() {
    const MOST: usize = 4;
    let targets = runnable();
    let holding = |pages: usize| {
        // The registers hold some values, and each other value takes 8 bytes
        // of the frame, 512 to a page.
        let count = 512 * pages + 32;
        let types = vec![Type::Int(Bits::B64); count];
        let starts: Vec<u64> = (0..count)
            .map(|i| (i as u64 + 1).wrapping_mul(0x9e37_79b9_7f4a_7c15))
            .collect();
        let sources: Vec<usize> = (0..count).map(|i| (i + 1) % count).collect();
        rotating(&types, &starts, &sources, 1)
    };
    let check = |pages: &usize| {
        let program = holding(*pages);
        let said = scalar::run(&program);
        for &target in &targets {
            scalar_runs_as(&program, target, &said);
        }
    };
    check(&MOST);
    generate::check(82, 2, |g: &mut Gen| 1 + g.below(MOST), check);
}

fn a_jump_that_swaps_two_values_swaps_them() {
    let int = Type::Int(Bits::B64);
    let program = rotating(&[int, int], &[1, 2], &[1, 0], 1);
    let said = scalar::run(&program);
    let swapped: Vec<u8> = [2u64, 1].iter().flat_map(|n| n.to_le_bytes()).collect();
    assert_eq!(said.stdout, swapped);
    for target in runnable() {
        scalar_runs_as(&program, target, &said);
    }
}

/// A program in the scalar form of three functions, each calling the next.
/// `main` calls `pass` with values of the types and writes what it gives
/// back. `pass` calls `leaf` with its own parameters in the order `order`
/// says, and gives back what `leaf` gave, in the order `back` says, and
/// after them its first parameter, which is alive across the call. `leaf`
/// gives back the parameters `picks` says, so that it gives back more
/// values than it has, or fewer, and two may be one.
fn passing(
    types: &[Type],
    starts: &[u64],
    order: &[usize],
    picks: &[usize],
    back: &[usize],
) -> scalar::Program {
    let given: Vec<Type> = picks.iter().map(|p| types[*p]).collect();

    let mut leaf = Builder::new();
    let own = leaf.params(types);
    leaf.ret(picks.iter().map(|p| own[*p]).collect());
    let leaf = leaf.finish(&given);

    let mut pass = Builder::new();
    let own = pass.params(types);
    let args = order.iter().map(|o| own[*o]).collect();
    let made = pass.call(0, args, &given);
    let mut gives: Vec<Value> = back.iter().map(|k| made[*k]).collect();
    gives.push(own[0]);
    let mut shape: Vec<Type> = back.iter().map(|k| given[*k]).collect();
    shape.push(types[0]);
    pass.ret(gives);
    let pass = pass.finish(&shape);

    let mut main = Builder::new();
    let first = starting(&mut main, types, starts);
    let got = main.call(1, first, &shape);
    write_all(&mut main, &shape, &got);
    main.ret(vec![]);
    let main = main.finish(&[]);

    let program = scalar::Program {
        data: vec![b"swap".to_vec(), b"x".to_vec()],
        functions: vec![leaf, pass, main],
        main: 2,
    };
    assert_eq!(scalar::check(&program), Ok(()));
    program
}

/// A call gives each argument to the function and takes each result as if
/// all were copied at once, with more arguments and results than there are
/// registers for them, and with a function that passes its own parameters on
/// in another order.
fn a_call_gives_its_arguments_and_takes_its_results_as_if_all_were_copied_at_once() {
    let targets = runnable();
    let generate = |g: &mut Gen| {
        let count = 1 + g.below(40);
        let types: Vec<Type> = (0..count).map(|_| *g.pick(&TYPES)).collect();
        let starts: Vec<u64> = (0..count)
            .map(|_| (g.below(1 << 20) as u64 + 1).wrapping_mul(0x9e37_79b9_7f4a_7c15))
            .collect();
        // Each argument is a parameter of the same type, and two may be the
        // same one.
        let order: Vec<usize> = (0..count)
            .map(|i| {
                let same: Vec<usize> = (0..count).filter(|j| types[*j] == types[i]).collect();
                *g.pick(&same)
            })
            .collect();
        let picks: Vec<usize> = (0..g.below(count + 8)).map(|_| g.below(count)).collect();
        let back: Vec<usize> = (0..g.below(picks.len() + 1))
            .map(|_| g.below(picks.len()))
            .collect();
        (types, starts, order, picks, back)
    };
    generate::check(81, 100, generate, |(types, starts, order, picks, back)| {
        // `back` reads the results of the leaf, which there may be none of.
        let back: Vec<usize> = if picks.is_empty() {
            Vec::new()
        } else {
            back.clone()
        };
        let program = passing(types, starts, order, picks, &back);
        let said = scalar::run(&program);
        for &target in &targets {
            scalar_runs_as(&program, target, &said);
        }
    });
}

/// The most memory, in KiB, a program run under a limit may map.
const LIMIT_KIB: usize = 64 * 1024;

/// The command that runs the program at `path` with the memory it may map
/// limited to `LIMIT_KIB`, on a machine where a test can limit it. Only
/// Linux enforces the limit a shell sets.
fn limited(path: &Path) -> Option<std::process::Command> {
    if !cfg!(target_os = "linux") {
        return None;
    }
    let mut command = std::process::Command::new("sh");
    command
        .arg("-c")
        .arg(format!("ulimit -v {LIMIT_KIB} && exec \"$0\""))
        .arg(path);
    Some(command)
}

/// As `tasks.md` says, running out of memory ends the task, and a failure
/// that reaches `main` ends the program with a failing status. The text the
/// program builds is larger than the limit lets it map, and everything
/// before it fits, so the program prints what comes before and no more.
fn a_program_the_system_refuses_memory_ends_with_a_failing_status_and_does_nothing_more() {
    // Whether a machine can limit memory is for the machine to say, so a
    // fingerprint, which no machine makes, counts the programs for every one.
    if !fingerprinting() && limited(Path::new("")).is_none() {
        return;
    }
    let targets = runnable();
    let bytes = LIMIT_KIB * 1024 * 2;
    let generate = |g: &mut generate::Gen| text::program_with_huge_text(g, bytes);
    generate::check(38, 20, generate, |(case, before)| {
        let source = case.source.as_bytes();
        for &target in &targets {
            let executable = build(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            if fingerprinting() {
                record(target, &[&executable, before]);
                continue;
            }
            let ran = run_with(&executable, |path| limited(path).expect("a limit"));
            // A status of its own, not the end a signal gives.
            assert!(ran.status > 0, "{}: {}", target.name(), ran.status);
            assert_eq!(
                String::from_utf8_lossy(&ran.stdout),
                String::from_utf8_lossy(before),
                "{}",
                target.name()
            );
        }
    });
}

fn a_long_literal_is_printed_whole() {
    let text = "abcdefghij".repeat(100_000);
    let source = format!("use std.console (console, print)\n\nfn main() ! console {{ print(\"{text}\") }}\n");
    let said = scalar::run(&after_the_pass(source.as_bytes()));
    assert_eq!(said.stdout, format!("{text}\n").into_bytes());
    for target in runnable() {
        runs_as(source.as_bytes(), target, &said);
    }
}

/// The `.smith` files in `tests/cases`, in order.
pub fn case_files() -> Vec<PathBuf> {
    let cases = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/cases");
    let mut found: Vec<_> = std::fs::read_dir(&cases)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "smith"))
        .collect();
    found.sort();
    assert!(!found.is_empty());
    found
}

/// Whether this run rewrites the expected output of the cases, as `SMITH_UPDATE=1 cargo test cases` asks.
pub fn updating() -> bool {
    std::env::var_os("SMITH_UPDATE").is_some_and(|v| !v.is_empty())
}

/// The variable that names the file of hashes another machine of the same run
/// made, as `.github/hash-cases.sh` prints them.
const HASHES: &str = "SMITH_HASHES";

/// A target's executables have the same bytes whichever machine builds them.
/// In `ci`, one job builds every case for every target and keeps the hash of
/// each, one line for each as `<target> <case file> <sha256>`, and every other
/// kind of machine builds the same cases and compares. A run that is given no
/// file has nothing to compare with, and checks nothing.
fn every_target_builds_the_cases_to_the_hashes_another_machine_made() {
    let Some(path) = std::env::var_os(HASHES) else {
        return;
    };
    // No target's bytes are read here, so a fingerprint has nothing to note.
    if fingerprinting() {
        return;
    }
    let made = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{HASHES}: {e}"));
    assert!(!made.trim().is_empty(), "{HASHES} names an empty file");
    let mut differ = Vec::new();
    let mut compared = 0;
    for line in made.lines() {
        let [target, file, hash] = line.split(' ').collect::<Vec<_>>()[..] else {
            panic!("{HASHES}: `{line}` is not `<target> <case> <hash>`");
        };
        let Some(target) = Target::from_name(target) else {
            panic!("{HASHES}: `{target}` is not a target");
        };
        let source = std::fs::read(case_files().iter().find(|p| p.ends_with(file)).unwrap())
            .unwrap();
        let bytes = build(&source, target).unwrap_or_else(|e| panic!("{file}: {e:?}"));
        compared += 1;
        if sha256::hex(&bytes) != hash {
            differ.push(format!("{} {file}", target.name()));
        }
    }
    assert!(
        differ.is_empty(),
        "on {}, {} of {compared} executables differ from those the run's first job made:\n{}",
        Target::host_description(),
        differ.len(),
        differ.join("\n"),
    );
}

fn the_hash_of_an_executable_is_sha256() {
    // Two of the examples FIPS 180-4 gives.
    assert_eq!(
        sha256::hex(b"abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
    assert_eq!(
        sha256::hex(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
    );
}

/// The tests' own source files.
fn test_files() -> Vec<(PathBuf, String)> {
    let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests");
    let mut found: Vec<_> = [dir.clone(), dir.join("support")]
        .iter()
        .flat_map(|d| std::fs::read_dir(d).unwrap())
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "rs"))
        .map(|p| {
            let text = std::fs::read_to_string(&p).unwrap();
            (p, text)
        })
        .collect();
    found.sort();
    assert!(found.len() > 1);
    found
}

fn no_test_names_a_target() {
    for (file, text) in test_files() {
        for target in Target::ALL {
            for name in [target.name().to_string(), format!("Target::{target:?}")] {
                assert!(
                    !text.contains(&name),
                    "{} names {name}; take targets from Target::ALL",
                    file.display()
                );
            }
        }
    }
}

/// The functions of a system library that are thin wrappers over the
/// kernel, as "Standing alone" in `knowledge/compiler.md` allows, by the
/// names a Mach-O file gives them.
const THIN_WRAPPERS: &[&str] = &["_write", "__exit", "___error", "_mmap", "_munmap"];

/// The same, by the names `KERNEL32.dll` gives them.
const KERNEL32_THIN_WRAPPERS: &[&str] = &[
    "GetStdHandle",
    "WriteFile",
    "ExitProcess",
    "VirtualAlloc",
    "VirtualFree",
    "GetCommandLineW",
];

fn u16_at(bytes: &[u8], at: usize) -> usize {
    u16::from_le_bytes(bytes[at..at + 2].try_into().unwrap()) as usize
}

fn u32_at(bytes: &[u8], at: usize) -> usize {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()) as usize
}

fn c_string(bytes: &[u8], at: usize) -> String {
    let end = bytes[at..].iter().position(|&b| b == 0).unwrap();
    String::from_utf8(bytes[at..at + end].to_vec()).unwrap()
}

/// The libraries a Mach-O executable loads, and the symbols it imports.
fn mach_o_imports(bytes: &[u8]) -> (Vec<String>, Vec<String>) {
    const LC_SYMTAB: usize = 0x2;
    const LC_LOAD_DYLIB: usize = 0xc;
    let mut libraries = Vec::new();
    let mut symbols = Vec::new();
    let mut at = 32;
    for _ in 0..u32_at(bytes, 16) {
        let (cmd, size) = (u32_at(bytes, at), u32_at(bytes, at + 4));
        if cmd == LC_LOAD_DYLIB {
            libraries.push(c_string(bytes, at + u32_at(bytes, at + 8)));
        } else if cmd == LC_SYMTAB {
            let (table, count) = (u32_at(bytes, at + 8), u32_at(bytes, at + 12));
            let strings = u32_at(bytes, at + 16);
            for i in 0..count {
                let entry = table + 16 * i;
                let kind = bytes[entry + 4];
                // External, and undefined: a symbol the loader finds.
                if kind & 0x01 != 0 && kind & 0x0e == 0 {
                    symbols.push(c_string(bytes, strings + u32_at(bytes, entry)));
                }
            }
        }
        at += size;
    }
    (libraries, symbols)
}

/// The libraries a PE executable loads, and the symbols it imports from
/// them.
fn pe_imports(bytes: &[u8]) -> (Vec<String>, Vec<String>) {
    let pe = u32_at(bytes, 0x3c);
    let sections = u16_at(bytes, pe + 6);
    let optional = pe + 24;
    let section_table = optional + u16_at(bytes, pe + 20);
    // Where an address in memory is in the file.
    let offset = |rva: usize| {
        (0..sections)
            .map(|i| section_table + 40 * i)
            .find_map(|h| {
                let (start, size) = (u32_at(bytes, h + 12), u32_at(bytes, h + 8));
                (start..start + size)
                    .contains(&rva)
                    .then(|| rva - start + u32_at(bytes, h + 20))
            })
            .expect("an address inside a section")
    };
    let mut libraries = Vec::new();
    let mut symbols = Vec::new();
    // The import directory, the second data directory of PE32+.
    let directory = u32_at(bytes, optional + 112 + 8);
    if directory == 0 {
        return (libraries, symbols);
    }
    let mut entry = offset(directory);
    while u32_at(bytes, entry + 12) != 0 {
        libraries.push(c_string(bytes, offset(u32_at(bytes, entry + 12))));
        let mut slot = offset(u32_at(bytes, entry));
        loop {
            let value = u64::from_le_bytes(bytes[slot..slot + 8].try_into().unwrap());
            if value == 0 {
                break;
            }
            assert!(value >> 63 == 0, "a symbol is imported by its name");
            symbols.push(c_string(bytes, offset(value as usize) + 2));
            slot += 8;
        }
        entry += 20;
    }
    (libraries, symbols)
}

/// The kinds of the segments an ELF executable's program headers list.
fn elf_segment_kinds(bytes: &[u8]) -> Vec<usize> {
    let headers = u32_at(bytes, 32);
    let size = u16_at(bytes, 54);
    (0..u16_at(bytes, 56))
        .map(|i| u32_at(bytes, headers + i * size))
        .collect()
}

/// A program that asks the system for memory.
const JOIN: &str = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  x = \"a\"\n  print(\"{x}{x}\")\n}\n";

/// A program that takes the words it was started with.
const WORDS: &str = "use std.console (console, print)\n\nfn main(args: List[str]) ! console, alloc {\n  print(\"{args.size}\")\n}\n";

fn every_executable_uses_only_thin_wrappers_over_the_kernel() {
    const PT_DYNAMIC: usize = 2;
    const PT_INTERP: usize = 3;
    let programs = [HELLO, JOIN, WORDS];
    for (target, source) in buildable()
        .into_iter()
        .filter(|t| t.is_machine_code())
        .flat_map(|t| programs.map(|p| (t, p)))
    {
        let bytes = compile(source.as_bytes(), target).unwrap();
        let name = target.name();
        if bytes.starts_with(b"\x7fELF") {
            let kinds = elf_segment_kinds(&bytes);
            assert!(
                !kinds.contains(&PT_INTERP) && !kinds.contains(&PT_DYNAMIC),
                "{name} links a library"
            );
        } else if bytes.starts_with(&0xfeed_facfu32.to_le_bytes()) {
            let (libraries, symbols) = mach_o_imports(&bytes);
            assert_eq!(
                libraries,
                ["/usr/lib/libSystem.B.dylib"],
                "{name} links only libSystem"
            );
            assert!(!symbols.is_empty(), "{name} imports what it calls");
            for symbol in symbols {
                assert!(
                    THIN_WRAPPERS.contains(&symbol.as_str()),
                    "{name} imports {symbol}, which is not a thin wrapper over the kernel"
                );
            }
        } else if bytes.starts_with(b"MZ") {
            let (libraries, symbols) = pe_imports(&bytes);
            assert_eq!(libraries, ["KERNEL32.dll"], "{name} links only KERNEL32");
            assert!(!symbols.is_empty(), "{name} imports what it calls");
            for symbol in symbols {
                assert!(
                    KERNEL32_THIN_WRAPPERS.contains(&symbol.as_str()),
                    "{name} imports {symbol}, which is not a thin wrapper over the kernel"
                );
            }
        } else {
            panic!("{name} writes an executable in a form this test does not read");
        }
    }
}
