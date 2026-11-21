//! The bootstrap compiler for Smith. It is written in Rust with no
//! dependencies, and is retired once Smith compiles itself.

mod check;
mod comments;
pub mod core;
pub mod diag;
mod divide;
mod function_args;
mod hash;
mod item_notes;
pub mod kernel;
mod lexer;
mod parser;
pub mod scalar;
pub mod target;
pub mod timings;
pub mod work;

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

/// The comment on each item of a source file that has one, as the item named
/// by where it is: a function `f`, its parameter `f.a`, a struct `S`, its field
/// `S.a`, an enum `E`, its case `E.a`, in the order they are written. It is
/// what `knowledge/language/functions.md` says a comment belongs to, found by
/// the line the item starts on.
pub fn item_comments(source: &[u8]) -> Result<Vec<(String, String)>, Vec<diag::Error>> {
    let tokens = lexer::lex(source).map_err(in_order)?;
    let file = parser::parse(&tokens).map_err(in_order)?;
    let text = std::str::from_utf8(source).unwrap_or_default();
    let mut found: Vec<(usize, String, String)> = Vec::new();
    // A part of an item, which has a comment of its own only where it starts
    // its line.
    let mut add = |name: String, pos: diag::Pos, part: bool| {
        if part && !comments::starts_line(text, pos.line, pos.col) {
            return;
        }
        let comment = comments::above(text, pos.line);
        if !comment.is_empty() {
            found.push((pos.line, name, comment));
        }
    };
    for f in file.functions.iter() {
        add(f.name.clone(), f.pos, false);
        for p in &f.params {
            add(format!("{}.{}", f.name, p.name), p.pos, true);
        }
    }
    for s in &file.structs {
        add(s.name.clone(), s.pos, false);
        for f in &s.fields {
            add(format!("{}.{}", s.name, f.name), f.pos, true);
        }
    }
    for e in &file.enums {
        add(e.name.clone(), e.pos, false);
        for c in &e.cases {
            add(format!("{}.{}", e.name, c.name), c.pos, true);
        }
    }
    found.sort_by_key(|(line, ..)| *line);
    Ok(found
        .into_iter()
        .map(|(_, name, comment)| (name, comment))
        .collect())
}

/// How many tokens a source file has, or the errors of the lexer. It is what
/// `measure/` times, so it does nothing else with the tokens.
pub fn count_tokens(source: &[u8]) -> Result<usize, Vec<diag::Error>> {
    lexer::lex(source).map(|tokens| tokens.len())
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

/// How many `impl`s of the prelude are read for a source file: the ones its
/// items reach, and none that nothing reaches. The text of an `impl` is lexed
/// and parsed when it is read.
pub fn prelude_read(source: &[u8]) -> Result<usize, Vec<diag::Error>> {
    checked(source, &mut Timings::new()).map(|checked| checked.prelude_loaded)
}

/// The traits of numbers of the prelude that are read for a source file, which
/// are those that it, or a module it imports, calls for by name.
pub fn prelude_numeric_read(source: &[u8]) -> Result<Vec<String>, Vec<diag::Error>> {
    checked(source, &mut Timings::new()).map(|checked| checked.prelude_numeric)
}

/// `prelude_read`, with `prelude` as the prelude's source, which is divided
/// here as `build.rs` divides `std/prelude.smith`, so that a test can add to a
/// copy of it.
pub fn prelude_read_with(prelude: &str, source: &[u8]) -> Result<usize, Vec<diag::Error>> {
    let tokens = lexer::lex(source).map_err(in_order)?;
    let file = parser::parse(&tokens).map_err(in_order)?;
    check::check_with_source(file, &check::Library::standard(), prelude)
        .map(|checked| checked.prelude_loaded)
        .map_err(in_order)
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

/// Where a project's modules are.
pub trait Source {
    /// The source of the module with a name written as in a `use`, such as
    /// `db.transaction`, or `None` when no file holds it. A module of the
    /// standard library is never asked for.
    fn source(&self, module: &str) -> Option<String>;
}

impl<F: Fn(&str) -> Option<String>> Source for F {
    fn source(&self, module: &str) -> Option<String> {
        self(module)
    }
}

/// The files a project's modules are in.
pub struct Project<'a> {
    pub files: &'a dyn Source,
    /// The module the file that is built is, when its path gives it a name.
    /// Nothing imports from it.
    pub entry: Option<&'a str>,
}

impl Project<'_> {
    fn library(&self) -> check::Library<'_> {
        check::Library::in_project(self.files, self.entry)
    }
}

