//! The bootstrap compiler for Smith. It is written in Rust with no
//! dependencies, and is retired once Smith compiles itself.

mod check;
pub mod diag;
mod lexer;
mod parser;
pub mod scalar;
pub mod target;
pub mod timings;

use target::Target;
use timings::Timings;

/// The program a source file describes, as the target's executable.
///
/// The errors are those of the earliest stage that finds any, in the order
/// of their places, each once.
pub fn compile(source: &[u8], target: Target) -> Result<Vec<u8>, Vec<diag::Error>> {
    compile_timed(source, target, &mut Timings::new())
}

/// The tokens of a source file in the plain form that the lexer written in
/// Smith, in `compiler/`, prints, or the errors of the lexer.
pub fn lex_plain(source: &[u8]) -> Result<String, Vec<diag::Error>> {
    lexer::lex(source).map(|tokens| lexer::plain(&tokens))
}

/// The program a source file describes, in the scalar form. Its errors are
/// those `compile` reports before a target is involved.
pub fn lower(source: &[u8]) -> Result<scalar::Program, Vec<diag::Error>> {
    lower_timed(source, &mut Timings::new())
}

/// How many functions and implementations of the prelude a source file reaches,
/// which are the ones the checker checks, or the errors `compile` reports
/// before a target is involved.
pub fn prelude_reached(source: &[u8]) -> Result<(usize, usize), Vec<diag::Error>> {
    checked(source, &mut Timings::new()).map(|checked| checked.prelude_reached)
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
    checked(source, timings).map(|checked| in_order(checked.warnings))
}

/// `compile`, recording how long each stage takes.
pub fn compile_timed(
    source: &[u8],
    target: Target,
    timings: &mut Timings,
) -> Result<Vec<u8>, Vec<diag::Error>> {
    let program = lower_timed(source, timings)?;
    let program = timings.stage("inline", || scalar::inline(&program));
    let program = timings.stage("fold", || scalar::fold(&program));
    let program = timings.stage("common", || scalar::common(&program));
    let program = timings.stage("remove", || scalar::remove(&program));
    Ok(timings.stage("emit", || target.emit(&program)))
}

fn lower_timed(source: &[u8], timings: &mut Timings) -> Result<scalar::Program, Vec<diag::Error>> {
    checked(source, timings).map(|checked| checked.program)
}

/// The source of a module of the standard library, for the errors that show a
/// line of it.
pub(crate) fn library_text(module: &str) -> &'static str {
    check::library_text(module)
}

/// The program a source file describes, in the scalar form, when the modules
/// that `use std.<name>` reads are those given, each by its name and its source,
/// in place of the standard library.
pub fn lower_in(
    source: &[u8],
    library: &[(&str, &str)],
) -> Result<scalar::Program, Vec<diag::Error>> {
    let tokens = lexer::lex(source).map_err(in_order)?;
    let file = parser::parse(&tokens).map_err(in_order)?;
    check::check(file, &check::Library::new(library))
        .map(|checked| checked.program)
        .map_err(in_order)
}

fn checked(source: &[u8], timings: &mut Timings) -> Result<check::Checked, Vec<diag::Error>> {
    let tokens = timings
        .stage("lex", || lexer::lex(source))
        .map_err(in_order)?;
    let file = timings
        .stage("parse", || parser::parse(&tokens))
        .map_err(in_order)?;
    timings
        .stage("check", || check::check(file, &check::Library::standard()))
        .map_err(in_order)
}

fn in_order(mut errors: Vec<diag::Error>) -> Vec<diag::Error> {
    errors.sort_by_key(|e| e.pos);
    errors.dedup();
    errors
}
