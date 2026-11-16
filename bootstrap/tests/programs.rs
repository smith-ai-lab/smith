//! The generator of programs from the part of the grammar the compiler
//! accepts, which can also place one mistake in what it builds. It records
//! what it built: what the program prints, or where its mistake is and what
//! the error must mention.
//!
//! A program is a few functions that take and return text, with bindings
//! and calls nested a few deep. It first builds a model of the program,
//! works out from the model what the program prints, and then writes the
//! model out as source, placing the mistake as it writes.

use std::fmt;

use smith::diag::Pos;

use crate::generate::Gen;

/// A generated file.
pub struct Case {
    pub source: Vec<u8>,
    /// What the program prints, when it has no mistake.
    pub stdout: Vec<u8>,
    /// The mistake placed in it, if any.
    pub mistake: Option<Expected>,
    /// How many functions `main` reaches, itself included.
    pub reached: usize,
}

impl fmt::Debug for Case {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", String::from_utf8_lossy(&self.source))
    }
}

/// The error a placed mistake must give.
#[derive(Clone, Debug)]
pub struct Expected {
    pub at: Pos,
    /// What the message must mention.
    pub mentions: String,
    /// What the `fix:` line must mention, or `None` if there must be no
    /// such line.
    pub fix: Option<String>,
}

/// The place just after `text`, which starts at the start of a file.
pub fn pos_after(text: &str) -> Pos {
    let line = text.matches('\n').count() + 1;
    let col = text.rsplit('\n').next().unwrap_or("").chars().count() + 1;
    Pos { line, col }
}

/// A piece of a valid string literal: how it is written, the text it
/// stands for, and how many places it takes on a terminal.
pub const PIECES: &[(&str, &str, usize)] = &[
    ("a", "a", 1),
    ("hello", "hello", 5),
    (" ", " ", 1),
    ("\t", "\t", 1),
    ("é", "é", 1),
    ("e\u{301}", "e\u{301}", 1),
    ("日本", "日本", 4),
    ("🙂", "🙂", 2),
    ("'", "'", 1),
    ("//", "//", 2),
    ("!@#$%^&*()", "!@#$%^&*()", 10),
    ("\\\"", "\"", 2),
    ("\\\\", "\\", 2),
    ("\\{", "{", 2),
    ("\\}", "}", 2),
    ("\\n", "\n", 2),
    ("\\t", "\t", 2),
];

/// The kinds of mistake the generator can place, each once in a file.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    // In the text.
    CarriageReturn,
    InvalidUtf8,
    // In the tokens, at a literal, or at the end of a function.
    UnknownEscape,
    BraceInString,
    ExpressionInString,
    NoClosingQuote,
    BackslashAtLineEnd,
    UnexpectedCharacter,
    CommentAfterToken,
    // In the syntax.
    ItemBefore,
    ItemAfter,
    NoName,
    GenericParameter,
    DefaultValue,
    Contract,
    ExpressionNotBuilt,
    Operator,
    Field,
    StatementNotBuilt,
    NoClosingParenthesis,
    TokenAfterStatement,
    NoClosingBrace,
    TokenAfterBody,
    // For the checker.
    NoMain,
    NoConsole,
    EffectNotBuilt,
    UnknownEffect,
    EffectTwice,
    TypeNotBuilt,
    UnknownFunction,
    UnknownName,
    DefinedTwice,
    Recursion,
    NoArgument,
    PrintEnd,
    TooFewArguments,
    TooManyArguments,
    NamedTwice,
    NoSuchParameter,
    PositionAfterName,
    TextWhereNothing,
    NothingWhereText,
}

const KINDS: &[Kind] = &[
    Kind::CarriageReturn,
    Kind::InvalidUtf8,
    Kind::UnknownEscape,
    Kind::BraceInString,
    Kind::ExpressionInString,
    Kind::NoClosingQuote,
    Kind::BackslashAtLineEnd,
    Kind::UnexpectedCharacter,
    Kind::CommentAfterToken,
    Kind::ItemBefore,
    Kind::ItemAfter,
    Kind::NoName,
    Kind::GenericParameter,
    Kind::DefaultValue,
    Kind::Contract,
    Kind::ExpressionNotBuilt,
    Kind::Operator,
    Kind::Field,
    Kind::StatementNotBuilt,
    Kind::NoClosingParenthesis,
    Kind::TokenAfterStatement,
    Kind::NoClosingBrace,
    Kind::TokenAfterBody,
    Kind::NoMain,
    Kind::NoConsole,
    Kind::EffectNotBuilt,
    Kind::UnknownEffect,
    Kind::EffectTwice,
    Kind::TypeNotBuilt,
    Kind::UnknownFunction,
    Kind::UnknownName,
    Kind::DefinedTwice,
    Kind::Recursion,
    Kind::NoArgument,
    Kind::PrintEnd,
    Kind::TooFewArguments,
    Kind::TooManyArguments,
    Kind::NamedTwice,
    Kind::NoSuchParameter,
    Kind::PositionAfterName,
    Kind::TextWhereNothing,
    Kind::NothingWhereText,
];

