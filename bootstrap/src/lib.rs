//! The bootstrap compiler for Smith. It is written in Rust with no
//! dependencies, and is retired once Smith compiles itself.

mod check;
pub mod diag;
pub mod eval;
pub mod ir;
mod lexer;
mod parser;
pub mod scalar;
pub mod target;
pub mod timings;

use ir::Program;
use target::Target;
use timings::Timings;

/// The program a source file describes, as the target's executable.
///
/// The errors are those of the earliest stage that finds any, in the order
/// of their places, each once.
pub fn compile(source: &[u8], target: Target) -> Result<Vec<u8>, Vec<diag::Error>> {
    compile_timed(source, target, &mut Timings::new())
}

/// The program a source file describes, in the form every target receives.
/// Its errors are those `compile` reports before a target is involved.
pub fn lower(source: &[u8]) -> Result<Program, Vec<diag::Error>> {
    lower_timed(source, &mut Timings::new())
}

/// The program a source file describes, in the scalar form, or the feature
/// that stopped it. Its errors are those `compile` reports before a target
/// is involved.
pub fn lower_scalar(
    source: &[u8],
) -> Result<Result<scalar::Program, scalar::Stopped>, Vec<diag::Error>> {
    let checked = checked(source, &mut Timings::new(), true)?;
    Ok(checked.scalar.expect("asked for"))
}

/// The warnings `smith check` reports about a source file, in the order of
/// their places, or the errors `compile` reports before a target is
/// involved. No code is generated for a target.
pub fn check(source: &[u8]) -> Result<Vec<diag::Error>, Vec<diag::Error>> {
    check_timed(source, &mut Timings::new())
}

/// `check`, recording how long each stage takes.
pub fn check_timed(
    source: &[u8],
    timings: &mut Timings,
) -> Result<Vec<diag::Error>, Vec<diag::Error>> {
    checked(source, timings, false).map(|checked| in_order(checked.warnings))
}

/// `compile`, recording how long each stage takes.
pub fn compile_timed(
    source: &[u8],
    target: Target,
    timings: &mut Timings,
) -> Result<Vec<u8>, Vec<diag::Error>> {
    let program = lower_timed(source, timings)?;
    timings
        .stage("emit", || target.emit(&program))
        .ok_or_else(|| {
            vec![diag::Error::unsupported(
                diag::Pos::START,
                &format!("building for {}", target.name()),
            )]
        })
}

fn lower_timed(source: &[u8], timings: &mut Timings) -> Result<Program, Vec<diag::Error>> {
    checked(source, timings, false).map(|checked| checked.program)
}

fn checked(
    source: &[u8],
    timings: &mut Timings,
    scalar: bool,
) -> Result<check::Checked, Vec<diag::Error>> {
    let tokens = timings
        .stage("lex", || lexer::lex(source))
        .map_err(in_order)?;
    let file = timings
        .stage("parse", || parser::parse(&tokens))
        .map_err(in_order)?;
    timings
        .stage("check", || check::check(file, scalar))
        .map_err(in_order)
}

fn in_order(mut errors: Vec<diag::Error>) -> Vec<diag::Error> {
    errors.sort_by_key(|e| e.pos);
    errors.dedup();
    errors
}
