//! The generator of programs from the part of the grammar the compiler
//! accepts, which can also place one mistake in what it builds. It records
//! what it built: what the program prints, or where its mistake is and what
//! the error must mention.
//!
//! A program is a few structs, whose fields are text or other structs, and
//! a few functions that take and return text and structs, with bindings,
//! constructions, field reads, and calls nested a few deep. It first builds
//! a model of the program, works out from the model what the program
//! prints, and then writes the model out as source, placing the mistake as
//! it writes.

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
    StatementNotBuilt,
    NoClosingParenthesis,
    TokenAfterStatement,
    NoClosingBrace,
    TokenAfterBody,
    StructNotBuilt,
    // For the checker.
    NoMain,
    NoConsole,
    EffectNotBuilt,
    UnknownEffect,
    EffectTwice,
    TypeNotBuilt,
    UnknownType,
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
    WrongType,
    FieldLeftOut,
    FieldTwice,
    NoSuchField,
    FieldByPosition,
    NoSuchFieldRead,
    NotAStruct,
    ContainsItself,
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
    Kind::StatementNotBuilt,
    Kind::NoClosingParenthesis,
    Kind::TokenAfterStatement,
    Kind::NoClosingBrace,
    Kind::TokenAfterBody,
    Kind::StructNotBuilt,
    Kind::NoMain,
    Kind::NoConsole,
    Kind::EffectNotBuilt,
    Kind::UnknownEffect,
    Kind::EffectTwice,
    Kind::TypeNotBuilt,
    Kind::UnknownType,
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
    Kind::WrongType,
    Kind::FieldLeftOut,
    Kind::FieldTwice,
    Kind::NoSuchField,
    Kind::FieldByPosition,
    Kind::NoSuchFieldRead,
    Kind::NotAStruct,
    Kind::ContainsItself,
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

    /// Whether the mistake needs a struct with a field, and a value of a
    /// struct for the statement it goes in.
    fn needs_struct(self) -> bool {
        matches!(
            self,
            Kind::WrongType
                | Kind::FieldLeftOut
                | Kind::FieldTwice
                | Kind::NoSuchField
                | Kind::FieldByPosition
                | Kind::NoSuchFieldRead
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
                | Kind::UnknownType
                | Kind::ContainsItself
        ) || self.needs_struct()
    }

    /// Whether the mistake is in a definition of its own, added before or
    /// after the program's.
    fn own_item(self) -> bool {
        matches!(self, Kind::StructNotBuilt | Kind::ContainsItself)
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

/// A type: text, or a struct by its place among the structs.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Type {
    Str,
    Struct(usize),
}

struct StructDef {
    name: &'static str,
    /// Its fields, in the order they are declared. A field's struct comes
    /// before this one, so no struct contains itself.
    fields: Vec<(&'static str, Type)>,
}

struct Func {
    name: String,
    params: Vec<(&'static str, Type)>,
    /// What it returns, or `None` for nothing.
    result: Option<Type>,
    body: Vec<Stmt>,
    /// Whether it needs `console`, as it prints or calls a function that
    /// carries it.
    console: bool,
    /// Whether it lists `console`.
    lists_console: bool,
}

/// The structs and the functions of a program.
struct Model {
    structs: Vec<StructDef>,
    fns: Vec<Func>,
}

impl Model {
    fn type_name(&self, ty: Type) -> &str {
        match ty {
            Type::Str => "str",
            Type::Struct(s) => self.structs[s].name,
        }
    }

