//! The targets that write a module for a host, and not an executable for a
//! chip. A module is run by `node`, with the file of JavaScript the compiler
//! writes beside it, and a test is skipped, saying so, where `node` is not
//! installed. `ci` has it installed.
//!
//! The targets are the cases of `Target::ALL` that say what is written beside
//! their executable, so no test names one.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use smith::lower;
use smith::scalar::{
    self, Binary, Bits, Builder, Compare, Op, Outcome, Program, Stream, System, Type, Value,
};
use smith::target::{Target, is_structured};

use crate::Test;
use crate::cli::{folder, smith, stderr};
use crate::generate::{self, Gen};
use crate::targets::{case_files, updating};
use crate::{choices, enums, lambdas, numbers, programs, repeat, text, tuples};

pub const ONCE: &[Test] = &[
    (
        "every_case_prints_under_node_what_its_out_file_says",
        every_case_prints_under_node_what_its_out_file_says,
    ),
    (
        "a_program_that_prints_asks_its_host_for_the_console_and_no_other_does",
        a_program_that_prints_asks_its_host_for_the_console_and_no_other_does,
    ),
    (
        "a_program_with_an_effect_that_is_not_built_is_one_error_that_names_it",
        a_program_with_an_effect_that_is_not_built_is_one_error_that_names_it,
    ),
    (
        "a_module_of_generated_programs_prints_what_the_evaluator_prints",
        a_module_of_generated_programs_prints_what_the_evaluator_prints,
    ),
    (
        "a_module_of_generated_programs_with_text_prints_what_the_evaluator_prints",
        a_module_of_generated_programs_with_text_prints_what_the_evaluator_prints,
    ),
    (
        "a_module_of_generated_programs_with_numbers_prints_what_the_evaluator_prints",
        a_module_of_generated_programs_with_numbers_prints_what_the_evaluator_prints,
    ),
    (
        "a_module_of_generated_programs_with_choices_prints_what_the_evaluator_prints",
        a_module_of_generated_programs_with_choices_prints_what_the_evaluator_prints,
    ),
    (
        "a_module_of_generated_programs_with_lambdas_prints_what_the_evaluator_prints",
        a_module_of_generated_programs_with_lambdas_prints_what_the_evaluator_prints,
    ),
    (
        "a_module_of_generated_programs_with_enums_and_tuples_prints_what_the_evaluator_prints",
        a_module_of_generated_programs_with_enums_and_tuples_prints_what_the_evaluator_prints,
    ),
    (
        "every_integer_operation_gives_what_the_evaluator_gives",
        every_integer_operation_gives_what_the_evaluator_gives,
    ),
    (
        "every_function_of_a_generated_program_is_written_as_loops_and_branches",
        every_function_of_a_generated_program_is_written_as_loops_and_branches,
    ),
    (
        "loops_and_choices_of_many_jumps_that_join_run_as_the_evaluator_says",
        loops_and_choices_of_many_jumps_that_join_run_as_the_evaluator_says,
    ),
    (
        "a_function_with_a_loop_that_has_two_ways_in_runs_as_the_evaluator_says",
        a_function_with_a_loop_that_has_two_ways_in_runs_as_the_evaluator_says,
    ),
    (
        "a_program_that_ends_with_a_status_ends_with_it",
        a_program_that_ends_with_a_status_ends_with_it,
    ),
    (
        "memory_is_grown_and_a_request_that_cannot_be_met_ends_the_program",
        memory_is_grown_and_a_request_that_cannot_be_met_ends_the_program,
    ),
];

/// The targets that write a module for a host.
fn modules() -> Vec<Target> {
    Target::ALL
        .iter()
        .copied()
        .filter(|t| !t.is_machine_code())
        .collect()
}

/// Whether `node` is installed. A test that needs it says so and is skipped
/// where it is not.
fn node() -> bool {
    static FOUND: OnceLock<bool> = OnceLock::new();
    let found = *FOUND.get_or_init(|| {
        crate::clock::output_within(Command::new("node").arg("--version"), false)
            .is_ok_and(|o| o.status.success())
    });
    if !found {
        eprintln!("skipped: node is not installed");
    }
    found
}