impl Kind {
    /// Whether the mistake goes in the signature of a function other than
    /// `main` that takes two parameters or more, or in a call of it.
    fn needs_parameters(self) -> bool {
        matches!(
            self,
            Kind::DefaultValue
                | Kind::TypeNotBuilt
                | Kind::TooFewArguments
                | Kind::TooManyArguments
                | Kind::NamedTwice
                | Kind::NoSuchParameter
                | Kind::PositionAfterName
        )
    }

    /// Whether the mistake goes at a literal.
    fn at_literal(self) -> bool {
        matches!(
            self,
            Kind::UnknownEscape
                | Kind::BraceInString
                | Kind::ExpressionInString
                | Kind::NoClosingQuote
                | Kind::BackslashAtLineEnd
                | Kind::UnexpectedCharacter
                | Kind::ExpressionNotBuilt
                | Kind::Operator
        )
    }

    /// Whether the mistake goes in a statement added at the end of `main`.
    fn ends_main(self) -> bool {
        matches!(
            self,
            Kind::StatementNotBuilt
                | Kind::NoClosingParenthesis
                | Kind::TokenAfterStatement
                | Kind::UnknownName
                | Kind::DefinedTwice
                | Kind::Recursion
                | Kind::NoArgument
                | Kind::PrintEnd
                | Kind::TextWhereNothing
                | Kind::NothingWhereText
        )
    }
}

const NOT_BUILT: &str = "not supported yet";

/// A valid program.
pub fn program(g: &mut Gen) -> Case {
    build(g, None)
}

/// A program with one mistake in it.
pub fn program_with_mistake(g: &mut Gen) -> Case {
    let kind = *g.pick(KINDS);
    build(g, Some(kind))
}

// The model of a program.

struct Func {
    name: String,
    params: Vec<&'static str>,
    /// Whether it returns `str`. If not, it returns nothing.
    gives: bool,
    body: Vec<Stmt>,
    /// Whether it needs `console`, as it prints or calls a function that
    /// carries it.
    console: bool,
    /// Whether it lists `console`.
    lists_console: bool,
}

enum Stmt {
    Define {
        name: String,
        typed: bool,
        value: Expr,
    },
    Expr(Expr),
}

enum Expr {
    /// A literal, as the pieces it is written with.
    Text(Vec<usize>),
    Name(String),
    Call {
        /// The function called, or `None` for `print`.
        callee: Option<usize>,
        args: Vec<Arg>,
    },
}

/// An argument: the parameter it is for, and whether it names it.
struct Arg {
    param: usize,
    named: bool,
    value: Expr,
}

const PARAMS: &[&str] = &["first", "second", "third", "word"];

/// How deep calls nest in an argument or a binding.
const DEPTH: usize = 3;

/// The functions of a program: `main` first, then the functions it
/// reaches. A function calls only functions after it, so none calls
/// itself, and every function is called by one before it. `shaped` asks
/// for a second function that returns text and takes two parameters or
/// more.
fn model(g: &mut Gen, shaped: bool) -> Vec<Func> {
    let mut fns = vec![Func {
        name: "main".to_string(),
        params: Vec::new(),
        gives: false,
        body: Vec::new(),
        console: false,
        lists_console: false,
    }];
    let others = g.below(4).max(usize::from(shaped));
    for i in 1..=others {
        let (count, gives) = if shaped && i == 1 {
            (2 + g.below(3), true)
        } else {
            (g.below(PARAMS.len() + 1), !g.one_in(3))
        };
        fns.push(Func {
            name: format!("f{i}"),
            params: PARAMS[..count].to_vec(),
            gives,
            body: Vec::new(),
            console: false,
            lists_console: false,
        });
    }
    for i in 0..fns.len() {
        fns[i].body = body(g, &fns, i);
    }
    // A function that nothing calls yet is called from the start of
    // `main`, last first.
    for j in (1..fns.len()).rev() {
        let called = (0..j).any(|i| fns[i].body.iter().any(|s| calls(stmt_expr(s), j)));
        if !called {
            let call = call_of(g, &fns, 0, j, &[], 1);
            let stmt = if fns[j].gives {
                print_of(call)
            } else {
                Stmt::Expr(call)
            };
            fns[0].body.insert(0, stmt);
        }
    }
    // Every program holds a literal and a call, for a mistake to go at.
    let exprs = || fns.iter().flat_map(|f| &f.body).map(stmt_expr);
    if !exprs().any(has_literal) || exprs().all(|e| calls_in(e) == 0) {
        let text = literal(g);
        fns[0].body.push(print_of(text));
    }
    for i in (0..fns.len()).rev() {
        let console = fns[i]
            .body
            .iter()
            .any(|s| needs_console(&fns, stmt_expr(s)));
        fns[i].console = console;
        fns[i].lists_console = console || g.one_in(4);
    }
    fns
}

