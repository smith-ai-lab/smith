//! Every stage of a build does work in proportion to the program. The lexer
//! of `compiler/lexer.smith` is repeated under other function names to 10 and
//! to 100 times its size, as `measure/` builds it, and the stages run over each.
//! What is compared is what each stage allocates, which is the same on every
//! machine and every run, as a time is not, and which grows with the work of a
//! stage as a pass that copies what it has already built does.

use smith::scalar::{self, Program};
use smith::target::Target;

use crate::Test;
use crate::counting::{Allocated, allocated};
use crate::repeat;

pub const ONCE: &[Test] = &[(
    "every_stage_of_a_build_allocates_in_proportion_to_the_program_as_it_grows_a_hundred_times",
    every_stage_of_a_build_allocates_in_proportion_to_the_program_as_it_grows_a_hundred_times,
)];

/// How many copies of the lexer's functions each program is of.
const COPIES: [usize; 3] = [1, 10, 100];

/// The most that a stage may allocate for the largest program, as a multiple of
/// what it allocates for the smallest, which `knowledge/compiler.md` says under
/// Speed, beside the least the time of a copy may grow to.
const MOST: u64 = 150;

/// What each stage allocates over a program, by the name of the stage.
fn stages(source: &str) -> Vec<(&'static str, Allocated)> {
    let (tokens, lexing) = allocated(|| smith::count_tokens(source.as_bytes()));
    assert!(tokens.is_ok(), "the source lexes");
    let (program, front) = allocated(|| smith::lower(source.as_bytes()));
    let program: Program = program.expect("the source builds");
    let (program, inline) = allocated(|| scalar::inline(&program));
    let (program, trim) = allocated(|| scalar::trim(&program));
    let (program, fold) = allocated(|| scalar::fold(&program));
    let (program, common) = allocated(|| scalar::common(&program));
    let (program, remove) = allocated(|| scalar::remove(&program));
    let (_, emit) = allocated(|| Target::ALL[0].emit(&program));
    vec![
        ("lex", lexing),
        ("lex, parse, check, and lower", front),
        ("inline", inline),
        ("trim", trim),
        ("fold", fold),
        ("common", common),
        ("remove", remove),
        ("emit", emit),
    ]
}

/// The functions that `bump` reaches and the lexer's types, repeated: a
/// part of the lexer small enough that three sizes of it are built well within
/// the time of a test. `measure/` builds the whole of it.
fn program(copies: usize) -> String {
    let file = include_str!("../../compiler/lexer.smith");
    repeat::repeated(
        repeat::lexer(file),
        copies,
        "bump",
        "bump(Lexer(src: ['a', '\\n'], at: 0, pos: Pos(line: 1, col: 1), tokens: [], errors: [],\n               line_has_token: false, keywords: [], puncts: [])).at",
    )
}

/// At a hundred times the size, no stage allocates more than a hundred and
/// fifty times what it allocates at one, and each copy added between ten and a
/// hundred costs a stage no more than half as much again as each copy added
/// between one and ten, so that a cost that grows with the whole program shows
/// at once and not only past the limit.
fn every_stage_of_a_build_allocates_in_proportion_to_the_program_as_it_grows_a_hundred_times() {
    let sizes: Vec<Vec<(&str, Allocated)>> = COPIES.iter().map(|&n| stages(&program(n))).collect();
    for (i, (stage, _)) in sizes[0].iter().enumerate() {
        let [one, ten, hundred] = [0, 1, 2].map(|s| sizes[s][i].1);
        for (what, a, b, c) in [
            ("times", one.calls, ten.calls, hundred.calls),
            ("bytes", one.bytes, ten.bytes, hundred.bytes),
        ] {
            assert!(
                c <= MOST * a,
                "{stage} asks to allocate {what}: {a}, {b}, {c} for 1, 10, 100 copies"
            );
        }
        // The cost of a copy added, in thousandths so that it is a whole number.
        // Bytes are not compared so, as a list that doubles when it is full
        // allocates twice as much for a size just past a power of two.
        let early = 1000 * ten.calls.saturating_sub(one.calls) / 9;
        let late = 1000 * hundred.calls.saturating_sub(ten.calls) / 90;
        assert!(
            2 * late <= 3 * early,
            "{stage} asks to allocate {} times for 1, {} for 10, {} for 100 copies",
            one.calls,
            ten.calls,
            hundred.calls
        );
    }
}