/// What the file of JavaScript beside a module does when `node` runs it.
fn run_host(dir: &Path, stem: &str) -> Outcome {
    let output = crate::clock::output_within(
        Command::new("node")
            .arg(dir.join(format!("{stem}.js")))
            .current_dir(dir),
        true,
    )
    .unwrap();
    assert!(
        output.status.code().is_some(),
        "{stem} ended by a signal: {}",
        stderr(&output)
    );
    Outcome {
        stdout: output.stdout,
        status: output.status.code().unwrap(),
    }
}

/// Writes the module of a program in the scalar form, and the file beside it,
/// in a folder of their own, and runs it under `node`.
fn run_module(target: Target, program: &Program) -> Outcome {
    let dir = folder("module");
    std::fs::write(
        dir.join(target.executable_name("program")),
        target.emit(program),
    )
    .unwrap();
    for (extension, bytes) in target.beside(program) {
        std::fs::write(dir.join(format!("program.{extension}")), bytes).unwrap();
    }
    run_host(&dir, "program")
}

/// The program a source file lowers to, after the passes, which is what each
/// target is given.
fn after_the_pass(source: &[u8]) -> Program {
    scalar::passes(&lower(source).unwrap_or_else(|e| panic!("{e:?}")))
}

/// What `node` and the evaluator each say of the program in the scalar form.
fn agree(program: &Program) {
    let said = scalar::run(program);
    for target in modules() {
        assert_eq!(run_module(target, program), said, "{}", target.name());
    }
}

/// What the modules in `files` import, one line each, as `file imports`, and
/// whether `WebAssembly` takes them to be valid, from one run of `node`.
fn inspect(files: &[PathBuf]) -> Vec<String> {
    let script = r#"
const fs = require("fs");
for (const file of process.argv.slice(1)) {
  const bytes = fs.readFileSync(file);
  if (!WebAssembly.validate(bytes)) {
    console.log(file + " is not valid");
    continue;
  }
  const imports = WebAssembly.Module.imports(new WebAssembly.Module(bytes));
  console.log(file + " " + imports.map((i) => `${i.module}.${i.name}:${i.kind}`).join(","));
}
"#;
    let output = crate::clock::output_within(
        Command::new("node").arg("-e").arg(script).args(files),
        true,
    )
    .unwrap();
    assert!(output.status.success(), "{}", stderr(&output));
    String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect()
}

/// Builds a file in a folder of its own, for the target, with `smith build`.
fn built(target: Target, name: &str, source: &[u8]) -> (PathBuf, std::process::Output) {
    let dir = folder(name);
    let file = format!("{name}.smith");
    std::fs::write(dir.join(&file), source).unwrap();
    let output = smith(&dir, &["build", "--target", target.name(), &file]);
    (dir, output)
}

fn every_case_prints_under_node_what_its_out_file_says() {
    if updating() || !node() {
        return;
    }
    let mut written = Vec::new();
    for target in modules() {
        for source in case_files() {
            let out = source.with_extension("out");
            if !out.exists() {
                continue;
            }
            let name = source.file_stem().unwrap().to_str().unwrap();
            let (dir, built) = built(target, name, &std::fs::read(&source).unwrap());
            assert!(built.status.success(), "{name}: {}", stderr(&built));
            let ran = run_host(&dir, name);
            let expected = std::fs::read(&out).unwrap();
            assert_eq!(
                String::from_utf8_lossy(&ran.stdout),
                String::from_utf8_lossy(&expected),
                "{name} for {}",
                target.name()
            );
            assert_eq!(ran.status, 0, "{name} for {}", target.name());
            written.push(dir.join(target.executable_name(name)));
        }
    }
    // The module is valid, and asks of its host only the console.
    assert!(!written.is_empty());
    for line in inspect(&written) {
        let (file, imports) = line.split_once(' ').unwrap();
        assert!(
            imports.is_empty() || imports == "console.print:function",
            "{file}: {imports}"
        );
    }
}

