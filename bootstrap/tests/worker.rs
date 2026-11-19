//! A module `smith build --target wasm` writes, run as a Cloudflare Worker in
//! `workerd`, which `miniflare` from npm starts without a network: a request
//! comes in, the module's `main` runs, and what it printed is the body of the
//! response. The Worker is `support/worker.mjs`, and `support/worker_driver.mjs`
//! starts it. `workerd` starts once for every test of this file, and the first
//! test that asks pays for it, which is about a second of the wall and none of
//! the processor of the test, as the program it starts is another's.
//!
//! A test is skipped, saying so, where `node` or the package is not installed.
//! `ci` installs the package, pinned by `support/package-lock.json`.

use std::cell::RefCell;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use smith::lower;
use smith::scalar::{self, Bits, Builder, Op, Outcome, Program, Stream, System};
use smith::target::Target;

use crate::cli::{folder, smith, stderr};
use crate::generate::{self, Gen};
use crate::{Test, choices, enums, lambdas, numbers, programs, text, tuples};

pub const ONCE: &[Test] = &[
    (
        "a_worker_answers_with_what_the_program_prints",
        a_worker_answers_with_what_the_program_prints,
    ),
    (
        "a_worker_whose_program_ends_with_a_status_answers_with_a_failure",
        a_worker_whose_program_ends_with_a_status_answers_with_a_failure,
    ),
    (
        "a_worker_of_generated_programs_answers_with_what_the_evaluator_prints",
        a_worker_of_generated_programs_answers_with_what_the_evaluator_prints,
    ),
];

/// The status of the response to a program that ends with a status other than
/// 0: a Worker that fails answers 500, and the header `x-smith-status` is the
/// status the program ended with.
const FAILED: u16 = 500;

/// What a Worker answered for a program.
#[derive(Debug, PartialEq)]
struct Answer {
    status: u16,
    ended: i32,
    body: Vec<u8>,
}

fn wasm() -> Target {
    Target::ALL
        .iter()
        .copied()
        .find(|t| t.name() == "wasm")
        .expect("the target `wasm` exists")
}

/// What the answer to a program that ends as the outcome says is.
fn answer_to(outcome: &Outcome) -> Answer {
    Answer {
        status: if outcome.status == 0 { 200 } else { FAILED },
        ended: outcome.status,
        body: outcome.stdout.clone(),
    }
}

fn support() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/support")
}

/// Whether `node` and `miniflare` are installed. A test that needs them says
/// so and is skipped where they are not.
fn installed() -> bool {
    static FOUND: OnceLock<bool> = OnceLock::new();
    let found = *FOUND.get_or_init(|| {
        let node = crate::clock::output_within(Command::new("node").arg("--version"), false)
            .is_ok_and(|o| o.status.success());
        node && support().join("node_modules/miniflare").is_dir()
    });
    if !found {
        eprintln!("skipped: node or the package miniflare is not installed (npm ci in tests/support)");
    }
    found
}

/// Starts the Worker with every module in the folder, which are the files
/// `name.wasm`, and returns what it answered to a request for each.
fn ask(dir: &Path) -> BTreeMap<String, Answer> {
    let output = crate::clock::output_within(
        Command::new("node")
            .arg(support().join("worker_driver.mjs"))
            .arg(dir)
            .current_dir(dir),
        true,
    )
    .unwrap();
    assert!(output.status.success(), "{}", stderr(&output));
    String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(|line| {
            let [name, status, ended, body] = line.split('\t').collect::<Vec<_>>()[..] else {
                panic!("a line of the driver: {line}");
            };
            let answer = Answer {
                status: status.parse().unwrap(),
                ended: ended.parse().unwrap(),
                body: decode(body),
            };
            (name.to_string(), answer)
        })
        .collect()
}

fn decode(base64: &str) -> Vec<u8> {
    let value = |c: u8| match c {
        b'A'..=b'Z' => c - b'A',
        b'a'..=b'z' => c - b'a' + 26,
        b'0'..=b'9' => c - b'0' + 52,
        b'+' => 62,
        _ => 63,
    };
    let mut out = Vec::new();
    let (mut bits, mut have) = (0u32, 0);
    for c in base64.bytes().take_while(|&c| c != b'=') {
        bits = bits << 6 | u32::from(value(c));
        have += 6;
        if have >= 8 {
            have -= 8;
            out.push((bits >> have) as u8);
            bits &= (1 << have) - 1;
        }
    }
    out
}