fn stmt_expr(stmt: &Stmt) -> &Expr {
    match stmt {
        Stmt::Define { value, .. } | Stmt::Expr(value) => value,
    }
}

fn print_of(value: Expr) -> Stmt {
    Stmt::Expr(Expr::Call {
        callee: None,
        args: vec![Arg {
            param: 0,
            named: false,
            value,
        }],
    })
}

fn calls(expr: &Expr, f: usize) -> bool {
    match expr {
        Expr::Call { callee, args } => {
            *callee == Some(f) || args.iter().any(|a| calls(&a.value, f))
        }
        _ => false,
    }
}

fn has_literal(expr: &Expr) -> bool {
    match expr {
        Expr::Text(_) => true,
        Expr::Name(_) => false,
        Expr::Call { args, .. } => args.iter().any(|a| has_literal(&a.value)),
    }
}

/// Whether working out the expression needs `console`, given what the
/// functions after the one it is in list.
fn needs_console(fns: &[Func], expr: &Expr) -> bool {
    match expr {
        Expr::Call { callee, args } => {
            callee.is_none_or(|f| fns[f].lists_console)
                || args.iter().any(|a| needs_console(fns, &a.value))
        }
        _ => false,
    }
}

fn literal(g: &mut Gen) -> Expr {
    let count = g.below(10);
    Expr::Text((0..count).map(|_| g.below(PIECES.len())).collect())
}

/// The statements of function `i`.
fn body(g: &mut Gen, fns: &[Func], i: usize) -> Vec<Stmt> {
    let mut names: Vec<String> = fns[i].params.iter().map(|p| p.to_string()).collect();
    let mut stmts = Vec::new();
    let count = g.below(4);
    let later = |gives: bool| -> Vec<usize> {
        (i + 1..fns.len())
            .filter(|&j| fns[j].gives == gives)
            .collect()
    };
    for n in 0..count {
        let last = n + 1 == count && !fns[i].gives;
        let stmt = match g.below(4) {
            1 => {
                let name = format!("x{}", stmts.len() + 1);
                let typed = g.one_in(2);
                let value = text(g, fns, i, &names, DEPTH);
                names.push(name.clone());
                Stmt::Define { name, typed, value }
            }
            2 if !later(false).is_empty() => {
                let j = *g.pick(&later(false));
                Stmt::Expr(call_of(g, fns, i, j, &names, DEPTH))
            }
            3 if !later(true).is_empty() && !last => {
                let j = *g.pick(&later(true));
                Stmt::Expr(call_of(g, fns, i, j, &names, DEPTH))
            }
            _ => {
                let value = text(g, fns, i, &names, DEPTH);
                let named = g.one_in(4);
                Stmt::Expr(Expr::Call {
                    callee: None,
                    args: vec![Arg {
                        param: 0,
                        named,
                        value,
                    }],
                })
            }
        };
        stmts.push(stmt);
    }
    if fns[i].gives {
        stmts.push(Stmt::Expr(text(g, fns, i, &names, DEPTH)));
    }
    stmts
}

/// An expression that gives text, in function `i`, where `names` are
/// defined.
fn text(g: &mut Gen, fns: &[Func], i: usize, names: &[String], depth: usize) -> Expr {
    let callees: Vec<usize> = (i + 1..fns.len()).filter(|&j| fns[j].gives).collect();
    match g.below(3) {
        1 if !names.is_empty() => Expr::Name(g.pick(names).clone()),
        2 if depth > 0 && !callees.is_empty() => {
            let j = *g.pick(&callees);
            call_of(g, fns, i, j, names, depth)
        }
        _ => literal(g),
    }
}

/// A call of function `j` from function `i`. Some arguments are given by
/// position, and the rest by name in any order.
fn call_of(g: &mut Gen, fns: &[Func], i: usize, j: usize, names: &[String], depth: usize) -> Expr {
    let count = fns[j].params.len();
    let by_position = count - g.below(count + 1);
    let mut rest: Vec<usize> = (by_position..count).collect();
    let mut order: Vec<usize> = (0..by_position).collect();
    while !rest.is_empty() {
        order.push(rest.remove(g.below(rest.len())));
    }
    let args = order
        .into_iter()
        .map(|param| Arg {
            param,
            named: param >= by_position,
            value: text(g, fns, i, names, depth - 1),
        })
        .collect();
    Expr::Call {
        callee: Some(j),
        args,
    }
}