/// `check_timed` for a file that imports the modules of a project.
pub fn check_project_timed(
    source: &[u8],
    project: &Project,
    timings: &mut Timings,
) -> Result<Vec<diag::Error>, Vec<diag::Error>> {
    checked_in(source, &project.library(), timings).map(|checked| in_order(checked.warnings))
}

/// The errors, or else the warnings, found in reading the file of a module
/// that no file that is built reaches: what its `use` lines and its syntax
/// say.
pub fn check_module(name: &str, project: &Project) -> Vec<diag::Error> {
    match check::check_module(name, &project.library()) {
        Ok(found) | Err(found) => in_order(found),
    }
}

/// `compile_timed` for a file that imports the modules of a project.
pub fn compile_project_timed(
    source: &[u8],
    project: &Project,
    target: Target,
    timings: &mut Timings,
) -> Result<Vec<u8>, Vec<diag::Error>> {
    compile_project_files_timed(source, project, target, false, timings)
        .map(|built| built.executable)
}

/// What a build gives: the executable, and the files that are written beside
/// it, each by its extension.
pub struct Built {
    pub executable: Vec<u8>,
    pub beside: Vec<(&'static str, Vec<u8>)>,
    /// The parts of the code of the executable and the name of each function, when
    /// a map was asked for. The text of the map is made by `map_text`, where it
    /// is written.
    pub map: Option<(Vec<target::Part>, Vec<String>)>,
}

impl Built {
    /// The text of the map of the executable, when one was asked for.
    pub fn map_text(&self) -> Option<String> {
        let (parts, names) = self.map.as_ref()?;
        Some(target::map(names, parts))
    }
}

/// `compile_project_timed`, with the files that go beside the executable, and
/// with the map of the executable among them when `map` says so.
pub fn compile_project_files_timed(
    source: &[u8],
    project: &Project,
    target: Target,
    map: bool,
    timings: &mut Timings,
) -> Result<Built, Vec<diag::Error>> {
    let program = checked_in(source, &project.library(), timings)?.program;
    emit(program, target, map, timings)
}

/// The program a source file describes, in the scalar form, when the modules
/// the file imports are those given, each by its name and its source.
pub fn lower_project(
    source: &[u8],
    modules: &[(&str, &str)],
) -> Result<scalar::Program, Vec<diag::Error>> {
    let files = |name: &str| {
        modules
            .iter()
            .find(|(m, _)| *m == name)
            .map(|(_, s)| s.to_string())
    };
    let project = Project {
        files: &files,
        entry: None,
    };
    checked_in(source, &project.library(), &mut Timings::new()).map(|checked| checked.program)
}

/// `compile`, recording how long each stage takes.
pub fn compile_timed(
    source: &[u8],
    target: Target,
    timings: &mut Timings,
) -> Result<Vec<u8>, Vec<diag::Error>> {
    let program = lower_timed(source, timings)?;
    Ok(emit(program, target, false, timings)?.executable)
}

fn emit(
    program: scalar::Program,
    target: Target,
    map: bool,
    timings: &mut Timings,
) -> Result<Built, Vec<diag::Error>> {
    if program.asks_for_entropy() && !target.answers_entropy() {
        return Err(vec![diag::Error::new(
            diag::Pos::START,
            format!(
                "`entropy` is not built for the target `{}` yet, so a program that asks for it cannot be built for it",
                target.name()
            ),
        )]);
    }
    // The stage of `inline` is the time of `tail` too, which looks once at each
    // function and changes the few that call themselves last.
    let program = timings.stage("inline", || scalar::inline(scalar::tail(program)));
    let program = timings.stage("trim", || scalar::trim(program));
    let program = timings.stage("thread", || scalar::thread(program));
    let program = timings.stage("fold", || scalar::fold(program));
    let program = timings.stage("common", || scalar::common(program));
    let program = timings.stage("remove", || scalar::remove(program));
    Ok(timings.stage("emit", || {
        let (executable, parts) = target.emit_parts(&program);
        let beside = target.beside(&program);
        // The names are kept for the text of the map, which is made where it
        // is written.
        let map = map.then(|| {
            let names = program.functions.into_iter().map(|f| f.name).collect();
            (parts, names)
        });
        Built {
            executable,
            beside,
            map,
        }
    }))
}

fn lower_timed(source: &[u8], timings: &mut Timings) -> Result<scalar::Program, Vec<diag::Error>> {
    checked(source, timings).map(|checked| checked.program)
}

/// The source of a module of the standard library, for the errors that show a
/// line of it.
pub fn library_text(module: &str) -> &'static str {
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
    let mut file = parser::parse(&tokens).map_err(in_order)?;
    with_function_args(&mut file, source);
    check::check(file, &check::Library::new(library))
        .map(|checked| checked.program)
        .map_err(in_order)
}