fn a_program_that_prints_asks_its_host_for_the_console_and_no_other_does() {
    if !node() {
        return;
    }
    let sources = [
        ("prints", "use std.console (console, print)\n\nfn main() ! console { print(\"hi\") }\n", true),
        ("is_empty", "fn main() {\n}\n", false),
        (
            "builds_text",
            "use std.bits (u64)\nuse std.list (push)\n\nfn main() ! alloc {\n  xs: List[u64] = [1, 2, 3]\n  ys = push(xs, 4)\n}\n",
            false,
        ),
    ];
    let mut files = Vec::new();
    for target in modules() {
        for (name, source, prints) in sources {
            let (dir, output) = built(target, name, source.as_bytes());
            assert!(output.status.success(), "{name}: {}", stderr(&output));
            let module = dir.join(target.executable_name(name));
            // The file of JavaScript is written beside it, and runs.
            assert!(dir.join(format!("{name}.js")).is_file(), "{name}");
            assert_eq!(run_host(&dir, name).status, 0, "{name}");
            files.push((module, prints));
        }
    }
    let paths: Vec<PathBuf> = files.iter().map(|(p, _)| p.clone()).collect();
    for (line, (_, prints)) in inspect(&paths).iter().zip(&files) {
        let (_, imports) = line.split_once(' ').unwrap();
        let wanted = if *prints { "console.print:function" } else { "" };
        assert_eq!(imports, wanted, "{line}");
    }
}

fn a_program_with_an_effect_that_is_not_built_is_one_error_that_names_it() {
    for target in modules() {
        let (dir, output) = built(target, "waits", b"fn main() ! net {\n}\n");
        assert!(!output.status.success());
        let errors = stderr(&output);
        let places = errors.lines().filter(|l| l.contains(".smith:")).count();
        assert_eq!(places, 1, "{errors}");
        assert!(errors.contains("`net`"), "{errors}");
        assert!(!dir.join(target.executable_name("waits")).exists());
        assert!(!dir.join("waits.js").exists());
    }
}

/// Checks, for `cases` generated programs, that each module prints under `node`
/// what the evaluator says its program prints, and ends as it does.
fn every_module_agrees<T: std::fmt::Debug>(
    seed: u64,
    cases: u64,
    generate: impl Fn(&mut Gen) -> T,
    source: impl Fn(&T) -> &[u8],
) {
    if !node() {
        return;
    }
    generate::check(seed, cases, generate, |case| {
        agree(&after_the_pass(source(case)));
    });
}

fn a_module_of_generated_programs_prints_what_the_evaluator_prints() {
    every_module_agrees(101, 12, programs::program, |case| &case.source);
}

fn a_module_of_generated_programs_with_text_prints_what_the_evaluator_prints() {
    every_module_agrees(102, 12, text::program, |case| case.source.as_bytes());
}

fn a_module_of_generated_programs_with_numbers_prints_what_the_evaluator_prints() {
    every_module_agrees(103, 12, numbers::program, |case| case.source.as_bytes());
}

fn a_module_of_generated_programs_with_choices_prints_what_the_evaluator_prints() {
    every_module_agrees(104, 12, choices::program, |case| case.source.as_bytes());
}

fn a_module_of_generated_programs_with_lambdas_prints_what_the_evaluator_prints() {
    every_module_agrees(105, 12, lambdas::program, |case| case.source.as_bytes());
}

fn a_module_of_generated_programs_with_enums_and_tuples_prints_what_the_evaluator_prints() {
    every_module_agrees(106, 8, enums::program, |case| case.source.as_bytes());
    every_module_agrees(107, 8, tuples::program, |case| case.source.as_bytes());
}