// What the model prints.

fn run(fns: &[Func], f: usize, args: Vec<Vec<u8>>, out: &mut Vec<u8>) -> Vec<u8> {
    let mut names: Vec<(String, Vec<u8>)> = (fns[f].params.iter())
        .map(|p| p.to_string())
        .zip(args)
        .collect();
    let mut last = Vec::new();
    for stmt in &fns[f].body {
        match stmt {
            Stmt::Define { name, value, .. } => {
                let value = eval(fns, &names, value, out);
                names.push((name.clone(), value));
            }
            Stmt::Expr(value) => last = eval(fns, &names, value, out),
        }
    }
    last
}

fn eval(fns: &[Func], names: &[(String, Vec<u8>)], expr: &Expr, out: &mut Vec<u8>) -> Vec<u8> {
    match expr {
        Expr::Text(pieces) => pieces.iter().flat_map(|&p| PIECES[p].1.bytes()).collect(),
        Expr::Name(name) => names.iter().find(|(n, _)| n == name).unwrap().1.clone(),
        Expr::Call { callee, args } => {
            let mut values = vec![Vec::new(); args.len()];
            for arg in args {
                values[arg.param] = eval(fns, names, &arg.value, out);
            }
            match callee {
                None => {
                    out.extend_from_slice(&values[0]);
                    out.push(b'\n');
                    Vec::new()
                }
                Some(f) => run(fns, *f, values, out),
            }
        }
    }
}

// Writing the model out.

/// The source being built, and the mistake once it is placed.
struct Builder<'a> {
    g: &'a mut Gen,
    kind: Option<Kind>,
    text: String,
    mistake: Option<Expected>,
    /// The literal the mistake goes at, if it goes at one, counting from
    /// 0 in the order they are written, and how many are written so far.
    literal_site: usize,
    literals: usize,
    /// The same, for the call the mistake goes at.
    call_site: usize,
    calls: usize,
    /// Whether a call of `f1` is written yet.
    f1_called: bool,
}

impl Builder<'_> {
    fn push(&mut self, text: &str) {
        self.text.push_str(text);
    }

    fn is(&self, kind: Kind) -> bool {
        self.kind == Some(kind)
    }

    /// Writes one of the choices.
    fn pick(&mut self, choices: &[&str]) {
        let choice = *self.g.pick(choices);
        self.push(choice);
    }

    /// Records the mistake as being where the next text goes.
    fn mark(&mut self, mentions: impl Into<String>, fix: Option<String>) {
        assert!(self.mistake.is_none(), "one mistake per file");
        self.mistake = Some(Expected {
            at: pos_after(&self.text),
            mentions: mentions.into(),
            fix,
        });
    }
}

const ITEMS: &[(&str, &str)] = &[
    ("struct A { x: f32 }", NOT_BUILT),
    ("use std.console (input)", NOT_BUILT),
    ("enum E {\n  a\n}", NOT_BUILT),
    ("pub fn f() {}", NOT_BUILT),
    (")", "definition"),
];

/// Functions that `main` does not reach, each with a mistake that would be
/// reported if it were checked.
pub const UNREACHED: &[&str] = &[
    "fn unused() { print(\"a\") }",
    "fn unused() -> str { nope() }",
    "fn unused() ! console { unused() }",
    "fn unused(a: i32) {}",
    "fn unused() ! console {\n  x = \"a\"\n  x = \"b\"\n  print(x, \"c\")\n}",
    "fn unused() -> str {}",
];

