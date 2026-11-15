//! The generator of programs from the part of the grammar the compiler
//! accepts, which can also place one mistake in what it builds. It records
//! what it built: what the program prints, or where its mistake is and what
//! the error must mention.

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
    pub fix: Option<&'static str>,
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
    // In the tokens.
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
    Parameter,
    ReturnType,
    Contract,
    EmptyBody,
    ArgumentNotALiteral,
    NamedArgument,
    Operator,
    NoClosingParenthesis,
    Field,
    TokenAfterStatement,
    StatementAfterTheFirst,
    NoClosingBrace,
    TokenAfterBody,
    // For the checker.
    NoMain,
    NoConsole,
    EffectNotBuilt,
    UnknownEffect,
    EffectTwice,
    UnknownFunction,
    CallOfMain,
    NoArgument,
    SecondArgument,
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
    Kind::Parameter,
    Kind::ReturnType,
    Kind::Contract,
    Kind::EmptyBody,
    Kind::ArgumentNotALiteral,
    Kind::NamedArgument,
    Kind::Operator,
    Kind::NoClosingParenthesis,
    Kind::Field,
    Kind::TokenAfterStatement,
    Kind::StatementAfterTheFirst,
    Kind::NoClosingBrace,
    Kind::TokenAfterBody,
    Kind::NoMain,
    Kind::NoConsole,
    Kind::EffectNotBuilt,
    Kind::UnknownEffect,
    Kind::EffectTwice,
    Kind::UnknownFunction,
    Kind::CallOfMain,
    Kind::NoArgument,
    Kind::SecondArgument,
];

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

/// The source being built, and the mistake once it is placed.
struct Builder {
    text: String,
    mistake: Option<Expected>,
}

impl Builder {
    fn push(&mut self, text: &str) {
        self.text.push_str(text);
    }

    /// Records the mistake as being where the next text goes.
    fn mark(&mut self, mentions: impl Into<String>, fix: Option<&'static str>) {
        assert!(self.mistake.is_none(), "one mistake per file");
        self.mistake = Some(Expected {
            at: pos_after(&self.text),
            mentions: mentions.into(),
            fix,
        });
    }
}