/// The integers every size has that an operation is most likely to get wrong:
/// next to zero, to the greatest signed number, and to the greatest unsigned
/// one, and with every other bit set.
fn edges(bits: Bits) -> Vec<u64> {
    let n = bits.count();
    let all = if n == 64 { u64::MAX } else { (1u64 << n) - 1 };
    let mut found = vec![
        0,
        1,
        2,
        3,
        all >> 1,
        (all >> 1) + 1,
        all - 1,
        all,
        0x5555_5555_5555_5555 & all,
    ];
    if n == 64 {
        found.push(0xFFFF_FFFF);
        found.push(0x1_0000_0000);
    }
    found.sort();
    found.dedup();
    found
}

/// A program that writes what each operation gives for the integers at the
/// edges of each size: arithmetic, the high half of a product, bits, shifts by
/// counts at and beyond the size, comparison, whether an answer fitted, and
/// widening and narrowing.
fn every_integer_operation_gives_what_the_evaluator_gives() {
    if !node() {
        return;
    }
    let mut b = Builder::new();
    let mut results: Vec<(scalar::Value, Bits)> = Vec::new();
    for bits in Bits::ALL {
        let values: Vec<_> = edges(bits).into_iter().map(|n| b.int(bits, n)).collect();
        for &x in &values {
            for &y in &values {
                for op in [
                    Binary::Add,
                    Binary::Sub,
                    Binary::Mul,
                    Binary::MulHighU,
                    Binary::MulHighS,
                    Binary::And,
                    Binary::Xor,
                    Binary::Or,
                ] {
                    results.push((b.binary(op, x, y), bits));
                }
                for op in [Compare::Equal, Compare::LessU, Compare::LessS] {
                    results.push((b.compare(op, x, y), Bits::B8));
                }
                for op in [Binary::Add, Binary::Sub, Binary::Mul] {
                    for signed in [false, true] {
                        let (number, fitted) = b.overflows(op, signed, x, y);
                        results.extend([(number, bits), (fitted, Bits::B8)]);
                    }
                }
            }
            for count in [0, 1, 7, 8, 15, 16, 31, 32, 33, 63, 64, 65, 255] {
                let count = b.int(Bits::B8, count);
                for op in [
                    Binary::ShiftLeft,
                    Binary::ShiftRightU,
                    Binary::ShiftRightS,
                ] {
                    results.push((b.binary(op, x, count), bits));
                }
            }
            for to in Bits::ALL {
                if to.count() > bits.count() {
                    for signed in [false, true] {
                        results.push((b.widen(signed, x, to), to));
                    }
                } else if to.count() < bits.count() {
                    results.push((b.narrow(x, to), to));
                }
            }
            let (first, second) = (values[0], values[values.len() - 1]);
            let on = b.compare(Compare::LessU, x, second);
            results.push((b.choose(on, first, x), bits));
        }
    }
    let wide: Vec<_> = results
        .iter()
        .map(|(v, bits)| match bits {
            Bits::B64 => *v,
            _ => b.widen(false, *v, Bits::B64),
        })
        .collect();
    let size = b.int(Bits::B64, 8 * wide.len() as u64);
    let buffer = b.allocate(size);
    for (place, v) in wide.iter().enumerate() {
        let offset = b.int(Bits::B64, 8 * place as u64);
        let at = b.binary(Binary::Add, buffer, offset);
        b.store(at, *v);
    }
    let call = System::Write(Stream::Stdout);
    b.emit(
        Op::System {
            call,
            args: vec![buffer, size],
        },
        &[],
    );
    b.ret(vec![]);
    let program = Program {
        data: Vec::new(),
        functions: vec![b.finish(&[])],
        main: 0,
    };
    agree(&program);
}

/// A program that writes the byte `a` and then does `then` with the builder.
fn after_writing(then: impl FnOnce(&mut Builder)) -> Program {
    let mut b = Builder::new();
    let one = b.int(Bits::B64, 1);
    let buffer = b.allocate(one);
    let letter = b.int(Bits::B8, u64::from(b'a'));
    b.store(buffer, letter);
    let call = System::Write(Stream::Stdout);
    b.emit(
        Op::System {
            call,
            args: vec![buffer, one],
        },
        &[],
    );
    then(&mut b);
    b.ret(vec![]);
    Program {
        data: Vec::new(),
        functions: vec![b.finish(&[])],
        main: 0,
    }
}