fn build(g: &mut Gen, kind: Option<Kind>) -> Case {
    let mut fns = model(g, kind.is_some_and(Kind::needs_parameters));
    let mut stdout = Vec::new();
    run(&fns, 0, Vec::new(), &mut stdout);
    if kind.is_some_and(Kind::ends_main)
        || matches!(kind, Some(Kind::EffectTwice | Kind::NoConsole))
    {
        fns[0].lists_console = true;
    }
    if kind == Some(Kind::NoConsole) && !fns.iter().any(|f| f.console) {
        fns[0].body.push(print_of(Expr::Text(Vec::new())));
        fns[0].console = true;
    }
    let order = {
        let mut rest: Vec<usize> = (0..fns.len()).collect();
        let mut order = Vec::new();
        while !rest.is_empty() {
            order.push(rest.remove(g.below(rest.len())));
        }
        order
    };
    let count_literals: usize = fns
        .iter()
        .flat_map(|f| &f.body)
        .map(|s| literals_in(stmt_expr(s)))
        .sum();
    let count_calls: usize = fns
        .iter()
        .flat_map(|f| &f.body)
        .map(|s| calls_in(stmt_expr(s)))
        .sum();
    let fn_site = match kind {
        Some(Kind::NoConsole) => {
            let needing: Vec<usize> = (0..order.len())
                .filter(|&at| fns[order[at]].console)
                .collect();
            *g.pick(&needing)
        }
        Some(Kind::NoClosingBrace) => order.len() - 1,
        _ => g.below(order.len()),
    };
    let mut b = Builder {
        literal_site: g.below(count_literals.max(1)),
        call_site: g.below(count_calls.max(1)),
        g,
        kind,
        text: String::new(),
        mistake: None,
        literals: 0,
        calls: 0,
        f1_called: false,
    };

    for _ in 0..b.g.below(3) {
        b.pick(&["\n", "// a comment\n", "  // indented\n", "//\n"]);
    }
    if b.is(Kind::ItemBefore) {
        let (item, mentions) = *b.g.pick(ITEMS);
        b.mark(mentions, None);
        b.push(item);
        b.push("\n");
    }
    if b.is(Kind::NoMain) {
        b.mistake = Some(Expected {
            at: pos_after(""),
            mentions: "`main`".to_string(),
            fix: None,
        });
    }
    if b.g.one_in(4) {
        b.pick(UNREACHED);
        b.push("\n");
        b.pick(&["", "\n"]);
    }
    for (at, &f) in order.iter().enumerate() {
        if at > 0 {
            b.pick(&["\n", "\n\n", "\n// next\n"]);
        }
        let closed = function(&mut b, &fns, f, at == fn_site);
        if !closed {
            return finish(b, stdout, fns.len());
        }
    }
    if b.g.below(3) > 0 || b.is(Kind::ItemAfter) {
        b.push("\n");
        for _ in 0..b.g.below(3) {
            b.pick(&["\n", "// the end\n"]);
        }
    }
    if b.is(Kind::ItemAfter) {
        let (item, mentions) = *b.g.pick(ITEMS);
        b.mark(mentions, None);
        b.push(item);
        b.push("\n");
    }
    finish(b, stdout, fns.len())
}

fn literals_in(expr: &Expr) -> usize {
    match expr {
        Expr::Text(_) => 1,
        Expr::Name(_) => 0,
        Expr::Call { args, .. } => args.iter().map(|a| literals_in(&a.value)).sum(),
    }
}

fn calls_in(expr: &Expr) -> usize {
    match expr {
        Expr::Call { args, .. } => 1 + args.iter().map(|a| calls_in(&a.value)).sum::<usize>(),
        _ => 0,
    }
}

/// A function's signature as the compiler's advice writes it, listing
/// `console`.
fn signature_with_console(f: &Func) -> String {
    let params: Vec<String> = f.params.iter().map(|p| format!("{p}: str")).collect();
    let result = if f.gives { " -> str" } else { "" };
    format!("fn {}({}){result} ! console", f.name, params.join(", "))
}