fn build(g: &mut Gen, kind: Option<Kind>) -> Case {
    let is = |k: Kind| kind == Some(k);
    let mut b = Builder {
        text: String::new(),
        mistake: None,
    };

    for _ in 0..g.below(3) {
        b.push(g.pick(&["\n", "// a comment\n", "  // indented\n", "//\n"]));
    }
    if is(Kind::ItemBefore) {
        let (item, mentions) = *g.pick(&[
            ("struct A { x: f32 }", NOT_BUILT),
            ("use std.console (input)", NOT_BUILT),
            ("enum E {\n  a\n}", NOT_BUILT),
            ("pub fn f() {}", NOT_BUILT),
            (")", "definition"),
        ]);
        b.mark(mentions, None);
        b.push(item);
        b.push("\n");
    }

    // The signature.
    if is(Kind::NoMain) {
        b.mistake = Some(Expected {
            at: pos_after(""),
            mentions: "`main`".to_string(),
            fix: None,
        });
    }
    b.push("fn");
    b.push(g.pick(&[" ", "  ", "\t"]));
    if is(Kind::NoName) {
        b.mark("name", None);
    } else {
        b.push(if is(Kind::NoMain) { "hello" } else { "main" });
    }
    if is(Kind::GenericParameter) {
        b.mark(NOT_BUILT, None);
        b.push("[T]");
    }
    b.push("(");
    b.push(g.pick(&["", " ", "\n", "\n  // no parameters\n"]));
    if is(Kind::Parameter) {
        b.mark(NOT_BUILT, None);
        b.push("x: i32");
    }
    b.push(")");
    if is(Kind::ReturnType) {
        b.push(" ");
        b.mark(NOT_BUILT, None);
        b.push("-> i32");
    }
    effects(g, &mut b, kind);
    if is(Kind::Contract) {
        b.push(g.pick(&[" ", "\n"]));
        b.mark(NOT_BUILT, None);
        b.push("requires true");
    }
    b.push(g.pick(&[" ", "\n", "  ", "\n// the body\n"]));
    b.push("{");

    // The body.
    if is(Kind::EmptyBody) {
        b.push(g.pick(&["", "\n"]));
        b.mark(NOT_BUILT, None);
        b.push("}\n");
        return finish(g, b, Vec::new(), kind);
    }
    b.push(g.pick(&[" ", "\n  ", "\n\t", "\n\n  ", "\n  // say it\n  "]));
    if is(Kind::UnexpectedCharacter) {
        b.mark("unexpected character", None);
        b.push(g.pick(&["#", "$", "?", "`", ";", "%", "é", "日"]));
    }
    let stdout = call(g, &mut b, kind);
    if is(Kind::NoClosingParenthesis) {
        b.push(" ");
        b.mark("expected", None);
        b.push("}\n");
        return finish(g, b, stdout, kind);
    }
    if is(Kind::Field) {
        b.mark(NOT_BUILT, None);
        b.push(".x");
    }
    if is(Kind::TokenAfterStatement) {
        b.push(" ");
        b.mark("line break", None);
        b.push("x");
    }
    if is(Kind::StatementAfterTheFirst) {
        b.push(g.pick(&["\n  ", "\n\n  ", "\n  // more\n  "]));
        b.mark(NOT_BUILT, None);
        b.push(g.pick(&[
            "print(\"b\")",
            "x = 1",
            "var x = 1",
            "x := 2",
            "break",
            "with a",
        ]));
    }
    b.push(g.pick(&[" ", "\n"]));
    if is(Kind::NoClosingBrace) {
        b.mark("`}`", None);
        return finish(g, b, stdout, kind);
    }
    b.push("}");
    if is(Kind::TokenAfterBody) {
        b.push(" ");
        b.mark("line break", None);
        b.push("x");
    }
    if is(Kind::CommentAfterToken) {
        b.push(" ");
        b.mark("own line", None);
        b.push("// done");
    }
    if g.below(3) > 0 || is(Kind::ItemAfter) {
        b.push("\n");
        for _ in 0..g.below(3) {
            b.push(g.pick(&["\n", "// the end\n"]));
        }
    }
    if is(Kind::ItemAfter) {
        let (item, mentions) = *g.pick(&[
            ("struct A { x: f32 }", NOT_BUILT),
            ("fn f() ! console { print(\"a\") }", NOT_BUILT),
            ("fn g()\n{\n  x = 1\n}", NOT_BUILT),
            ("use std.console (input)", NOT_BUILT),
            (")", "definition"),
        ]);
        b.mark(mentions, None);
        b.push(item);
        b.push("\n");
    }
    finish(g, b, stdout, kind)
}

/// The `!` and the list of effects, which is `console` unless the mistake
/// is in the list.
fn effects(g: &mut Gen, b: &mut Builder, kind: Option<Kind>) {
    // Each effect, with what its error must mention if it is the mistake.
    let mut list: Vec<(&str, Option<String>)> = Vec::new();
    if kind != Some(Kind::NoConsole) {
        list.push(("console", None));
    }
    let extra = match kind {
        Some(Kind::EffectNotBuilt) => {
            let name = *g.pick(&[
                "fs", "net", "time", "ui", "alloc", "spawn", "gpu", "diverge", "exec",
            ]);
            Some((name, NOT_BUILT.to_string()))
        }
        Some(Kind::UnknownEffect) => {
            let name = *g.pick(&["consol", "io", "stdout", "Console"]);
            Some((name, format!("`{name}`")))
        }
        _ => None,
    };
    if let Some((name, mentions)) = extra {
        let at = g.below(2).min(list.len());
        list.insert(at, (name, Some(mentions)));
    }
    if kind == Some(Kind::EffectTwice) {
        // Whichever is written second is the mistake.
        let at = g.below(2);
        list.insert(at, ("console", None));
        list[1].1 = Some("already listed".to_string());
    }
    if list.is_empty() {
        return;
    }
    b.push(g.pick(&[" ", "", "  "]));
    b.push("!");
    b.push(g.pick(&[" ", "", "\t"]));
    for (i, (name, mistake)) in list.into_iter().enumerate() {
        if i > 0 {
            b.push(",");
            b.push(g.pick(&[" ", "", "\n  ", "\n// and\n"]));
        }
        if let Some(mentions) = mistake {
            b.mark(mentions, None);
        }
        b.push(name);
    }
}