fn a_program_that_ends_with_a_status_ends_with_it() {
    if !node() {
        return;
    }
    for status in [0, 1, 7, 255] {
        let program = after_writing(|b| {
            let status = b.int(Bits::B8, status);
            b.exit(status);
        });
        let said = scalar::run(&program);
        assert_eq!(said.status, status as i32);
        agree(&program);
    }
}

/// A request that is more than the memory has grows it, and the bytes at the
/// end of it are those that were written. A request that no memory can meet
/// ends the program with a failing status, having written what came before.
fn memory_is_grown_and_a_request_that_cannot_be_met_ends_the_program() {
    if !node() {
        return;
    }
    let program = after_writing(|b| {
        let size = b.int(Bits::B64, 3_000_000);
        let large = b.allocate(size);
        let last = b.int(Bits::B64, 2_999_999);
        let at = b.binary(Binary::Add, large, last);
        let letter = b.int(Bits::B8, u64::from(b'z'));
        b.store(at, letter);
        let one = b.int(Bits::B64, 1);
        let call = System::Write(Stream::Stdout);
        b.emit(
            Op::System {
                call,
                args: vec![at, one],
            },
            &[],
        );
        let too_much = b.int(Bits::B64, 1 << 40);
        b.allocate(too_much);
        let letter = b.int(Bits::B8, u64::from(b'q'));
        b.store(at, letter);
        b.emit(
            Op::System {
                call,
                args: vec![at, one],
            },
            &[],
        );
    });
    let said = scalar::run_with_limit(&program, 1 << 30);
    assert_eq!(said.stdout, b"az");
    assert_eq!(said.status, 1);
    for target in modules() {
        assert_eq!(run_module(target, &program), said, "{}", target.name());
    }
}

/// Whether every function of the program after the passes has only jumps back
/// to blocks that dominate them, which is when its blocks are written as
/// `loop`, `block`, `if`, and `br`, with no number of the block to run.
fn written_as_loops(program: &Program) -> bool {
    program.functions.iter().all(is_structured)
}

/// The passes never leave a function whose blocks cannot be written as
/// WebAssembly's own loops and branches, for any program the generators build.
fn every_function_of_a_generated_program_is_written_as_loops_and_branches() {
    fn all<T: std::fmt::Debug>(
        seed: u64,
        generate: impl Fn(&mut Gen) -> T,
        source: impl Fn(&T) -> &[u8],
    ) {
        generate::check(seed, 12, generate, |case| {
            assert!(written_as_loops(&after_the_pass(source(case))));
        });
    }
    all(101, programs::program, |case| &case.source);
    all(102, text::program, |case| case.source.as_bytes());
    all(103, numbers::program, |case| case.source.as_bytes());
    all(104, choices::program, |case| case.source.as_bytes());
    all(105, lambdas::program, |case| case.source.as_bytes());
    all(106, enums::program, |case| case.source.as_bytes());
    all(107, tuples::program, |case| case.source.as_bytes());
    let file = include_str!("../../compiler/lexer.smith");
    let lexer = repeat::repeated(
        repeat::lexer(file),
        1,
        "lex",
        "match lex(['f', 'n', ' ', 'm', '\\n']) {\n    .ok(tokens) { tokens.size }\n    .err(errors) { 0 }\n  }",
    );
    assert!(written_as_loops(&after_the_pass(lexer.as_bytes())));
}

/// Writes the byte `letter` to standard output from the buffer of one byte.
fn write_letter(b: &mut Builder, buffer: Value, one: Value, letter: u8) {
    let letter = b.int(Bits::B8, u64::from(letter));
    b.store(buffer, letter);
    let call = System::Write(Stream::Stdout);
    b.emit(
        Op::System {
            call,
            args: vec![buffer, one],
        },
        &[],
    );
}