/// Writes function `f`, which holds the mistake if `site`. Gives whether
/// its body is closed.
fn function(b: &mut Builder, fns: &[Func], f: usize, site: bool) -> bool {
    let func = &fns[f];
    let kind = b.kind;
    let here = |k: Kind| site && kind == Some(k);
    b.push("fn");
    b.pick(&[" ", "  ", "\t"]);
    if here(Kind::NoName) {
        b.mark("name", None);
    } else if f == 0 && b.is(Kind::NoMain) {
        b.push("hello");
    } else {
        b.push(&func.name);
    }
    if here(Kind::GenericParameter) {
        b.mark(NOT_BUILT, None);
        b.push("[T]");
    }
    b.push("(");
    if func.params.is_empty() {
        b.pick(&["", " ", "\n", "\n  // no parameters\n"]);
    }
    for (i, p) in func.params.iter().enumerate() {
        if i > 0 {
            b.pick(&[", ", ",", ",\n  "]);
        }
        b.push(p);
        b.pick(&[": ", ":"]);
        if f == 1 && i == 0 && b.is(Kind::TypeNotBuilt) {
            b.mark(NOT_BUILT, None);
            b.push("i32");
        } else {
            b.push("str");
        }
        if f == 1 && i == 1 && b.is(Kind::DefaultValue) {
            b.push(" ");
            b.mark(NOT_BUILT, None);
            b.push("= \"a\"");
        }
    }
    if !func.params.is_empty() {
        b.pick(&["", ","]);
    }
    b.push(")");
    if func.gives {
        b.pick(&[" -> ", "->", " ->  "]);
        b.push("str");
    }
    effects(b, func, here(Kind::NoConsole), f == 0);
    if here(Kind::Contract) {
        b.pick(&[" ", "\n"]);
        b.mark(NOT_BUILT, None);
        b.push("requires true");
    }
    b.pick(&[" ", "\n", "  ", "\n// the body\n"]);
    b.push("{");

    let console_site = here(Kind::NoConsole).then(|| signature_with_console(func));
    let mut extra = Vec::new();
    if f == 0
        && let Some(kind) = b.kind.filter(|k| k.ends_main())
    {
        extra.push(kind);
    }
    let one_line = func.body.len() == 1 && extra.is_empty() && b.g.one_in(2);
    let mut names: Vec<String> = Vec::new();
    for stmt in &func.body {
        if one_line {
            b.pick(&[" ", "  "]);
        } else {
            b.pick(&["\n  ", "\n\t", "\n\n  ", "\n  // a step\n  "]);
        }
        match stmt {
            Stmt::Define { name, typed, value } => {
                b.push(name);
                if *typed {
                    b.pick(&[": str = ", ":str = "]);
                } else {
                    b.pick(&[" = ", "=", "  =  "]);
                }
                expr(b, fns, value, &console_site);
                names.push(name.clone());
            }
            Stmt::Expr(value) => expr(b, fns, value, &console_site),
        }
    }
    for kind in extra {
        b.pick(&["\n  ", "\n\n  ", "\n  // more\n  "]);
        main_statement(b, kind, &names);
        if kind == Kind::NoClosingParenthesis {
            return true;
        }
    }
    if func.body.is_empty() {
        b.pick(&["", "\n"]);
    } else if one_line {
        b.pick(&[" ", ""]);
    } else {
        b.push("\n");
    }
    if here(Kind::NoClosingBrace) {
        b.mark("`}`", None);
        return false;
    }
    b.push("}");
    if here(Kind::TokenAfterBody) {
        b.push(" ");
        b.mark("line break", None);
        b.push("x");
    }
    if here(Kind::CommentAfterToken) {
        b.push(" ");
        b.mark("own line", None);
        b.push("// done");
    }
    true
}

/// A statement at the end of `main` that holds the mistake.
fn main_statement(b: &mut Builder, kind: Kind, names: &[String]) {
    match kind {
        Kind::StatementNotBuilt => {
            b.mark(NOT_BUILT, None);
            b.pick(&["var x = \"a\"", "x += \"a\"", "with a", "break"]);
        }
        Kind::NoClosingParenthesis => {
            b.push("print(\"a\" ");
            b.mark("expected", None);
            b.push("}");
        }
        Kind::TokenAfterStatement => {
            b.push("print(\"a\") ");
            b.mark("line break", None);
            b.push("x");
        }
        Kind::UnknownName => {
            b.push("print(");
            b.mark("`nope`", None);
            b.push("nope)");
        }
        Kind::DefinedTwice => {
            let name = names.first().cloned().unwrap_or_else(|| {
                b.push("twice = \"a\"\n  ");
                "twice".to_string()
            });
            b.mark(format!("`{name}`"), None);
            b.push(&name);
            b.push(" = \"b\"");
        }
        Kind::Recursion => {
            b.mark(NOT_BUILT, None);
            b.push("main()");
        }
        Kind::NoArgument => {
            b.push("print(");
            b.mark("needs the text", Some("`print(\"hello\")`".to_string()));
            b.push(")");
        }
        Kind::PrintEnd => {
            b.push("print(\"a\", ");
            b.mark(NOT_BUILT, None);
            b.pick(&["\"b\"", "end: \"\"", "to: stderr"]);
            b.push(")");
        }
        Kind::TextWhereNothing => {
            b.mark("returns nothing", None);
            b.push("\"a\"");
        }
        Kind::NothingWhereText => {
            b.push("print(");
            b.mark("`print` returns nothing", None);
            b.push("print(\"a\"))");
        }
        _ => unreachable!("not a statement at the end of `main`"),
    }
}