    /// Whether the type is that of what function `f` returns.
    fn returns(&self, f: usize, ty: Type) -> bool {
        self.fns[f].result == Some(ty)
    }
}

enum Stmt {
    Define {
        name: String,
        ty: Type,
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
    /// The construction of a struct, with each field by its place in the
    /// struct, in the order they are written.
    Construct {
        s: usize,
        fields: Vec<(usize, Expr)>,
    },
    /// A read of field `field` of struct `s`.
    Field {
        of: Box<Expr>,
        s: usize,
        field: usize,
    },
}

/// An argument: the parameter it is for, and whether it names it.
struct Arg {
    param: usize,
    named: bool,
    value: Expr,
}

const PARAMS: &[&str] = &["first", "second", "third", "word"];

const STRUCTS: &[&str] = &["Name", "Pair", "Point"];

const FIELDS: &[&str] = &["first", "last", "x", "y", "inner"];

/// How deep calls nest in an argument or a binding.
const DEPTH: usize = 3;

/// A type for a parameter, a binding, or a result: text, or now and then
/// a struct.
fn some_type(g: &mut Gen, structs: &[StructDef]) -> Type {
    if !structs.is_empty() && g.one_in(3) {
        Type::Struct(g.below(structs.len()))
    } else {
        Type::Str
    }
}

/// The structs of a program. `with_field` asks for one with a field.
fn structs(g: &mut Gen, with_field: bool) -> Vec<StructDef> {
    let count = g.below(STRUCTS.len() + 1).max(usize::from(with_field));
    let mut structs = Vec::new();
    for (i, &name) in STRUCTS[..count].iter().enumerate() {
        let mut names = FIELDS.to_vec();
        let fields = g.below(4).max(usize::from(with_field && i == 0));
        let fields = (0..fields)
            .map(|_| {
                let field = names.remove(g.below(names.len()));
                let ty = if i > 0 && g.one_in(3) {
                    Type::Struct(g.below(i))
                } else {
                    Type::Str
                };
                (field, ty)
            })
            .collect();
        structs.push(StructDef { name, fields });
    }
    structs
}

/// The program: its structs, then its functions, `main` first and then
/// the functions it reaches. A function calls only functions after it, so
/// none calls itself, and every function is called by one before it.
/// `shaped` asks for a second function that returns text and takes two
/// parameters or more, and `with_field` for a struct with a field.
fn model(g: &mut Gen, shaped: bool, with_field: bool) -> Model {
    let structs = structs(g, with_field);
    let mut fns = vec![Func {
        name: "main".to_string(),
        params: Vec::new(),
        result: None,
        body: Vec::new(),
        console: false,
        lists_console: false,
    }];
    let others = g.below(4).max(usize::from(shaped));
    for i in 1..=others {
        let (count, result) = if shaped && i == 1 {
            (2 + g.below(3), Some(Type::Str))
        } else {
            let count = g.below(PARAMS.len() + 1);
            let result = (!g.one_in(3)).then(|| some_type(g, &structs));
            (count, result)
        };
        let params = PARAMS[..count]
            .iter()
            .map(|&p| (p, some_type(g, &structs)))
            .collect();
        fns.push(Func {
            name: format!("f{i}"),
            params,
            result,
            body: Vec::new(),
            console: false,
            lists_console: false,
        });
    }
    let mut m = Model { structs, fns };
    for i in 0..m.fns.len() {
        m.fns[i].body = body(g, &m, i);
    }
    // A function that nothing calls yet is called from the start of
    // `main`, last first.
    for j in (1..m.fns.len()).rev() {
        let called = (0..j).any(|i| m.fns[i].body.iter().any(|s| calls(stmt_expr(s), j)));
        if !called {
            let call = call_of(g, &m, 0, j, &[], 1);
            let stmt = match m.fns[j].result {
                Some(Type::Str) => print_of(call),
                Some(ty) => Stmt::Define {
                    name: format!("y{j}"),
                    ty,
                    typed: false,
                    value: call,
                },
                None => Stmt::Expr(call),
            };
            m.fns[0].body.insert(0, stmt);
        }
    }
    // Every program holds a literal, and a call that gives text or
    // nothing, for a mistake to go at.
    let has_plain_call =
        (m.fns.iter().flat_map(|f| &f.body)).any(|s| plain_calls_in(&m, stmt_expr(s)) > 0);
    let has_literal = (m.fns.iter().flat_map(|f| &f.body)).any(|s| literals_in(stmt_expr(s)) > 0);
    if !has_literal || !has_plain_call {
        let text = literal(g);
        m.fns[0].body.push(print_of(text));
    }
    for i in (0..m.fns.len()).rev() {
        let console = m.fns[i]
            .body
            .iter()
            .any(|s| needs_console(&m, stmt_expr(s)));
        m.fns[i].console = console;
        m.fns[i].lists_console = console || g.one_in(4);
    }
    m
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

/// The expressions directly inside an expression.
fn inside(expr: &Expr) -> Vec<&Expr> {
    match expr {
        Expr::Text(_) | Expr::Name(_) => Vec::new(),
        Expr::Call { args, .. } => args.iter().map(|a| &a.value).collect(),
        Expr::Construct { fields, .. } => fields.iter().map(|(_, e)| e).collect(),
        Expr::Field { of, .. } => vec![of],
    }
}

fn calls(expr: &Expr, f: usize) -> bool {
    matches!(expr, Expr::Call { callee: Some(c), .. } if *c == f)
        || inside(expr).into_iter().any(|e| calls(e, f))
}

/// Whether working out the expression needs `console`, given what the
/// functions after the one it is in list.
fn needs_console(m: &Model, expr: &Expr) -> bool {
    matches!(expr, Expr::Call { callee, .. } if callee.is_none_or(|f| m.fns[f].lists_console))
        || inside(expr).into_iter().any(|e| needs_console(m, e))
}

fn literals_in(expr: &Expr) -> usize {
    usize::from(matches!(expr, Expr::Text(_)))
        + inside(expr).into_iter().map(literals_in).sum::<usize>()
}

fn calls_in(expr: &Expr) -> usize {
    usize::from(matches!(expr, Expr::Call { .. }))
        + inside(expr).into_iter().map(calls_in).sum::<usize>()
}

/// Whether the call gives text or nothing, rather than a struct.
fn plain(m: &Model, callee: Option<usize>) -> bool {
    callee.is_none_or(|f| !matches!(m.fns[f].result, Some(Type::Struct(_))))
}

/// How many calls in the expression give text or nothing.
fn plain_calls_in(m: &Model, expr: &Expr) -> usize {
    let here = matches!(expr, Expr::Call { callee, .. } if plain(m, *callee));
    usize::from(here)
        + inside(expr)
            .into_iter()
            .map(|e| plain_calls_in(m, e))
            .sum::<usize>()
}

fn literal(g: &mut Gen) -> Expr {
    let count = g.below(10);
    Expr::Text((0..count).map(|_| g.below(PIECES.len())).collect())
}

/// The statements of function `i`.
fn body(g: &mut Gen, m: &Model, i: usize) -> Vec<Stmt> {
    let mut names: Vec<(String, Type)> = (m.fns[i].params.iter())
        .map(|(p, ty)| (p.to_string(), *ty))
        .collect();
    let mut stmts = Vec::new();
    let count = g.below(4);
    let later = |gives: bool| -> Vec<usize> {
        (i + 1..m.fns.len())
            .filter(|&j| m.fns[j].result.is_some() == gives)
            .collect()
    };
    for n in 0..count {
        let last = n + 1 == count && m.fns[i].result.is_none();
        let stmt = match g.below(4) {
            1 => {
                let name = format!("x{}", stmts.len() + 1);
                let typed = g.one_in(2);
                let ty = some_type(g, &m.structs);
                let value = value(g, m, i, &names, ty, DEPTH);
                names.push((name.clone(), ty));
                Stmt::Define {
                    name,
                    ty,
                    typed,
                    value,
                }
            }
            2 if !later(false).is_empty() => {
                let j = *g.pick(&later(false));
                Stmt::Expr(call_of(g, m, i, j, &names, DEPTH))
            }
            3 if !later(true).is_empty() && !last => {
                let j = *g.pick(&later(true));
                Stmt::Expr(call_of(g, m, i, j, &names, DEPTH))
            }
            _ => {
                let value = value(g, m, i, &names, Type::Str, DEPTH);
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
    if let Some(ty) = m.fns[i].result {
        stmts.push(Stmt::Expr(value(g, m, i, &names, ty, DEPTH)));
    }
    stmts
}

/// An expression of the type, in function `i`, where `names` are defined.
fn value(
    g: &mut Gen,
    m: &Model,
    i: usize,
    names: &[(String, Type)],
    ty: Type,
    depth: usize,
) -> Expr {
    let named: Vec<&String> = (names.iter())
        .filter(|(_, t)| *t == ty)
        .map(|(n, _)| n)
        .collect();
    let callees: Vec<usize> = (i + 1..m.fns.len()).filter(|&j| m.returns(j, ty)).collect();
    // The fields of this type, each as its struct and its place there.
    let sources: Vec<(usize, usize)> = (m.structs.iter().enumerate())
        .flat_map(|(s, def)| {
            (def.fields.iter().enumerate())
                .filter(|(_, (_, t))| *t == ty)
                .map(move |(f, _)| (s, f))
        })
        .collect();
    match g.below(4) {
        1 if !named.is_empty() => Expr::Name(g.pick(&named).to_string()),
        2 if depth > 0 && !callees.is_empty() => {
            let j = *g.pick(&callees);
            call_of(g, m, i, j, names, depth)
        }
        3 if depth > 0 && !sources.is_empty() => {
            let (s, field) = *g.pick(&sources);
            let of = value(g, m, i, names, Type::Struct(s), depth - 1);
            Expr::Field {
                of: Box::new(of),
                s,
                field,
            }
        }
        _ => match ty {
            Type::Str => literal(g),
            Type::Struct(s) => construct(g, m, i, names, s, depth),
        },
    }
}

/// The construction of struct `s`, with its fields in any order.
fn construct(
    g: &mut Gen,
    m: &Model,
    i: usize,
    names: &[(String, Type)],
    s: usize,
    depth: usize,
) -> Expr {
    let mut rest: Vec<usize> = (0..m.structs[s].fields.len()).collect();
    let mut fields = Vec::new();
    while !rest.is_empty() {
        let field = rest.remove(g.below(rest.len()));
        let ty = m.structs[s].fields[field].1;
        let value = value(g, m, i, names, ty, depth.saturating_sub(1));
        fields.push((field, value));
    }
    Expr::Construct { s, fields }
}

/// A call of function `j` from function `i`. Some arguments are given by
/// position, and the rest by name in any order.
fn call_of(
    g: &mut Gen,
    m: &Model,
    i: usize,
    j: usize,
    names: &[(String, Type)],
    depth: usize,
) -> Expr {
    let count = m.fns[j].params.len();
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
            value: value(g, m, i, names, m.fns[j].params[param].1, depth - 1),
        })
        .collect();
    Expr::Call {
        callee: Some(j),
        args,
    }
}

// What the model prints.

/// A value: text, or a struct's fields in the order they are declared.
#[derive(Clone)]
enum Val {
    Text(Vec<u8>),
    Struct(Vec<Val>),
}

fn run(m: &Model, f: usize, args: Vec<Val>, out: &mut Vec<u8>) -> Val {
    let mut names: Vec<(String, Val)> = (m.fns[f].params.iter())
        .map(|(p, _)| p.to_string())
        .zip(args)
        .collect();
    let mut last = Val::Text(Vec::new());
    for stmt in &m.fns[f].body {
        match stmt {
            Stmt::Define { name, value, .. } => {
                let value = eval(m, &names, value, out);
                names.push((name.clone(), value));
            }
            Stmt::Expr(value) => last = eval(m, &names, value, out),
        }
    }
    last
}

fn eval(m: &Model, names: &[(String, Val)], expr: &Expr, out: &mut Vec<u8>) -> Val {
    match expr {
        Expr::Text(pieces) => Val::Text(pieces.iter().flat_map(|&p| PIECES[p].1.bytes()).collect()),
        Expr::Name(name) => names.iter().find(|(n, _)| n == name).unwrap().1.clone(),
        Expr::Call { callee, args } => {
            let mut values = vec![Val::Text(Vec::new()); args.len()];
            for arg in args {
                values[arg.param] = eval(m, names, &arg.value, out);
            }
            match callee {
                None => {
                    let Val::Text(text) = &values[0] else {
                        unreachable!("`print` takes text")
                    };
                    out.extend_from_slice(text);
                    out.push(b'\n');
                    Val::Text(Vec::new())
                }
                Some(f) => run(m, *f, values, out),
            }
        }
        Expr::Construct { s, fields } => {
            let mut values = vec![Val::Text(Vec::new()); m.structs[*s].fields.len()];
            for (field, value) in fields {
                values[*field] = eval(m, names, value, out);
            }
            Val::Struct(values)
        }
        Expr::Field { of, field, .. } => match eval(m, names, of, out) {
            Val::Struct(mut values) => values.swap_remove(*field),
            Val::Text(_) => unreachable!("a field is read from a struct"),
        },
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
    /// The same, for the call that gives text or nothing that the mistake
    /// goes at.
    plain_site: usize,
    plain_calls: usize,
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
    ("derive Eq for A", NOT_BUILT),
    ("use std.console (input)", NOT_BUILT),
    ("enum E {\n  a\n}", NOT_BUILT),
    ("pub fn f() {}", NOT_BUILT),
    (")", "definition"),
];

/// Definitions that `main` does not reach, each with a mistake that would
/// be reported if it were checked.
pub const UNREACHED: &[&str] = &[
    "fn unused() { print(\"a\") }",
    "fn unused() -> str { nope() }",
    "fn unused() ! console { unused() }",
    "fn unused(a: i32) {}",
    "fn unused() ! console {\n  x = \"a\"\n  x = \"b\"\n  print(x, \"c\")\n}",
    "fn unused() -> str {}",
    "fn unused(n: Nope) -> str { n.x }",
    "struct Unused {\n  a: Nope\n}",
    "struct Unused {\n  next: Unused\n}",
];

/// A definition of its own, before or after the program's, that holds the
/// mistake. Each is written as the text before the mistake, and the rest.
const STRUCTS_NOT_BUILT: &[(&str, &str)] = &[
    ("struct Spare {\n  a: str ", "= \"a\"\n}"),
    ("struct Spare", "[T] {}"),
    ("struct Spare {\n  ", "pub a: str\n}"),
];

/// The same, for a struct that `main` reaches and that contains itself.
const CYCLES: &[(&str, &str)] = &[
    ("struct Loop {\n  next: ", "Loop\n}"),
    (
        "struct Loop {\n  a: str\n  next: Other\n}\n\nstruct Other {\n  back: ",
        "Loop\n}",
    ),
];

/// A definition of the program: a function or a struct, by its place.
enum Item {
    Fn(usize),
    Struct(usize),
}

fn build(g: &mut Gen, kind: Option<Kind>) -> Case {
    let mut m = model(
        g,
        kind.is_some_and(Kind::needs_parameters),
        kind.is_some_and(Kind::needs_struct),
    );
    let mut stdout = Vec::new();
    run(&m, 0, Vec::new(), &mut stdout);
    if kind.is_some_and(Kind::ends_main)
        || matches!(kind, Some(Kind::EffectTwice | Kind::NoConsole))
    {
        m.fns[0].lists_console = true;
    }
    if kind == Some(Kind::NoConsole) && !m.fns.iter().any(|f| f.console) {
        m.fns[0].body.push(print_of(Expr::Text(Vec::new())));
        m.fns[0].console = true;
    }
    // The value of a struct the statement with the mistake is built
    // around: a construction, or for some mistakes any value.
    let around = kind.filter(|k| k.needs_struct()).map(|k| {
        let with_fields: Vec<usize> = (0..m.structs.len())
            .filter(|&s| !m.structs[s].fields.is_empty())
            .collect();
        let s = *g.pick(&with_fields);
        if matches!(k, Kind::WrongType | Kind::NoSuchFieldRead) {
            value(g, &m, 0, &[], Type::Struct(s), DEPTH)
        } else {
            construct(g, &m, 0, &[], s, DEPTH)
        }
    });
    let order = {
        let mut rest: Vec<usize> = (0..m.fns.len()).collect();
        let mut order = Vec::new();
        while !rest.is_empty() {
            order.push(rest.remove(g.below(rest.len())));
        }
        order
    };
    // Each struct goes before the function at a place in that order, or
    // after them all.
    let mut items = Vec::new();
    let struct_at: Vec<usize> = (0..m.structs.len())
        .map(|_| g.below(order.len() + 1))
        .collect();
    for at in 0..=order.len() {
        for (s, _) in struct_at.iter().enumerate().filter(|(_, a)| **a == at) {
            items.push(Item::Struct(s));
        }
        if let Some(&f) = order.get(at) {
            items.push(Item::Fn(f));
        }
    }
    let exprs = || m.fns.iter().flat_map(|f| &f.body).map(stmt_expr);
    let count_literals: usize = exprs().map(literals_in).sum();
    let count_calls: usize = exprs().map(calls_in).sum();
    let count_plain: usize = exprs().map(|e| plain_calls_in(&m, e)).sum();
    let fn_site = match kind {
        Some(Kind::NoConsole) => {
            let needing: Vec<usize> = (0..m.fns.len()).filter(|&f| m.fns[f].console).collect();
            *g.pick(&needing)
        }
        Some(Kind::NoClosingBrace) => order[order.len() - 1],
        _ => order[g.below(order.len())],
    };
    let mut b = Builder {
        literal_site: g.below(count_literals.max(1)),
        call_site: g.below(count_calls.max(1)),
        plain_site: g.below(count_plain.max(1)),
        g,
        kind,
        text: String::new(),
        mistake: None,
        literals: 0,
        calls: 0,
        plain_calls: 0,
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
    let own_item_after = b.g.one_in(2);
    if kind.is_some_and(Kind::own_item) && !own_item_after {
        own_item(&mut b);
        b.pick(&["\n", "\n\n"]);
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
    for (at, item) in items.iter().enumerate() {
        if at > 0 {
            b.pick(&["\n", "\n\n", "\n// next\n"]);
        }
        match *item {
            Item::Fn(f) => {
                let closed = function(&mut b, &m, f, f == fn_site, around.as_ref());
                if !closed {
                    return finish(b, stdout, m.fns.len());
                }
            }
            Item::Struct(s) => structure(&mut b, &m, s),
        }
    }
    let own_after = kind.is_some_and(Kind::own_item) && own_item_after;
    if b.g.below(3) > 0 || b.is(Kind::ItemAfter) || own_after {
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
    if own_after {
        own_item(&mut b);
        b.push("\n");
    }
    finish(b, stdout, m.fns.len())
}

/// Writes the definition of its own that holds the mistake.
fn own_item(b: &mut Builder) {
    let (mentions, choices) = match b.kind {
        Some(Kind::StructNotBuilt) => (NOT_BUILT, STRUCTS_NOT_BUILT),
        Some(Kind::ContainsItself) => (NOT_BUILT, CYCLES),
        _ => unreachable!("not a mistake in a definition of its own"),
    };
    let (before, rest) = *b.g.pick(choices);
    b.push(before);
    b.mark(mentions, None);
    b.push(rest);
}

/// Writes a struct's definition.
fn structure(b: &mut Builder, m: &Model, s: usize) {
    let def = &m.structs[s];
    b.push("struct ");
    b.push(def.name);
    b.pick(&[" ", "  "]);
    b.push("{");
    if def.fields.len() == 1 && b.g.one_in(3) {
        let (name, ty) = def.fields[0];
        b.push(" ");
        b.push(name);
        b.pick(&[": ", ":"]);
        b.push(m.type_name(ty));
        b.push(" }");
        return;
    }
    for &(name, ty) in &def.fields {
        b.pick(&["\n  ", "\n\t", "\n\n  ", "\n  // a field\n  "]);
        b.push(name);
        b.pick(&[": ", ":"]);
        b.push(m.type_name(ty));
    }
    if def.fields.is_empty() {
        b.pick(&["", "\n"]);
    } else {
        b.push("\n");
    }
    b.push("}");
}

/// A function's signature as the compiler's advice writes it, listing
/// `console`.
fn signature_with_console(m: &Model, f: &Func) -> String {
    let params: Vec<String> = (f.params.iter())
        .map(|(p, ty)| format!("{p}: {}", m.type_name(*ty)))
        .collect();
    let result = match f.result {
        Some(ty) => format!(" -> {}", m.type_name(ty)),
        None => String::new(),
    };
    format!("fn {}({}){result} ! console", f.name, params.join(", "))
}

/// Writes function `f`, which holds the mistake if `site`. Gives whether
/// its body is closed. `around` is the value a statement with the mistake
/// at the end of `main` is built around, if it needs one.
fn function(b: &mut Builder, m: &Model, f: usize, site: bool, around: Option<&Expr>) -> bool {
    let func = &m.fns[f];
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
    for (i, (p, ty)) in func.params.iter().enumerate() {
        if i > 0 {
            b.pick(&[", ", ",", ",\n  "]);
        }
        b.push(p);
        b.pick(&[": ", ":"]);
        if f == 1 && i == 0 && b.is(Kind::TypeNotBuilt) {
            b.mark(NOT_BUILT, None);
            b.push("i32");
        } else {
            b.push(m.type_name(*ty));
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
    if let Some(ty) = func.result {
        b.pick(&[" -> ", "->", " ->  "]);
        b.push(m.type_name(ty));
    }
    effects(b, func, here(Kind::NoConsole), f == 0);
    if here(Kind::Contract) {
        b.pick(&[" ", "\n"]);
        b.mark(NOT_BUILT, None);
        b.push("requires true");
    }
    b.pick(&[" ", "\n", "  ", "\n// the body\n"]);
    b.push("{");

    let console_site = here(Kind::NoConsole).then(|| signature_with_console(m, func));
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
            Stmt::Define {
                name,
                ty,
                typed,
                value,
            } => {
                b.push(name);
                if *typed {
                    b.pick(&[": ", ":"]);
                    b.push(m.type_name(*ty));
                    b.push(" = ");
                } else {
                    b.pick(&[" = ", "=", "  =  "]);
                }
                expr(b, m, value, &console_site);
                names.push(name.clone());
            }
            Stmt::Expr(value) => expr(b, m, value, &console_site),
        }
    }
    for kind in extra {
        b.pick(&["\n  ", "\n\n  ", "\n  // more\n  "]);
        main_statement(b, m, kind, &names, around);
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

/// The struct a value of a struct is of.
fn struct_of(m: &Model, value: &Expr) -> usize {
    match value {
        Expr::Construct { s, .. } => *s,
        Expr::Field { s, field, .. } => match m.structs[*s].fields[*field].1 {
            Type::Struct(t) => t,
            Type::Str => unreachable!("a value of a struct"),
        },
        Expr::Call {
            callee: Some(f), ..
        } => match m.fns[*f].result {
            Some(Type::Struct(t)) => t,
            _ => unreachable!("a value of a struct"),
        },
        _ => unreachable!("a value of a struct has no names to use"),
    }
}

/// A statement at the end of `main` that holds the mistake. `around` is
/// the value of a struct it is built around, if it needs one.
fn main_statement(b: &mut Builder, m: &Model, kind: Kind, names: &[String], around: Option<&Expr>) {
    let no_console = None;
    match kind {
        Kind::StatementNotBuilt => {
            b.mark(NOT_BUILT, None);
            b.pick(&[
                "var x = \"a\"",
                "x += \"a\"",
                "with a",
                "break",
                "x.y = \"a\"",
            ]);
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
        Kind::UnknownType => {
            let name = *b.g.pick(&["Nope", "Nmae", "Txt", "Strr"]);
            b.push("wrong: ");
            b.mark(format!("`{name}`"), None);
            b.push(name);
            b.push(" = \"a\"");
        }
        Kind::ContainsItself => b.push("wrong: Loop = \"a\""),
        Kind::WrongType => {
            let around = around.expect("a value of a struct");
            let name = m.structs[struct_of(m, around)].name;
            if b.g.one_in(2) {
                b.push("wrong: ");
                b.push(name);
                b.push(" = ");
                b.mark(format!("`{name}`"), None);
                b.push("\"a\"");
            } else {
                b.push("print(");
                b.mark(format!("`{name}`"), None);
                expr(b, m, around, &no_console);
                b.push(")");
            }
        }
        Kind::NoSuchFieldRead => {
            b.push("wrong = ");
            expr(b, m, around.expect("a value of a struct"), &no_console);
            b.push(".");
            b.mark("`nope`", None);
            b.push("nope");
        }
        Kind::FieldLeftOut | Kind::FieldTwice | Kind::NoSuchField | Kind::FieldByPosition => {
            let Some(Expr::Construct { s, fields }) = around else {
                unreachable!("a construction")
            };
            b.push("wrong = ");
            construction(b, m, *s, fields, Some(kind), &no_console);
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
fn expr(b: &mut Builder, m: &Model, expr: &Expr, console_site: &Option<String>) {
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
            let plain_site = plain(m, *callee) && b.plain_calls == b.plain_site;
            b.plain_calls += usize::from(plain(m, *callee));
            let needs = callee.is_none_or(|f| m.fns[f].lists_console);
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
                b.push(callee.map_or("print", |f| m.fns[f].name.as_str()));
            }
            b.push("(");
            b.pick(&["", "\n    ", "\n    // the arguments\n    "]);
            // The mistake about arguments goes in the first call of `f1`.
            let first_of_f1 = *callee == Some(1) && !b.f1_called;
            b.f1_called |= *callee == Some(1);
            let kind = b.kind.filter(|k| first_of_f1 && k.needs_parameters());
            call_args(b, m, *callee, args, kind, console_site);
            if plain_site && b.is(Kind::NotAStruct) {
                b.push(".");
                b.mark("not a struct", None);
                b.push("x");
            }
        }
        Expr::Construct { s, fields } => construction(b, m, *s, fields, None, console_site),
        Expr::Field { of, s, field } => {
            self::expr(b, m, of, console_site);
            b.push(".");
            b.push(m.structs[*s].fields[*field].0);
        }
    }
}

/// Writes the construction of struct `s`, with the mistake about its
/// fields if `kind` is one.
fn construction(
    b: &mut Builder,
    m: &Model,
    s: usize,
    fields: &[(usize, Expr)],
    kind: Option<Kind>,
    console_site: &Option<String>,
) {
    let def = &m.structs[s];
    b.push(def.name);
    b.push("(");
    if !fields.is_empty() {
        b.pick(&["", "\n    "]);
    }
    let written = match kind {
        Some(Kind::FieldLeftOut) => &fields[..fields.len() - 1],
        _ => fields,
    };
    for (i, (field, value)) in written.iter().enumerate() {
        if i > 0 {
            b.pick(&[", ", ",", ",\n    "]);
        }
        let name = def.fields[*field].0;
        if i == 0 && kind == Some(Kind::FieldByPosition) {
            let fix = format!("`{}: `", def.fields[0].0);
            b.mark("by name", Some(fix));
        } else {
            b.push(name);
            b.pick(&[": ", ":"]);
        }
        expr(b, m, value, console_site);
    }
    // One more field, by name.
    let more = |b: &mut Builder, name: &str, mentions: String| {
        if !written.is_empty() {
            b.push(", ");
        }
        b.mark(mentions, None);
        b.push(name);
        b.push(": \"a\"");
    };
    match kind {
        Some(Kind::FieldTwice) => {
            let name = def.fields[written[0].0].0;
            more(b, name, "twice".to_string());
        }
        Some(Kind::NoSuchField) => more(b, "nope", "`nope`".to_string()),
        _ => {}
    }
    if !written.is_empty() {
        b.pick(&["", ","]);
    }
    if kind == Some(Kind::FieldLeftOut) {
        let (field, _) = fields[fields.len() - 1];
        b.mark(format!("`{}`", def.fields[field].0), None);
    }
    b.push(")");
}

/// Writes the arguments of a call and its `)`, with the mistake about
/// arguments if `kind` is one.
fn call_args(
    b: &mut Builder,
    m: &Model,
    callee: Option<usize>,
    args: &[Arg],
    kind: Option<Kind>,
    console_site: &Option<String>,
) {
    let params: Vec<&str> = match callee {
        None => vec!["text"],
        Some(f) => m.fns[f].params.iter().map(|(p, _)| *p).collect(),
    };
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
        expr(b, m, &arg.value, console_site);
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

/// Two programs that differ only in how a second name gets its struct: in
/// the first it is bound to the struct a first name holds, directly or
/// through a function that gives back what it is given, and in the second
/// to a new struct built from the same fields. The rest of each program
/// reads fields through both names.
pub fn shared_and_copied(g: &mut Gen) -> (String, String) {
    let count = 1 + g.below(FIELDS.len());
    let fields = &FIELDS[..count];
    let mut program = String::from("struct Name {\n");
    for field in fields {
        program.push_str(&format!("  {field}: str\n"));
    }
    program.push_str("}\n\nfn same(n: Name) -> Name { n }\n\nfn main() ! console {\n");
    let mut order: Vec<usize> = (0..count).collect();
    let mut given = Vec::new();
    while !order.is_empty() {
        let field = order.remove(g.below(order.len()));
        let pieces: String = (0..g.below(4)).map(|_| g.pick(PIECES).0).collect();
        given.push(format!("{}: \"{pieces}\"", fields[field]));
    }
    let construction = format!("Name({})", given.join(", "));
    program.push_str(&format!("  a = {construction}\n"));
    let (shared, copied) = if g.one_in(2) {
        ("same(a)".to_string(), format!("same({construction})"))
    } else {
        ("a".to_string(), construction)
    };
    let mut rest = String::new();
    for _ in 0..1 + g.below(6) {
        let name = *g.pick(&["a", "b", "same(a)", "same(b)"]);
        rest.push_str(&format!("  print({name}.{})\n", g.pick(fields)));
    }
    rest.push_str("}\n");
    (
        format!("{program}  b = {shared}\n{rest}"),
        format!("{program}  b = {copied}\n{rest}"),
    )
}