/// The core term of each function of a source file that has one, which is a
/// function made of whole numbers and `bool`, or the errors `compile` reports
/// before a target is involved.
pub fn core_terms(source: &[u8]) -> Result<Vec<core::Function>, Vec<diag::Error>> {
    let tokens = lexer::lex(source).map_err(in_order)?;
    let mut file = parser::parse(&tokens).map_err(in_order)?;
    with_function_args(&mut file, source);
    check::check_core(file, &check::Library::standard())
        .map(|checked| checked.core)
        .map_err(in_order)
}

fn checked(source: &[u8], timings: &mut Timings) -> Result<check::Checked, Vec<diag::Error>> {
    checked_in(source, &check::Library::standard(), timings)
}

fn checked_in(
    source: &[u8],
    library: &check::Library,
    timings: &mut Timings,
) -> Result<check::Checked, Vec<diag::Error>> {
    let tokens = timings
        .stage("lex", || lexer::lex(source))
        .map_err(in_order)?;
    let mut file = timings
        .stage("parse", || parser::parse(&tokens))
        .map_err(in_order)?;
    with_function_args(&mut file, source);
    timings
        .stage("check", || check::check(file, library))
        .map_err(in_order)
}

/// Makes the structs of the arguments of the file's functions, when it writes
/// `Function`, which they are for, and notes the comments of its types, when
/// it writes `Describe`.
fn with_function_args(file: &mut parser::File, source: &[u8]) {
    file.text = Some(std::str::from_utf8(source).unwrap_or_default().into());
    if function_args::asks(file) {
        function_args::add(file, std::str::from_utf8(source).unwrap_or_default(), 0);
    }
    if item_notes::asks(file) {
        item_notes::add(file, std::str::from_utf8(source).unwrap_or_default());
    }
}

fn in_order(mut errors: Vec<diag::Error>) -> Vec<diag::Error> {
    errors.sort_by_key(|e| e.pos);
    errors.dedup();
    errors
}

/// The errors in every module of the standard library, each read whole and
/// checked whole, whether or not a program reaches what is in it.
pub fn check_standard_library() -> Vec<diag::Error> {
    in_order(check::check_standard_library())
}

/// The errors in every module of a library, given as a name and a source each,
/// read whole and checked whole.
pub fn check_library(modules: &[(&str, &str)]) -> Vec<diag::Error> {
    in_order(check::check_library(&check::Library::new(modules)))
}