/// The `!` and the list of effects of a function. The list of `main`
/// holds the mistake, if it is in a list.
fn effects(b: &mut Builder, func: &Func, no_console: bool, main: bool) {
    // Each effect, with what its error must mention if it is the mistake.
    let mut list: Vec<(&str, Option<String>)> = Vec::new();
    if func.lists_console && !no_console {
        list.push(("console", None));
    }
    if main {
        let extra = match b.kind {
            Some(Kind::EffectNotBuilt) => {
                let name = *b.g.pick(&[
                    "fs", "net", "time", "ui", "alloc", "spawn", "gpu", "diverge", "exec",
                ]);
                Some((name, NOT_BUILT.to_string()))
            }
            Some(Kind::UnknownEffect) => {
                let name = *b.g.pick(&["consol", "io", "stdout", "Console"]);
                Some((name, format!("`{name}`")))
            }
            _ => None,
        };
        if let Some((name, mentions)) = extra {
            let at = b.g.below(2).min(list.len());
            list.insert(at, (name, Some(mentions)));
        }
        if b.is(Kind::EffectTwice) {
            // Whichever is written second is the mistake.
            let at = b.g.below(2);
            list.insert(at, ("console", None));
            list[1].1 = Some("already listed".to_string());
        }
    }
    if list.is_empty() {
        return;
    }
    b.pick(&[" ", "", "  "]);
    b.push("!");
    b.pick(&[" ", "", "\t"]);
    for (i, (name, mistake)) in list.into_iter().enumerate() {
        if i > 0 {
            b.push(",");
            b.pick(&[" ", "", "\n  ", "\n// and\n"]);
        }
        if let Some(mentions) = mistake {
            b.mark(mentions, None);
        }
        b.push(name);
    }
}

/// Writes an expression. `console_site` is the advice to expect, when the
/// mistake is that the function lacks `console`.
fn expr(b: &mut Builder, fns: &[Func], expr: &Expr, console_site: &Option<String>) {
    match expr {
        Expr::Text(pieces) => {
            let site = b.kind.is_some_and(Kind::at_literal) && b.literals == b.literal_site;
            b.literals += 1;
            if site {
                bad_literal(b, pieces);
            } else {
                literal_text(b, pieces, None);
            }
        }
        Expr::Name(name) => b.push(name),
        Expr::Call { callee, args } => {
            let site = b.calls == b.call_site;
            b.calls += 1;
            let needs = callee.is_none_or(|f| fns[f].lists_console);
            if needs
                && b.mistake.is_none()
                && let Some(fix) = console_site
            {
                b.mark("`console`", Some(fix.clone()));
            }
            if site && b.is(Kind::UnknownFunction) {
                let name = *b.g.pick(&["prin", "println", "nope", "Print"]);
                b.mark(format!("`{name}`"), None);
                b.push(name);
            } else {
                b.push(callee.map_or("print", |f| fns[f].name.as_str()));
            }
            b.push("(");
            b.pick(&["", "\n    ", "\n    // the arguments\n    "]);
            // The mistake about arguments goes in the first call of `f1`.
            let first_of_f1 = *callee == Some(1) && !b.f1_called;
            b.f1_called |= *callee == Some(1);
            let kind = b.kind.filter(|k| first_of_f1 && k.needs_parameters());
            call_args(b, fns, *callee, args, kind, console_site);
            if site && b.is(Kind::Field) {
                b.mark(NOT_BUILT, None);
                b.push(".x");
            }
        }
    }
}

/// Writes the arguments of a call and its `)`, with the mistake about
/// arguments if `kind` is one.
fn call_args(
    b: &mut Builder,
    fns: &[Func],
    callee: Option<usize>,
    args: &[Arg],
    kind: Option<Kind>,
    console_site: &Option<String>,
) {
    let params: &[&str] = callee.map_or(&["text"], |f| &fns[f].params);
    // How each argument is written: by name or not.
    let named = |a: &Arg| match kind {
        Some(Kind::TooFewArguments | Kind::TooManyArguments) => false,
        Some(Kind::NamedTwice | Kind::NoSuchParameter | Kind::PositionAfterName) => true,
        _ => a.named,
    };
    let mut written: Vec<&Arg> = args.iter().collect();
    if matches!(kind, Some(Kind::TooFewArguments | Kind::TooManyArguments)) {
        written.sort_by_key(|a| a.param);
    }
    let dropped = (kind == Some(Kind::TooFewArguments)).then(|| written.pop().unwrap());
    for (i, arg) in written.iter().enumerate() {
        if i > 0 {
            b.pick(&[", ", ",", ",\n    "]);
        }
        if named(arg) {
            b.push(params[arg.param]);
            b.pick(&[": ", ":"]);
        }
        expr(b, fns, &arg.value, console_site);
    }
    // One more argument, by name if it has one.
    let more = |b: &mut Builder, mentions: String, fix: Option<String>, name: Option<&str>| {
        b.push(", ");
        b.mark(mentions, fix);
        if let Some(name) = name {
            b.push(name);
            b.push(": ");
        }
        b.push("\"a\"");
    };
    match kind {
        Some(Kind::TooManyArguments) => more(b, "takes".to_string(), None, None),
        Some(Kind::NamedTwice) => {
            let name = params[written[0].param];
            more(b, format!("`{name}`"), None, Some(name));
        }
        Some(Kind::NoSuchParameter) => more(b, "`nope`".to_string(), None, Some("nope")),
        Some(Kind::PositionAfterName) => {
            more(
                b,
                "by position".to_string(),
                Some("name it".to_string()),
                None,
            );
        }
        _ => {}
    }
    if !written.is_empty() {
        b.pick(&["", ","]);
    }
    if let Some(arg) = dropped {
        b.mark(format!("`{}`", params[arg.param]), None);
    }
    b.push(")");
}