/// A program in the scalar form that writes `a`, and then does what `then`
/// says.
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

fn after_the_pass(source: &[u8]) -> Program {
    scalar::passes(&lower(source).unwrap_or_else(|e| panic!("{e:?}")))
}

const HELLO: &str = "hello";
const FAILS: &str = "fails";

/// The modules of generated programs, as the tests that run under `node` make
/// them, and what the evaluator says each prints. There are fewer cases, as each
/// is a request.
fn generated() -> Vec<(String, Vec<u8>, Outcome)> {
    let found = RefCell::new(Vec::new());
    fn collect<T: std::fmt::Debug>(
        found: &RefCell<Vec<(String, Vec<u8>, Outcome)>>,
        seed: u64,
        cases: u64,
        generate: impl Fn(&mut Gen) -> T,
        source: impl Fn(&T) -> &[u8],
    ) {
        generate::check(seed, cases, generate, |case| {
            let program = after_the_pass(source(case));
            let mut found = found.borrow_mut();
            let name = format!("generated_{}", found.len());
            found.push((name, wasm().emit(&program), scalar::run(&program)));
        });
    }
    collect(&found, 101, 6, programs::program, |c| &c.source);
    collect(&found, 102, 6, text::program, |c| c.source.as_bytes());
    collect(&found, 103, 6, numbers::program, |c| c.source.as_bytes());
    collect(&found, 104, 6, choices::program, |c| c.source.as_bytes());
    collect(&found, 105, 6, lambdas::program, |c| c.source.as_bytes());
    collect(&found, 106, 4, enums::program, |c| c.source.as_bytes());
    collect(&found, 107, 4, tuples::program, |c| c.source.as_bytes());
    found.into_inner()
}

/// The answers of one start of `workerd` to `hello.smith` built by `smith
/// build --target wasm`, to a program that ends with a status, and to the
/// generated programs, with what the evaluator says for the last two.
fn run() -> &'static (BTreeMap<String, Answer>, BTreeMap<String, Outcome>) {
    static RUN: OnceLock<(BTreeMap<String, Answer>, BTreeMap<String, Outcome>)> = OnceLock::new();
    RUN.get_or_init(|| {
        let dir = folder("worker");
        let source = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/cases/hello.smith");
        std::fs::copy(&source, dir.join("hello.smith")).unwrap();
        let built = smith(&dir, &["build", "--target", "wasm", "hello.smith"]);
        assert!(built.status.success(), "{}", stderr(&built));
        assert!(dir.join("hello.wasm").is_file());

        let fails = after_writing(|b| {
            let status = b.int(Bits::B8, 7);
            b.exit(status);
        });
        std::fs::write(dir.join(format!("{FAILS}.wasm")), wasm().emit(&fails)).unwrap();
        let mut wanted = BTreeMap::new();
        wanted.insert(FAILS.to_string(), scalar::run(&fails));
        for (name, module, outcome) in generated() {
            std::fs::write(dir.join(format!("{name}.wasm")), module).unwrap();
            wanted.insert(name, outcome);
        }
        (ask(&dir), wanted)
    })
}

fn a_worker_answers_with_what_the_program_prints() {
    if !installed() {
        return;
    }
    let hello = std::fs::read(Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/cases/hello.out"))
        .unwrap();
    assert_eq!(hello, b"hello world\n");
    let answer = &run().0[HELLO];
    assert_eq!(answer.status, 200);
    assert_eq!(answer.body, hello);
}

fn a_worker_whose_program_ends_with_a_status_answers_with_a_failure() {
    if !installed() {
        return;
    }
    let (answers, wanted) = run();
    let outcome = &wanted[FAILS];
    assert_eq!(outcome.status, 7);
    let answer = &answers[FAILS];
    assert_eq!(*answer, answer_to(outcome));
    assert_eq!(answer.status, FAILED);
}

fn a_worker_of_generated_programs_answers_with_what_the_evaluator_prints() {
    if !installed() {
        return;
    }
    let (answers, wanted) = run();
    let mut checked = 0;
    for (name, outcome) in wanted.iter().filter(|(n, _)| n.starts_with("generated_")) {
        assert_eq!(answers[name], answer_to(outcome), "{name}");
        checked += 1;
    }
    assert!(checked > 0);
}