fn program_of(b: Builder) -> Program {
    Program {
        data: Vec::new(),
        functions: vec![b.finish(&[])],
        main: 0,
    }
}

/// A loop whose round chooses among four blocks that each give a number to the
/// one that joins them, which adds it and goes round again, and a block that
/// writes the sum when the loop ends.
fn loops_and_choices_of_many_jumps_that_join_run_as_the_evaluator_says() {
    if !node() {
        return;
    }
    let big = Type::Int(Bits::B64);
    let mut b = Builder::new();
    let one = b.int(Bits::B64, 1);
    let buffer = b.allocate(one);
    let zero = b.int(Bits::B64, 0);
    let rounds = b.int(Bits::B64, 8);
    let three = b.int(Bits::B64, 3);
    let (head, h) = b.block(&[big, big]);
    let (body, _) = b.block(&[]);
    let (join, j) = b.block(&[big]);
    let (finish, f) = b.block(&[big]);
    let ways: Vec<usize> = (0..4).map(|_| b.block(&[]).0).collect();
    b.jump(head, vec![zero, zero]);
    b.at(head);
    let more = b.compare(Compare::LessU, h[0], rounds);
    b.switch(more, vec![(finish, vec![h[1]]), (body, vec![])]);
    b.at(body);
    let way = b.binary(Binary::And, h[0], three);
    let way = b.narrow(way, Bits::B8);
    b.switch(way, ways.iter().map(|&w| (w, vec![])).collect());
    for (place, &way) in ways.iter().enumerate() {
        b.at(way);
        write_letter(&mut b, buffer, one, b'p' + place as u8);
        let gives = b.int(Bits::B64, 1 << place);
        b.jump(join, vec![gives]);
    }
    b.at(join);
    let sum = b.binary(Binary::Add, h[1], j[0]);
    let next = b.binary(Binary::Add, h[0], one);
    b.jump(head, vec![next, sum]);
    b.at(finish);
    let last = b.narrow(f[0], Bits::B8);
    b.store(buffer, last);
    let call = System::Write(Stream::Stdout);
    b.emit(
        Op::System {
            call,
            args: vec![buffer, one],
        },
        &[],
    );
    b.ret(vec![]);
    let program = program_of(b);
    assert!(written_as_loops(&program));
    assert!(!scalar::run(&program).stdout.is_empty());
    agree(&program);
}

/// Two blocks that each jump to the other and that the first block of the
/// function reaches both of, so that neither dominates the other. Its blocks
/// cannot be written as loops, and the module is still right.
fn a_function_with_a_loop_that_has_two_ways_in_runs_as_the_evaluator_says() {
    if !node() {
        return;
    }
    let big = Type::Int(Bits::B64);
    let mut b = Builder::new();
    let one = b.int(Bits::B64, 1);
    let buffer = b.allocate(one);
    let zero = b.int(Bits::B64, 0);
    let (first, f) = b.block(&[big]);
    let (second, s) = b.block(&[big]);
    let (exit, _) = b.block(&[]);
    let (limit_first, limit_second) = (b.int(Bits::B64, 5), b.int(Bits::B64, 7));
    let way = b.int(Bits::B8, 1);
    b.switch(way, vec![(first, vec![zero]), (second, vec![zero])]);
    for (block, p, letter, limit, other) in [
        (first, &f, b'a', limit_first, second),
        (second, &s, b'b', limit_second, first),
    ] {
        b.at(block);
        write_letter(&mut b, buffer, one, letter);
        let next = b.binary(Binary::Add, p[0], one);
        let more = b.compare(Compare::LessU, next, limit);
        b.switch(more, vec![(exit, vec![]), (other, vec![next])]);
    }
    b.at(exit);
    b.ret(vec![]);
    let program = program_of(b);
    assert!(!written_as_loops(&program));
    assert!(!scalar::run(&program).stdout.is_empty());
    agree(&program);
}