/// Writes a literal with the mistake in it, or in place of it.
fn bad_literal(b: &mut Builder, pieces: &[usize]) {
    match b.kind {
        Some(Kind::UnexpectedCharacter) => {
            b.mark("unexpected character", None);
            b.pick(&["#", "$", "?", "`", ";", "%", "é", "日"]);
            literal_text(b, pieces, None);
        }
        Some(Kind::ExpressionNotBuilt) => {
            b.mark(NOT_BUILT, None);
            b.pick(&["1", "(\"a\")", "-\"a\"", "[\"a\"]", "fn() { \"a\" }", "if"]);
        }
        Some(Kind::Operator) => {
            literal_text(b, pieces, None);
            b.push(" ");
            b.mark(NOT_BUILT, None);
            b.pick(&["+ \"b\"", "== \"b\"", "and x", "..\"b\""]);
        }
        Some(Kind::NoClosingQuote) => {
            let opening = pos_after(&b.text);
            literal_text(b, pieces, Some(Kind::NoClosingQuote));
            b.mistake = Some(Expected {
                at: opening,
                mentions: "no closing quote".to_string(),
                fix: None,
            });
            // The string takes in the rest of its line.
            b.push("\n");
        }
        Some(Kind::BackslashAtLineEnd) => {
            literal_text(b, pieces, Some(Kind::BackslashAtLineEnd));
            b.push("\n");
        }
        kind => literal_text(b, pieces, kind),
    }
}

/// Writes a literal, with a mistake inside it if `kind` is one that goes
/// there, between two of its pieces.
fn literal_text(b: &mut Builder, pieces: &[usize], kind: Option<Kind>) {
    let bad_at = b.g.below(pieces.len() + 1);
    b.push("\"");
    for i in 0..=pieces.len() {
        if i == bad_at {
            match kind {
                Some(Kind::UnknownEscape) => {
                    b.mark("unknown escape", None);
                    b.push("\\");
                    b.pick(&["q", "x", "0", " ", "u", "é", "'", "r"]);
                }
                Some(Kind::BraceInString) => {
                    b.mark("closes no expression", Some("`\\}`".to_string()));
                    b.push("}");
                }
                Some(Kind::ExpressionInString) => {
                    b.mark(NOT_BUILT, None);
                    b.pick(&["{x}", "{name}", "{1 + 2}", "{f(\"a\")}"]);
                }
                _ => {}
            }
        }
        if let Some(&p) = pieces.get(i) {
            b.push(PIECES[p].0);
        }
    }
    match kind {
        Some(Kind::NoClosingQuote) => {}
        Some(Kind::BackslashAtLineEnd) => {
            b.mark("unknown escape", None);
            b.push("\\");
        }
        _ => b.push("\""),
    }
}

/// The file, with a mistake in its text placed at any character, if that
/// is the kind.
fn finish(b: Builder, stdout: Vec<u8>, reached: usize) -> Case {
    let mut source = b.text.clone().into_bytes();
    let mut mistake = b.mistake;
    let inserted: Option<(&[u8], &str, Option<&str>)> = match b.kind {
        Some(Kind::CarriageReturn) => Some((b"\r", "carriage return", Some("line feed"))),
        Some(Kind::InvalidUtf8) => Some((b"\xff", "UTF-8", None)),
        _ => None,
    };
    if let Some((bytes, mentions, fix)) = inserted {
        let boundaries: Vec<usize> = (0..=b.text.len())
            .filter(|i| b.text.is_char_boundary(*i))
            .collect();
        let at = *b.g.pick(&boundaries);
        mistake = Some(Expected {
            at: pos_after(&b.text[..at]),
            mentions: mentions.to_string(),
            fix: fix.map(str::to_string),
        });
        source.splice(at..at, bytes.iter().copied());
    }
    assert!(
        b.kind.is_none() || mistake.is_some(),
        "the mistake {:?} was placed",
        b.kind
    );
    Case {
        source,
        stdout,
        mistake,
        reached,
    }
}