/// The call to `print`, and what it prints.
fn call(g: &mut Gen, b: &mut Builder, kind: Option<Kind>) -> Vec<u8> {
    let is = |k: Kind| kind == Some(k);
    if is(Kind::NoConsole) {
        b.mark("`console`", Some("`fn main() ! console`"));
    }
    if is(Kind::UnknownFunction) {
        let name = *g.pick(&["prin", "println", "write", "Print"]);
        b.mark(format!("`{name}`"), None);
        b.push(name);
    } else if is(Kind::CallOfMain) {
        b.mark(NOT_BUILT, None);
        b.push("main");
    } else {
        b.push("print");
    }
    b.push("(");
    b.push(g.pick(&["", "\n    ", "\n    // what to print\n    "]));
    if is(Kind::NoArgument) {
        b.mark("needs the text", Some("`print(\"hello\")`"));
        b.push(")");
        return Vec::new();
    }
    if is(Kind::ArgumentNotALiteral) {
        b.mark(NOT_BUILT, None);
        b.push(g.pick(&["1", "x", "(\"a\")", "-1", "[\"a\"]"]));
        b.push(")");
        return Vec::new();
    }
    if is(Kind::NamedArgument) {
        b.mark(NOT_BUILT, None);
        b.push("text: ");
    }
    let stdout = literal(g, b, kind);
    if is(Kind::NoClosingQuote) || is(Kind::BackslashAtLineEnd) {
        b.push("\n)");
        return stdout;
    }
    if is(Kind::Operator) {
        b.push(" ");
        b.mark(NOT_BUILT, None);
        b.push(g.pick(&["+ \"b\"", "== \"b\"", "and x", "..\"b\""]));
    }
    if is(Kind::SecondArgument) {
        b.push(", ");
        b.mark(NOT_BUILT, None);
        b.push(g.pick(&["\"b\"", "end: \"\"", "to: stderr"]));
    }
    if is(Kind::NoClosingParenthesis) {
        return stdout;
    }
    b.push(g.pick(&["", ",", "\n  ", ",\n  "]));
    b.push(")");
    stdout
}

/// A string literal, and the line `print` writes for it. A mistake in the
/// literal goes between two of its pieces.
fn literal(g: &mut Gen, b: &mut Builder, kind: Option<Kind>) -> Vec<u8> {
    let count = g.below(10);
    let bad_at = g.below(count + 1);
    let opening = pos_after(&b.text);
    b.push("\"");
    let mut text = Vec::new();
    for i in 0..=count {
        if i == bad_at {
            match kind {
                Some(Kind::UnknownEscape) => {
                    b.mark("unknown escape", None);
                    b.push("\\");
                    b.push(g.pick(&["q", "x", "0", " ", "u", "é", "'", "r"]));
                }
                Some(Kind::BraceInString) => {
                    b.mark("closes no expression", Some("`\\}`"));
                    b.push("}");
                }
                Some(Kind::ExpressionInString) => {
                    b.mark(NOT_BUILT, None);
                    b.push(g.pick(&["{x}", "{name}", "{1 + 2}", "{f(\"a\")}"]));
                }
                _ => {}
            }
        }
        if i == count {
            break;
        }
        let (written, means, _) = *g.pick(PIECES);
        b.push(written);
        text.extend_from_slice(means.as_bytes());
    }
    match kind {
        Some(Kind::NoClosingQuote) => {
            b.mistake = Some(Expected {
                at: opening,
                mentions: "no closing quote".to_string(),
                fix: None,
            });
        }
        Some(Kind::BackslashAtLineEnd) => {
            b.mark("unknown escape", None);
            b.push("\\");
        }
        _ => b.push("\""),
    }
    text.push(b'\n');
    text
}

/// The file, with a mistake in its text placed at any character, if that
/// is the kind.
fn finish(g: &mut Gen, b: Builder, stdout: Vec<u8>, kind: Option<Kind>) -> Case {
    let mut source = b.text.clone().into_bytes();
    let mut mistake = b.mistake;
    let inserted: Option<(&[u8], &str, Option<&'static str>)> = match kind {
        Some(Kind::CarriageReturn) => Some((b"\r", "carriage return", Some("line feed"))),
        Some(Kind::InvalidUtf8) => Some((b"\xff", "UTF-8", None)),
        _ => None,
    };
    if let Some((bytes, mentions, fix)) = inserted {
        let boundaries: Vec<usize> = (0..=b.text.len())
            .filter(|i| b.text.is_char_boundary(*i))
            .collect();
        let at = *g.pick(&boundaries);
        mistake = Some(Expected {
            at: pos_after(&b.text[..at]),
            mentions: mentions.to_string(),
            fix,
        });
        source.splice(at..at, bytes.iter().copied());
    }
    Case {
        source,
        stdout,
        mistake,
    }
}
