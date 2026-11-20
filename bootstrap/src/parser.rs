//! Builds the syntax tree of a file from its tokens, following the grammar
//! in `knowledge/grammar.md`. It accepts the part of the grammar the
//! compiler builds so far, and reports anything else the grammar allows as
//! not supported yet.

use std::collections::HashSet;
use std::sync::Arc;

use crate::diag::{Error, Pos};
use crate::lexer::{Piece, Tok, Token};

mod effects_with_and_try;
mod functions_calls_and_lambdas;
mod lists_and_text;
mod numbers_and_operators;
pub mod outline;
mod traits;

use effects_with_and_try::{Block, Line, split};
pub use effects_with_and_try::{EffectDef, is_operation};
pub use lists_and_text::Part;
pub use numbers_and_operators::ArithOp;
use numbers_and_operators::{compared, number};
pub use traits::{Impl, Trait, Without};

/// The definitions of a file, each kind in the order they are written.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct File {
    /// The module each number of a place says, by that number, as the map of an
    /// executable names them: the module of the standard library, the file of a
    /// project's module with its path as a module's name, and the file that is
    /// built, which is number 0. Empty until the checker reads the modules.
    pub modules: Vec<(usize, String)>,
    // Each item is shared, so that the prelude's, which is parsed once in a
    // process, is not copied into each file that is checked.
    pub functions: Vec<Arc<Function>>,
    pub structs: Vec<Arc<Struct>>,
    pub enums: Vec<Arc<Enum>>,
    pub traits: Vec<Arc<Trait>>,
    pub effects: Vec<EffectDef>,
    pub impls: Vec<Arc<Impl>>,
    pub withouts: Vec<Without>,
    /// The `use` lines, which come before every other item.
    pub uses: Vec<Use>,
    /// The names of the items written with `pub`, in a file of the library.
    pub exports: Vec<String>,
    /// Every name written after the `use` lines, when there are any.
    pub written: HashSet<String>,
    /// Each name the file writes where it may mean a name of another module:
    /// not after a `.`, not where an item is named, and not before a `:`.
    /// Where the file binds the name itself is for the checker to find.
    pub idents: Vec<(String, Pos)>,
    /// Every name written with the name of an imported module before it, as
    /// in `transaction.begin(db)`.
    pub qualified: Vec<Qualified>,
    /// For the modules of a program, the traits each can name, by the number
    /// of its place and the names the traits have among the file's items. The
    /// checker reads it to find a call that two of them could mean.
    pub scopes: Vec<(usize, Vec<String>)>,
    /// What the file uses that is not built yet, which the parser read past
    /// by putting an `Expr::Unbuilt` or an unbuilt `Type` in its place, so
    /// that the rest of the file is checked.
    pub unbuilt: Vec<Error>,
}

/// A name written with the name of an imported module before it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Qualified {
    /// The module, as its `use` names it.
    pub module: String,
    /// The name the module is written as, and where it is.
    pub written: (String, Pos),
    /// The name of the module's, and where it is.
    pub name: (String, Pos),
}

/// A `use` line: the module's name, one part at a time, and the names listed
/// from it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Use {
    /// Where `use` is.
    pub pos: Pos,
    pub module: Vec<(String, Pos)>,
    /// The name the module is imported as, when it is imported itself, and
    /// where `as` gives another.
    pub alias: Option<(String, Pos)>,
    /// Whether the module's names are listed. A `use` with no list brings in
    /// the module itself.
    pub listed: bool,
    pub names: Vec<Listed>,
}

/// A name a `use` lists, and the name `as` gives it in the file.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Listed {
    pub name: String,
    pub pos: Pos,
    pub alias: Option<(String, Pos)>,
}

impl Use {
    /// The name that stands for the module in the file that imports it: its
    /// last segment, or the name `as` gives.
    pub fn called(&self) -> (&str, Pos) {
        match &self.alias {
            Some((name, pos)) => (name, *pos),
            None => {
                let (name, pos) = self.module.last().expect("a module has a name");
                (name, *pos)
            }
        }
    }
}

/// A type parameter, as in `[T: Named]` or `[A, out B]`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Generic {
    pub name: String,
    pub pos: Pos,
    /// Where `out` is written, if it is.
    pub out: Option<Pos>,
    /// The trait after `:`, if there is one.
    pub bound: Option<Type>,
    /// The type after `=`, which a use that leaves the parameter out gets.
    pub default: Option<Type>,
}

/// A struct definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Struct {
    pub name: String,
    pub pos: Pos,
    pub generics: Vec<Generic>,
    pub fields: Vec<Field>,
}

/// An enum definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Enum {
    pub name: String,
    pub pos: Pos,
    pub generics: Vec<Generic>,
    pub cases: Vec<CaseDef>,
}

/// A case of an enum, and the type it holds, if it holds one.
#[derive(Debug, PartialEq, Eq)]
pub struct CaseDef {
    pub name: String,
    pub pos: Pos,
    pub ty: Option<Type>,
}

/// A field of a struct, its type, and its default, if it has one.
#[derive(Debug, PartialEq, Eq)]
pub struct Field {
    pub name: String,
    pub pos: Pos,
    pub ty: Type,
    pub default: Option<Expr>,
    /// Where `pub` is, when the field is written with it.
    pub public: Option<Pos>,
}

/// A function definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Function {
    pub name: String,
    pub pos: Pos,
    pub generics: Vec<Generic>,
    pub params: Vec<Param>,
    /// The type after `->`, if there is one.
    pub result: Option<Type>,
    /// The name the signature gives the result, if it gives one.
    pub result_name: Option<(String, Pos)>,
    pub effects: Effects,
    /// What the caller owes, one expression for each `requires`.
    pub requires: Vec<Expr>,
    /// The `requires` that name a trait, as `requires Pair[A, B]`, which the
    /// parser leaves among `requires` and the checker moves here.
    pub needs: Vec<Expr>,
    /// What the function gives, one expression for each `ensures`.
    pub ensures: Vec<Expr>,
    /// The measure that proves the function ends, one expression for each of
    /// its parts, as `decreases a, b` writes them. It is empty when the function
    /// has none.
    pub decreases: Vec<Expr>,
    pub body: Vec<Stmt>,
    /// Where the `}` that closes the body is.
    pub end: Pos,
}

/// The effects after `!`, each with where it is written.
type Effects = Vec<(String, Pos)>;

/// A function's name and where it is, its type parameters, its
/// parameters, its result with the name it is given, and its effects.
type Signature = (
    String,
    Pos,
    Vec<Generic>,
    Vec<Param>,
    Option<Type>,
    Option<(String, Pos)>,
    Effects,
);

/// A function's signature and its contracts: what its `requires` lines say,
/// then what its `ensures` lines say.
/// A place where a name is not a bound of a range, other than inside another type.
#[derive(Clone, Copy, Debug)]
enum NoNames {
    Field,
    Var,
}

type BodySignature = (
    String,
    Pos,
    Vec<Generic>,
    Vec<Param>,
    Option<Type>,
    Option<(String, Pos)>,
    Effects,
    Vec<Expr>,
    Vec<Expr>,
    Vec<Expr>,
);

/// A parameter, its type, and its default, if it has one.
#[derive(Debug, PartialEq, Eq)]
pub struct Param {
    pub name: String,
    pub pos: Pos,
    pub ty: Type,
    pub default: Option<Expr>,
    /// Whether the parameter is marked `take`.
    pub take: bool,
    /// For a parameter of a function type, whether each of the function's
    /// own parameters is marked `take`.
    pub takes: Vec<bool>,
    /// For a parameter of a function type, the effects the function carries.
    pub effects: Effects,
}

/// A type: a name, and the types in brackets after it, if any. A tuple
/// type has no name, and its elements are its `args`. A function type has
/// no name either, and its `args` are the types of its parameters followed
/// by the type it gives.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Type {
    /// The name, which is `UNBUILT` for a type that is not built yet.
    pub name: String,
    pub pos: Pos,
    pub args: Vec<Type>,
    /// Whether it is a function type, written `fn(T) -> U`.
    pub function: bool,
    /// The bounds of a range type, written `a..b`, whose name is `RANGE`: the
    /// first, and the end, which the range stops short of.
    pub range: Option<(Bound, Bound)>,
}

/// A bound of a range type: a whole number written out, the name of a
/// parameter of the function the type is written in, or the `size` of one.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Bound {
    Number(i128),
    Name(String),
    /// `xs.size`, the size of a name.
    Size(String),
}

impl Bound {
    fn written(&self) -> String {
        match self {
            Bound::Number(n) => n.to_string(),
            Bound::Name(n) => n.clone(),
            Bound::Size(n) => format!("{n}.size"),
        }
    }

    /// The expression a bound is in a clause, with `pos` for its places.
    fn expr(&self, pos: Pos) -> Option<Expr> {
        let name = |n: &str| Expr::Name {
            name: n.to_string(),
            pos,
        };
        match self {
            Bound::Number(_) => None,
            Bound::Name(n) => Some(name(n)),
            Bound::Size(n) => Some(Expr::Field {
                of: Box::new(name(n)),
                name: "size".to_string(),
                pos,
            }),
        }
    }

    /// The name that the bound is about, if it is about one.
    fn named(&self) -> Option<&str> {
        match self {
            Bound::Number(_) => None,
            Bound::Name(n) | Bound::Size(n) => Some(n),
        }
    }
}

/// The name of a type that is not built yet, as an array type is. It is no
/// name a program can write. Its `args` are the names written inside it and
/// the type of its elements, so that what names a type parameter still does.
pub const UNBUILT: &str = "?";

/// The name of a range type, written `a..b`. It is no name a program can
/// write, and the bounds are in the type's `range`.
pub const RANGE: &str = "..";

impl Type {
    /// How the type is written in the source.
    pub fn written(&self) -> String {
        let args: Vec<String> = self.args.iter().map(Type::written).collect();
        if let Some((first, end)) = &self.range {
            format!("{}..{}", first.written(), end.written())
        } else if self.function {
            let (gives, params) = args.split_last().expect("a result");
            if self
                .args
                .last()
                .is_some_and(|g| g.name.is_empty() && !g.function && g.args.is_empty())
            {
                format!("fn({})", params.join(", "))
            } else {
                format!("fn({}) -> {gives}", params.join(", "))
            }
        } else if self.name.is_empty() {
            format!("({})", args.join(", "))
        } else if args.is_empty() {
            self.name.clone()
        } else {
            format!("{}[{}]", self.name, args.join(", "))
        }
    }

    /// Whether the type is one that is not built yet.
    pub fn unbuilt(&self) -> bool {
        self.name == UNBUILT
    }
}

/// A statement of a block.
#[derive(Debug, PartialEq, Eq)]
pub enum Stmt {
    /// `x = e` or `x: T = e`, which defines a name, or `var x = e` or
    /// `var x: T = e`, which defines a name that can change. Whether a
    /// bare `x = e` defines `x` or assigns to a `var` is for the checker to
    /// decide.
    Define {
        name: String,
        pos: Pos,
        ty: Option<Type>,
        var: bool,
        value: Expr,
    },
    /// `x.f = e`, which assigns to a field of `x`, or to a field of a field
    /// for each further name.
    Assign {
        name: String,
        pos: Pos,
        fields: Vec<Named>,
        value: Expr,
    },
    /// `(a, var b) = e`, which takes a tuple apart. Whether each bare name
    /// in it defines a name or assigns to a `var` is for the checker to
    /// decide.
    Destructure {
        pattern: Pattern,
        ty: Option<Type>,
        value: Expr,
    },
    Expr(Expr),
}

/// An expression.
#[derive(Debug, PartialEq, Eq)]
pub enum Expr {
    /// What is not built yet, whose place the parser kept so that the rest
    /// of the file is checked. It has no type, and nothing is said of it.
    Unbuilt {
        pos: Pos,
    },
    /// A string literal with its escapes applied.
    Text {
        text: Vec<u8>,
        pos: Pos,
    },
    /// A whole number literal, with a `-` before it when it is written
    /// with one. It keeps its source spelling, and its value is kept as the
    /// size of the number, which is the largest value that fits when the
    /// number is larger than any type holds.
    Number {
        text: String,
        magnitude: u128,
        negative: bool,
        pos: Pos,
    },
    /// A string literal that holds expressions, as its parts in order.
    Interp {
        pos: Pos,
        parts: Vec<Part>,
    },
    Name {
        name: String,
        pos: Pos,
    },
    /// A call of a named function or of a name that holds a function, or
    /// the construction of a struct.
    Call(Call),
    /// `e.name`, which reads a field, or names a case in full when `e` is
    /// the name of an enum.
    Field {
        of: Box<Expr>,
        name: String,
        /// Where the field's name is.
        pos: Pos,
    },
    /// A case written with a leading dot, as in `.none` or `.cat(c)`, with
    /// the value it holds, if one is written.
    Case {
        name: String,
        /// Where the dot is.
        pos: Pos,
        payload: Option<Box<Expr>>,
    },
    Match(Box<Match>),
    /// `left + right`, `left - right`, or `left * right`.
    Arith {
        op: ArithOp,
        left: Box<Expr>,
        right: Box<Expr>,
        /// Where the operator is.
        op_pos: Pos,
    },
    /// A tuple, `()` or one of two values or more.
    Tuple {
        /// Where the `(` is.
        pos: Pos,
        items: Vec<Expr>,
    },
    Lambda(Box<Lambda>),
    /// A list written out, `[]` or `[a, b, c]`.
    List {
        /// Where the `[` is.
        pos: Pos,
        items: Vec<Expr>,
    },
    /// `of[index]`.
    Index {
        of: Box<Expr>,
        index: Box<Expr>,
        /// Where the `[` is.
        pos: Pos,
    },
    /// `of[a..b]`, `of[..b]`, or `of[a..]`, the part of `of` from `a` up to
    /// `b`, where a left-out end is the start or the size.
    Slice {
        of: Box<Expr>,
        from: Option<Box<Expr>>,
        to: Option<Box<Expr>>,
        /// Where the `[` is.
        pos: Pos,
    },
    /// `loop i in a..b { ... }`, or an open-ended `loop { ... }`.
    Loop(Box<Loop>),
    /// `break`, which ends the nearest `loop` around it.
    Break {
        pos: Pos,
    },
    /// `return e`, or `return` alone, which leaves the nearest function or
    /// lambda with `e` as its value.
    Return {
        value: Option<Box<Expr>>,
        pos: Pos,
    },
}

/// A `loop`: over a range of whole numbers when it has one, and otherwise
/// open-ended, running until a `break`.
#[derive(Debug, PartialEq, Eq)]
pub struct Loop {
    /// Where `loop` is.
    pub pos: Pos,
    pub range: Option<Range>,
    /// The list a `loop v in xs` runs over.
    pub over: Option<Over>,
    /// The measure of an open-ended `loop decreases e`.
    pub decreases: Option<Expr>,
    pub body: Vec<Stmt>,
    /// Where the `}` that closes the body is.
    pub end: Pos,
}

/// The head of a `loop` over a range: `i in a..b`.
#[derive(Debug, PartialEq, Eq)]
pub struct Range {
    /// The name each number is given, and where it is.
    pub name: String,
    pub pos: Pos,
    pub from: Expr,
    pub to: Expr,
}

/// The head of a `loop` over the elements of a value: `v in xs`.
#[derive(Debug, PartialEq, Eq)]
pub struct Over {
    /// The name each element is given, and where it is.
    pub name: String,
    pub pos: Pos,
    pub value: Expr,
}

/// A lambda: a block, with `fn` and the names of its parameters before it
/// when it takes any.
#[derive(Debug, PartialEq, Eq)]
pub struct Lambda {
    /// Where it starts: at `fn`, or at the `{` when it takes no parameters.
    pub pos: Pos,
    pub params: Vec<Named>,
    /// The type written for each parameter, if it is, and the one written for
    /// the result.
    pub types: Vec<Option<Type>>,
    pub result: Option<Type>,
    pub body: Vec<Stmt>,
    /// Where the `}` that closes its body is.
    pub end: Pos,
}

/// `match e { pattern { ... } ... }`, or what is read as one: `if`,
/// `not`, `and`, and `or`, each a `match` on a `bool` with a block for
/// `.true` and one for `.false`.
#[derive(Debug, PartialEq, Eq)]
pub struct Match {
    /// Where it starts: at `match`, `if`, or `not`, or at the left side of
    /// `and` and `or`.
    pub pos: Pos,
    pub of: Expr,
    pub arms: Vec<Arm>,
    pub form: Form,
}

/// How a `match` is written.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Form {
    Match,
    /// `if c { a } else { b }`, whose blocks are those of `.true` and then
    /// `.false`. Without `else`, the block of `.false` is empty.
    If {
        otherwise: bool,
    },
    /// `not c`, whose blocks give `false` for `.true` and `true` for
    /// `.false`.
    Not,
    /// `a and b`, whose blocks give `b` for `.true` and `false` for
    /// `.false`.
    And,
    /// `a or b`, whose blocks give `true` for `.true` and `b` for `.false`.
    Or,
    /// `try e`, which matches what `Try.split(e)` gives: the block of `.go`
    /// gives the value it holds, and that of `.stop` returns the one it
    /// holds.
    Try,
}

/// A pattern of a `match` and the block that runs when it matches.
#[derive(Debug, PartialEq, Eq)]
pub struct Arm {
    pub pattern: Pattern,
    pub body: Vec<Stmt>,
    /// Where the `}` that closes the block is.
    pub end: Pos,
}

/// A pattern: a bare name, which matches anything and names it, a case
/// with a leading dot and the pattern for what it holds, if it holds one,
/// or a tuple of patterns.
#[derive(Debug, PartialEq, Eq)]
pub enum Pattern {
    Bind {
        name: String,
        /// Where the name is.
        pos: Pos,
        /// Where `var` is, if the name is marked as one that can change.
        var: Option<Pos>,
    },
    Case {
        name: String,
        /// Where the dot is.
        pos: Pos,
        inner: Option<Box<Pattern>>,
    },
    Tuple {
        /// Where the `(` is.
        pos: Pos,
        items: Vec<Pattern>,
    },
    /// A whole number or a character, which matches a value equal to it.
    Number {
        text: String,
        magnitude: u128,
        negative: bool,
        pos: Pos,
    },
    /// A text, which matches a text equal to it.
    Text { text: Vec<u8>, pos: Pos },
}

impl Pattern {
    /// Where the pattern starts.
    pub fn pos(&self) -> Pos {
        match self {
            Pattern::Bind { pos, var, .. } => var.unwrap_or(*pos),
            Pattern::Case { pos, .. }
            | Pattern::Tuple { pos, .. }
            | Pattern::Number { pos, .. }
            | Pattern::Text { pos, .. } => *pos,
        }
    }
}

/// Whether a token starts a pattern that is not a name: a tuple, a case, a
/// literal, or a list.
fn starts_pattern(tok: &Tok) -> bool {
    matches!(
        tok,
        Tok::Punct("(" | "." | "[" | "-") | Tok::Number(_) | Tok::Str(_) | Tok::Interp(_)
    )
}

/// The name that stands for a value a pattern takes apart, and where the
/// pattern is. No source text can write it, so a call cannot give it by name.
fn hidden(pattern: &Pattern) -> (String, Pos) {
    let pos = pattern.pos();
    (format!("pattern@{}:{}", pos.line, pos.col), pos)
}

/// The binding of a pattern to the name that stands for what it takes apart.
fn taken_apart(pattern: Pattern, name: &str, pos: Pos) -> Stmt {
    Stmt::Destructure {
        pattern,
        ty: None,
        value: Expr::Name {
            name: name.to_string(),
            pos,
        },
    }
}

impl Expr {
    /// Where the expression starts.
    pub fn pos(&self) -> Pos {
        match self {
            Expr::Text { pos, .. }
            | Expr::Number { pos, .. }
            | Expr::Interp { pos, .. }
            | Expr::Name { pos, .. }
            | Expr::Case { pos, .. }
            | Expr::Tuple { pos, .. }
            | Expr::List { pos, .. }
            | Expr::Unbuilt { pos }
            | Expr::Break { pos }
            | Expr::Return { pos, .. } => *pos,
            Expr::Index { of, .. } | Expr::Slice { of, .. } => of.pos(),
            Expr::Loop(l) => l.pos,
            Expr::Call(call) => call.pos,
            Expr::Field { of, .. } => of.pos(),
            Expr::Arith { left, .. } => left.pos(),
            Expr::Match(m) => m.pos,
            Expr::Lambda(l) => l.pos,
        }
    }
}

/// A call of a named function, or the construction of a struct, which is
/// written as a call. A function of a trait may be named with its trait,
/// as in `Named.name_of(x)`.
#[derive(Debug, PartialEq, Eq)]
pub struct Call {
    /// The name before the `.`, and where it is, if one is written.
    pub qualifier: Option<(String, Pos)>,
    pub callee: String,
    /// Where the call starts, which is where its qualifier is if it has
    /// one.
    pub pos: Pos,
    /// Where the callee's own name is.
    pub name_pos: Pos,
    pub args: Vec<Arg>,
    /// Where the closing parenthesis is, or where the lambda after the call
    /// starts when it has no parentheses.
    pub end: Pos,
}

/// An argument, given by position or by the name of its parameter, or a
/// lambda written after the call, which is its last argument and is for its
/// last parameter.
#[derive(Debug, PartialEq, Eq)]
pub struct Arg {
    pub name: Option<(String, Pos)>,
    pub value: Expr,
    /// Whether it is a lambda written after the call.
    pub after: bool,
}

/// Parses a file into its definitions.
///
/// Every definition and every statement is parsed on its own, so a mistake
/// in one is reported and does not hide a mistake in another. Within one
/// statement, or one function's signature, only the first mistake is
/// reported, as what follows it cannot be read with any confidence.
pub fn parse(tokens: &[Token]) -> Result<File, Vec<Error>> {
    parse_file(tokens, false)
}

/// Parses a file of the library, in which an item may be written with `pub`
/// and the names of those items are the file's exports.
pub fn parse_library(tokens: &[Token]) -> Result<File, Vec<Error>> {
    parse_file(tokens, true)
}

fn parse_file(tokens: &[Token], library: bool) -> Result<File, Vec<Error>> {
    let mut p = Parser {
        tokens,
        at: 0,
        errors: Vec::new(),
        unbuilt: Vec::new(),
        bound_names_allowed: false,
        no_names: None,
        name_bound_ok: false,
        no_lambda: false,
        binds: Vec::new(),
        holes: Vec::new(),
        modules: Vec::new(),
        qualified: Vec::new(),
    };
    let mut uses = Vec::new();
    let mut exports = Vec::new();
    let mut items = false;
    let mut after_uses = 0;
    let mut functions = Vec::new();
    let mut structs = Vec::new();
    let mut enums = Vec::new();
    let mut traits = Vec::new();
    let mut effects = Vec::new();
    let mut impls = Vec::new();
    let mut withouts = Vec::new();
    loop {
        p.skip_newlines();
        let start = p.at;
        let mut public = false;
        if library && p.tok() == &Tok::Keyword("pub") {
            public = true;
            p.bump();
        }
        let before = (functions.len(), structs.len(), enums.len(), traits.len());
        let effects_before = effects.len();
        let is_use = p.tok() == &Tok::Keyword("use");
        let late = items && is_use;
        if !matches!(p.tok(), Tok::Eof | Tok::Keyword("use")) {
            items = true;
        }
        match p.tok() {
            Tok::Eof => break,
            Tok::Keyword("use") => match p.use_line() {
                Ok(u) if late => {
                    p.errors.push(
                        Error::new(u.pos, "`use` comes before every other item of the file")
                            .with_fix("move this line to the top of the file"),
                    );
                }
                Ok(u) => {
                    if !u.listed {
                        let (name, _) = u.called();
                        let module: Vec<&str> = u.module.iter().map(|(n, _)| n.as_str()).collect();
                        p.modules.push((name.to_string(), module.join(".")));
                    }
                    uses.push(u);
                    after_uses = p.at;
                }
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("fn") => {
                if let Ok(f) = p.function(start, false) {
                    functions.extend(f);
                }
            }
            Tok::Keyword("struct") => match p.structure() {
                Ok(s) => structs.push(s),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("enum") => match p.enumeration() {
                Ok(e) => enums.push(e),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("trait") => match p.declare_trait() {
                Ok(t) => traits.push(t),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("effect") => match p.declare_effect() {
                Ok((e, operations)) => {
                    effects.push(e);
                    functions.extend(operations);
                }
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("impl") => match p.implementation() {
                Ok(i) => impls.extend(i),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("without") => match p.without() {
                Ok(w) => withouts.push(w),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            _ => {
                let error = p.item();
                p.errors.push(error);
                p.skip_item(start);
            }
        }
        if public {
            exports.extend(functions[before.0..].iter().map(|f| f.name.clone()));
            exports.extend(structs[before.1..].iter().map(|s| s.name.clone()));
            exports.extend(enums[before.2..].iter().map(|e| e.name.clone()));
            exports.extend(traits[before.3..].iter().map(|t| t.name.clone()));
            exports.extend(
                effects[effects_before..]
                    .iter()
                    .map(|e: &EffectDef| e.name.clone()),
            );
        }
    }
    if !library {
        for field in structs.iter().flat_map(|s| &s.fields) {
            if let Some(pos) = field.public {
                p.unbuilt.push(Error::unsupported(pos, "`pub`"));
            }
        }
    }
    if p.errors.is_empty() {
        Ok(File {
            functions: share(functions),
            structs: share(structs),
            enums: share(enums),
            traits: share(traits),
            effects,
            impls: share(impls),
            withouts,
            written: if uses.is_empty() {
                HashSet::new()
            } else {
                let mut written = HashSet::new();
                written_names(&tokens[after_uses..], &mut written);
                written
            },
            idents: {
                let mut idents = Vec::new();
                ident_uses(&tokens[after_uses..], None, &mut idents);
                idents
            },
            uses,
            exports,
            qualified: p.qualified,
            scopes: Vec::new(),
            unbuilt: p.unbuilt,
            modules: Vec::new(),
        })
    } else {
        // What is not built is reported with the mistakes of the syntax, as
        // the file is not read further.
        p.errors.append(&mut p.unbuilt);
        Err(p.errors)
    }
}

/// Adds every name the tokens write, those in a string's braces among them.
fn written_names(tokens: &[Token], into: &mut HashSet<String>) {
    for token in tokens {
        match &token.tok {
            Tok::Ident(name) => {
                into.insert(name.clone());
            }
            Tok::Interp(pieces) => {
                for piece in pieces {
                    if let Piece::Expr(inner) = piece {
                        written_names(inner, into);
                    }
                }
            }
            _ => {}
        }
    }
}

/// Adds each name the tokens write as a use of a name: those in a string's
/// braces among them, and not one after a `.`, after the word that names an
/// item, or before a `:`. `before` is the token before the first.
fn ident_uses(tokens: &[Token], before: Option<&Tok>, into: &mut Vec<(String, Pos)>) {
    let mut previous = before;
    for (i, token) in tokens.iter().enumerate() {
        match &token.tok {
            Tok::Ident(name) => {
                let named = matches!(
                    previous,
                    Some(
                        Tok::Punct(".")
                            | Tok::Keyword("fn" | "struct" | "enum" | "trait" | "effect")
                    )
                );
                let label = matches!(tokens.get(i + 1).map(|t| &t.tok), Some(Tok::Punct(":")));
                if !named && !label {
                    into.push((name.clone(), token.pos));
                }
            }
            Tok::Interp(pieces) => {
                for piece in pieces {
                    if let Piece::Expr(inner) = piece {
                        ident_uses(inner, None, into);
                    }
                }
            }
            _ => {}
        }
        previous = Some(&token.tok);
    }
}

/// A name and where it is.
type Named = (String, Pos);

/// The name, its place, and the fields a place names, if the expression is
/// one: a field of a name, or of a field of one.
fn place(expr: &Expr) -> Option<(String, Pos, Vec<Named>)> {
    match expr {
        Expr::Field { of, name, pos } => match &**of {
            Expr::Name {
                name: root,
                pos: at,
            } => Some((root.clone(), *at, vec![(name.clone(), *pos)])),
            of => {
                let (root, at, mut fields) = place(of)?;
                fields.push((name.clone(), *pos));
                Some((root, at, fields))
            }
        },
        _ => None,
    }
}

/// The error for a definition that gives the name `true` or `false`, which
/// are the cases of `bool` wherever they are written. The prelude's `bool`
/// is read at line 0 and defines them.
fn truth_named(name: &str, pos: Pos) -> Result<(), Error> {
    if (name == "true" || name == "false") && pos.line != 0 {
        return Err(Error::new(
            pos,
            format!("`{name}` is a case of `bool`, so nothing else can be named `{name}`"),
        ));
    }
    Ok(())
}

/// The block of a `match` on a `bool` for the case `name`.
fn arm(name: &str, pos: Pos, body: Vec<Stmt>, end: Pos) -> Arm {
    Arm {
        pattern: Pattern::Case {
            name: name.to_string(),
            pos,
            inner: None,
        },
        body,
        end,
    }
}

/// The case of `bool` named `name`, as a statement.
fn truth(name: &str, pos: Pos) -> Vec<Stmt> {
    vec![Stmt::Expr(Expr::Case {
        name: name.to_string(),
        pos,
        payload: None,
    })]
}

/// `not operand`, with `not` at `pos`.
fn negated(pos: Pos, operand: Expr) -> Expr {
    let at = operand.pos();
    Expr::Match(Box::new(Match {
        pos,
        of: operand,
        arms: vec![
            arm("true", at, truth("false", at), at),
            arm("false", at, truth("true", at), at),
        ],
        form: Form::Not,
    }))
}

/// `left and right` or `left or right`.
fn logic(form: Form, left: Expr, right: Expr) -> Expr {
    let (pos, at) = (left.pos(), right.pos());
    let right = vec![Stmt::Expr(right)];
    let arms = if form == Form::And {
        vec![
            arm("true", at, right, at),
            arm("false", at, truth("false", at), at),
        ]
    } else {
        vec![
            arm("true", at, truth("true", at), at),
            arm("false", at, right, at),
        ]
    };
    Expr::Match(Box::new(Match {
        pos,
        of: left,
        arms,
        form,
    }))
}

/// Whether a token can start an expression.
fn starts_expression(tok: &Tok) -> bool {
    matches!(
        tok,
        Tok::Ident(_)
            | Tok::Number(_)
            | Tok::Str(_)
            | Tok::Interp(_)
            | Tok::Punct("(" | "[" | "{" | "." | "-")
            | Tok::Keyword("not" | "try" | "if" | "match" | "for" | "loop" | "fn")
    )
}

/// Whether a token can continue an expression that comes before it.
fn continues_expression(tok: &Tok) -> bool {
    matches!(
        tok,
        Tok::Punct(
            "(" | "["
                | "."
                | ".."
                | "+"
                | "-"
                | "*"
                | "/"
                | "@"
                | "^"
                | "=="
                | "!="
                | "<"
                | "<="
                | ">"
                | ">="
        ) | Tok::Keyword("and" | "or")
    )
}

struct Parser<'a> {
    tokens: &'a [Token],
    at: usize,
    errors: Vec<Error>,
    /// Whether the parameters of the function being read may have a range
    /// type with a name for a bound: those of a function that has a body.
    bound_names_allowed: bool,
    /// Where a name is not a bound, which of the places that say so it is, for
    /// the error: none is a type inside another type.
    no_names: Option<NoNames>,
    /// Whether the type read next is the whole type of such a parameter, so
    /// that it may have a name for a bound.
    name_bound_ok: bool,
    /// What is not built yet and was read past, which is not a mistake of
    /// the syntax.
    unbuilt: Vec<Error>,
    /// Whether a `{` after a call opens the body of a construct, as it does
    /// after the value of a `match`, rather than a lambda. Inside
    /// parentheses and braces it opens a lambda again.
    no_lambda: bool,
    /// The bindings that take apart the parameters of the function whose
    /// signature was read last that are written as patterns, in order. They
    /// open its body.
    binds: Vec<Stmt>,
    /// Where a name `_` was read as a value, in order. A `|>` looks at the
    /// ones read since its right side began, and takes them off.
    holes: Vec<Pos>,
    /// The modules a `use` imported itself, each by the name it is written as
    /// and the name the `use` gives it.
    modules: Vec<(String, String)>,
    /// The names written with the name of one of those before them.
    qualified: Vec<Qualified>,
}

impl Parser<'_> {
    fn tok(&self) -> &Tok {
        &self.tokens[self.at].tok
    }

    fn pos(&self) -> Pos {
        self.tokens[self.at].pos
    }

    fn next_tok(&self) -> &Tok {
        let next = (self.at + 1).min(self.tokens.len() - 1);
        &self.tokens[next].tok
    }

    fn bump(&mut self) {
        if self.at + 1 < self.tokens.len() {
            self.at += 1;
        }
    }

    fn skip_newlines(&mut self) {
        while self.tok() == &Tok::Newline {
            self.bump();
        }
    }

    fn expected(&self, what: &str, found: &Tok) -> Error {
        Error::new(
            self.pos(),
            format!("expected {what}, found {}", found.describe()),
        )
    }

    fn expect_punct(&mut self, p: &'static str, what: &str) -> Result<(), Error> {
        if self.tok() == &Tok::Punct(p) {
            self.bump();
            Ok(())
        } else {
            Err(self.expected(what, self.tok()))
        }
    }

    fn ident(&mut self, what: &str) -> Result<(String, Pos), Error> {
        let pos = self.pos();
        match self.tok() {
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                Ok((name, pos))
            }
            tok => Err(self.expected(what, tok)),
        }
    }

    /// Parses a name that a definition gives, which is not `true` or
    /// `false` outside the prelude.
    fn defined(&mut self, what: &str) -> Result<(String, Pos), Error> {
        let pos = self.pos();
        if let Tok::Ident(name) = self.tok() {
            truth_named(name, pos)?;
        }
        self.ident(what)
    }

    /// Skips the tokens of a definition that holds a mistake, from its
    /// first token to the next token outside any bracket that starts a line
    /// at its first column. A brace, `requires`, or `ensures` there belongs
    /// to the definition before it, as a line break before them does not
    /// end it.
    fn skip_item(&mut self, from: usize) {
        self.at = from;
        let mut depth = 0usize;
        loop {
            match self.tok() {
                Tok::Eof => return,
                Tok::Newline if depth == 0 => {
                    self.skip_newlines();
                    let continues = matches!(
                        self.tok(),
                        Tok::Punct("{" | "}") | Tok::Keyword("requires" | "ensures")
                    );
                    if self.pos().col == 1 && !continues {
                        return;
                    }
                    continue;
                }
                Tok::Punct("(" | "[" | "{") => depth += 1,
                Tok::Punct(")" | "]" | "}") => depth = depth.saturating_sub(1),
                _ => {}
            }
            self.bump();
        }
    }

    /// Skips the tokens of a statement that holds a mistake, up to the line
    /// break or `}` that ends it. Gives whether one was found before the end
    /// of the file.
    ///
    /// A `}` with no `{` open in the statement closes the block, even inside
    /// a bracket the statement left open, so that a missing `)` is not also
    /// reported as a missing `}`.
    fn skip_statement(&mut self, from: usize) -> bool {
        self.at = from;
        let mut open: Vec<&str> = Vec::new();
        loop {
            match self.tok() {
                Tok::Eof => return false,
                Tok::Newline if open.is_empty() => return true,
                Tok::Punct("}") if !open.contains(&"{") => return true,
                Tok::Punct(p @ ("(" | "[" | "{")) => open.push(p),
                Tok::Punct(p @ (")" | "]" | "}")) => {
                    let opener = match *p {
                        ")" => "(",
                        "]" => "[",
                        _ => "{",
                    };
                    if let Some(i) = open.iter().rposition(|o| *o == opener) {
                        open.truncate(i);
                    }
                }
                _ => {}
            }
            self.bump();
        }
    }

    /// Parses a `use` line and the line break after it: the module's name,
    /// then the names taken from it in parentheses.
    fn use_line(&mut self) -> Result<Use, Error> {
        let pos = self.pos();
        self.bump();
        let mut module = vec![self.ident("the name of a module")?];
        while self.tok() == &Tok::Punct(".") {
            self.bump();
            module.push(self.ident("a name after `.`")?);
        }
        let mut alias = None;
        if self.tok() == &Tok::Keyword("as") {
            self.bump();
            alias = Some(self.ident("a name for the module")?);
        }
        let mut names = Vec::new();
        let listed = self.tok() == &Tok::Punct("(");
        if listed && alias.is_some() {
            return Err(Error::unsupported(
                self.pos(),
                "a list of names after a module's `as`",
            ));
        }
        if listed {
            self.bump();
            loop {
                self.skip_newlines();
                if self.tok() == &Tok::Punct(")") {
                    break;
                }
                let (name, at) = self.ident("a name to import or `)`")?;
                let mut alias = None;
                if self.tok() == &Tok::Keyword("as") {
                    self.bump();
                    alias = Some(self.ident("the name to give it")?);
                }
                names.push(Listed {
                    name,
                    pos: at,
                    alias,
                });
                self.skip_newlines();
                match self.tok() {
                    Tok::Punct(",") => self.bump(),
                    Tok::Punct(")") => {}
                    tok => return Err(self.expected("`,` or `)`", tok)),
                }
            }
            self.bump();
        }
        match self.tok() {
            Tok::Newline | Tok::Eof => {}
            tok => return Err(self.expected("a line break", tok)),
        }
        Ok(Use {
            pos,
            module,
            alias,
            listed,
            names,
        })
    }

    /// The error for a definition other than a function.
    fn item(&self) -> Error {
        let pos = self.pos();
        let feature = match self.tok() {
            Tok::Keyword("pub") => "`pub`",
            Tok::Keyword("effect") => "declaring an effect",
            Tok::Keyword("derive") => "`derive`",
            Tok::Keyword("foreign") => "a foreign function",
            Tok::Ident(_) => "a definition other than a function",
            tok => return self.expected("a definition", tok),
        };
        Error::unsupported(pos, feature)
    }

    /// Parses a struct and the line break after it.
    fn structure(&mut self) -> Result<Struct, Error> {
        self.bump();
        let (name, pos) = self.defined("the struct's name")?;
        let generics = self.generics()?;
        self.expect_punct("{", "`{` to open the struct's fields")?;
        let mut fields = Vec::new();
        loop {
            self.skip_newlines();
            let mut public = None;
            match self.tok() {
                Tok::Punct("}") => break,
                Tok::Keyword("pub") => {
                    public = Some(self.pos());
                    self.bump();
                }
                _ => {}
            }
            let (name, pos) = self.defined("a field or `}`")?;
            self.expect_punct(":", "`:` and the field's type")?;
            self.no_names = Some(NoNames::Field);
            let ty = self.ty();
            self.no_names = None;
            let ty = ty?;
            let mut default = None;
            if self.tok() == &Tok::Punct("=") {
                self.bump();
                default = Some(self.default()?);
            }
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            fields.push(Field {
                name,
                pos,
                ty,
                default,
                public,
            });
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(Struct {
                name,
                pos,
                generics,
                fields,
            }),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses an enum and the line break after it.
    fn enumeration(&mut self) -> Result<Enum, Error> {
        self.bump();
        let (name, pos) = self.defined("the enum's name")?;
        let generics = self.generics()?;
        self.expect_punct("{", "`{` to open the enum's cases")?;
        let mut cases = Vec::new();
        loop {
            self.skip_newlines();
            if self.tok() == &Tok::Punct("}") {
                break;
            }
            let (name, pos) = self.defined("a case or `}`")?;
            let mut ty = None;
            if self.tok() == &Tok::Punct(":") {
                self.bump();
                ty = Some(self.ty()?);
            }
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            cases.push(CaseDef { name, pos, ty });
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(Enum {
                name,
                pos,
                generics,
                cases,
            }),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses the type parameters in brackets after a name, if there are
    /// any.
    fn generics(&mut self) -> Result<Vec<Generic>, Error> {
        let mut generics = Vec::new();
        if self.tok() != &Tok::Punct("[") {
            return Ok(generics);
        }
        self.bump();
        self.skip_newlines();
        loop {
            let out = if self.tok() == &Tok::Keyword("out") {
                let at = self.pos();
                self.bump();
                Some(at)
            } else {
                None
            };
            let (name, pos) = self.defined("a type parameter")?;
            let bound = if self.tok() == &Tok::Punct(":") {
                self.bump();
                Some(self.ty()?)
            } else {
                None
            };
            let default = if self.tok() == &Tok::Punct("=") {
                self.bump();
                Some(self.ty()?)
            } else {
                None
            };
            generics.push(Generic {
                name,
                pos,
                out,
                bound,
                default,
            });
            self.skip_newlines();
            match self.tok() {
                Tok::Punct(",") => {
                    self.bump();
                    self.skip_newlines();
                    if self.tok() == &Tok::Punct("]") {
                        break;
                    }
                }
                Tok::Punct("]") => break,
                tok => return Err(self.expected("`,` or `]`", tok)),
            }
        }
        self.bump();
        Ok(generics)
    }

    /// What the caller owes for the parameters that have a range type with a
    /// name for a bound: a parameter of range `a..n` is below `n`, and one of
    /// range `m..b` is at least `m`. None when a name is not a parameter.
    fn bounds_owed(&mut self, params: &[Param]) -> Option<Vec<Expr>> {
        let mut owed = Vec::new();
        let mut named = true;
        for p in params {
            let Some((first, end)) = &p.ty.range else {
                continue;
            };
            let pos = p.ty.pos;
            let name = |n: &str| Expr::Name {
                name: n.to_string(),
                pos,
            };
            for bound in [first, end] {
                if let Some(n) = bound.named()
                    && (n == p.name || !params.iter().any(|q| q.name == n))
                {
                    self.errors.push(
                        Error::new(
                            pos,
                            format!(
                                "`{n}` is not a parameter of this function, and a bound is one"
                            ),
                        )
                        .with_fix("write the bound as a number, or take it as another parameter"),
                    );
                    named = false;
                }
            }
            if let Some(held) = first.expr(pos) {
                owed.push(compared(held, "<=", name(&p.name), pos));
            }
            if let Some(held) = end.expr(pos) {
                owed.push(compared(name(&p.name), "<", held, pos));
            }
        }
        named.then_some(owed)
    }

    /// What the function owes for a result that has a range type with a name
    /// for a bound: a result of range `a..n` is below `n`, and one of range
    /// `m..b` is at least `m`. A result that is not named is given a name
    /// for the clause to be about. None when a name is not a parameter.
    fn result_owed(
        &mut self,
        params: &[Param],
        result: Option<&Type>,
        result_name: &mut Option<(String, Pos)>,
    ) -> Option<Vec<Expr>> {
        let Some((first, end)) = result.and_then(|t| t.range.as_ref()) else {
            return Some(Vec::new());
        };
        let pos = result.map_or(Pos::START, |t| t.pos);
        let named = result_name.as_ref().map(|(n, _)| n.clone());
        let mut result_is = named.unwrap_or_else(|| {
            let mut name = "result".to_string();
            while params.iter().any(|q| q.name == name) {
                name.push('@');
            }
            name
        });
        if result_name.is_none() {
            *result_name = Some((std::mem::take(&mut result_is), pos));
        }
        let given = result_name.as_ref().map(|(n, _)| n.clone())?;
        let name = |n: &str| Expr::Name {
            name: n.to_string(),
            pos,
        };
        let mut owed = Vec::new();
        let mut all_named = true;
        for bound in [first, end] {
            if let Some(n) = bound.named()
                && (n == given || !params.iter().any(|q| q.name == n))
            {
                self.errors.push(
                    Error::new(
                        pos,
                        format!("`{n}` is not a parameter of this function, and a bound is one"),
                    )
                    .with_fix("write the bound as a number, or take it as a parameter"),
                );
                all_named = false;
            }
        }
        if let Some(held) = first.expr(pos) {
            owed.push(compared(held, "<=", name(&given), pos));
        }
        if let Some(held) = end.expr(pos) {
            owed.push(compared(name(&given), "<", held, pos));
        }
        all_named.then_some(owed)
    }

    /// Parses the value of a default.
    fn default(&mut self) -> Result<Expr, Error> {
        self.expr()
    }

    /// Parses a tuple pattern where a name is introduced: a parameter, or
    /// the name of a `loop`. Its parts are names, `var` and a name, and
    /// tuples of these, as these always match.
    fn binder(&mut self) -> Result<Pattern, Error> {
        match self.tok() {
            Tok::Ident(name) if name != "true" && name != "false" => self.pattern(),
            Tok::Keyword("var") => self.pattern(),
            Tok::Punct("(") => {
                let pos = self.pos();
                let items = self.list(Self::binder)?;
                match items.len() {
                    0 => Err(Error::new(pos, "expected a pattern, found `()`")),
                    1 => Err(Error::unsupported(pos, "one pattern in parentheses")),
                    _ => Ok(Pattern::Tuple { pos, items }),
                }
            }
            _ => Err(Error::new(
                self.pos(),
                "this pattern may not match, so it cannot stand where a name is introduced, and is written in a `match`",
            )),
        }
    }

    /// Reads `take` before a type, if it is there.
    fn taking(&mut self) -> bool {
        let take = self.tok() == &Tok::Keyword("take");
        if take {
            self.bump();
        }
        take
    }

    /// Parses a type, and for a function type whether each of its parameters
    /// is marked `take`.
    fn ty_taking(&mut self) -> Result<(Type, Vec<bool>, Effects), Error> {
        if self.tok() == &Tok::Keyword("fn") {
            return self.function_type(true);
        }
        Ok((self.ty()?, Vec::new(), Vec::new()))
    }

    /// Reads past an array type, which is not built, from its `[`: the sizes
    /// in brackets and the type of its elements. The type it gives holds the
    /// names the sizes write and the type of the elements.
    fn array_type(&mut self, pos: Pos) -> Result<Type, Error> {
        self.unbuilt.push(Error::unsupported(pos, "an array type"));
        self.bump();
        let mut args = Vec::new();
        let mut depth = 1;
        while depth > 0 {
            match self.tok() {
                Tok::Eof | Tok::Newline => return Err(self.expected("`]`", self.tok())),
                Tok::Punct("[") => depth += 1,
                Tok::Punct("]") => depth -= 1,
                Tok::Ident(name) => args.push(Type {
                    name: name.clone(),
                    pos: self.pos(),
                    args: Vec::new(),
                    function: false,
                    range: None,
                }),
                _ => {}
            }
            self.bump();
        }
        args.push(self.ty()?);
        Ok(Type {
            name: UNBUILT.to_string(),
            pos,
            args,
            function: false,
            range: None,
        })
    }

    /// Parses a type: a name, and the types in brackets after it, if any.
    fn ty(&mut self) -> Result<Type, Error> {
        let pos = self.pos();
        let names = std::mem::take(&mut self.name_bound_ok);
        match self.tok() {
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                if self.tok() == &Tok::Punct("..") || self.tok() == &Tok::Punct(".") {
                    let first = self.name_bound(name, pos)?;
                    return self.range_type(pos, names, Some(first));
                }
                let mut args = Vec::new();
                if self.tok() == &Tok::Punct("[") {
                    self.bump();
                    args = self.types()?;
                }
                Ok(Type {
                    name,
                    pos,
                    args,
                    function: false,
                    range: None,
                })
            }
            Tok::Number(_) | Tok::Punct("-") => self.range_type(pos, names, None),
            Tok::Punct("[") => self.array_type(pos),
            Tok::Keyword("fn") => Ok(self.function_type(false)?.0),
            Tok::Punct("(") => {
                let mut args = self.list(Self::ty)?;
                if self.tok() == &Tok::Punct("->") {
                    let tuple = Type {
                        name: String::new(),
                        pos,
                        args,
                        function: false,
                        range: None,
                    };
                    self.bump();
                    let result = self.ty()?;
                    let params = tuple.args.iter().map(Type::written).collect::<Vec<_>>();
                    let fix = format!("fn({}) -> {}", params.join(", "), result.written());
                    return Err(Error::new(
                        pos,
                        "a list of types in parentheses is a tuple, and a function type is written with `fn`",
                    )
                    .with_fix(&fix));
                }
                if args.len() == 1 {
                    self.unbuilt
                        .push(Error::unsupported(pos, "one type in parentheses"));
                    return Ok(args.remove(0));
                }
                Ok(Type {
                    name: String::new(),
                    pos,
                    args,
                    function: false,
                    range: None,
                })
            }
            tok => Err(self.expected("a type", tok)),
        }
    }

    /// Parses a range type, `a..b`, whose bounds are numbers written out, or
    /// the name of a parameter where `names` says one may be. `first` is the
    /// first bound if it was read already.
    fn range_type(&mut self, pos: Pos, names: bool, first: Option<Bound>) -> Result<Type, Error> {
        let first = match first {
            Some(first) => first,
            None => self.range_bound()?,
        };
        if self.tok() != &Tok::Punct("..") {
            let tok = self.tok().clone();
            return Err(self.expected("`..` after the first bound of a range", &tok));
        }
        self.bump();
        let end = self.range_bound()?;
        if matches!(self.tok(), Tok::Punct("+" | "-" | "*" | "(")) {
            return Err(Error::new(
                pos,
                "a bound is a whole number, a name, or the `size` of a name",
            )
            .with_fix("bind the number to a name first, and write the name"));
        }
        match (&first, &end) {
            (Bound::Number(min), Bound::Number(end)) => {
                if end.checked_sub(1).is_none_or(|max| max < min - 1) {
                    return Err(Error::new(
                        pos,
                        "a range ends where it begins or after it, so `a..b` has `b` at least `a`",
                    ));
                }
            }
            _ if !names => {
                let (what, fix) = match self.no_names {
                    Some(NoNames::Field) => (
                        "the type of a field",
                        "write the bound as a number, or say what holds between two fields with a proof field",
                    ),
                    Some(NoNames::Var) => (
                        "the type of a `var`, which can change",
                        "write the bound as a number, or bind the value with `=`",
                    ),
                    None => ("a type inside another type", "write the bound as a number"),
                };
                self.errors.push(
                    Error::new(pos, format!("a name is not a bound in {what}")).with_fix(fix),
                );
                return Ok(Type {
                    name: UNBUILT.to_string(),
                    pos,
                    args: Vec::new(),
                    function: false,
                    range: None,
                });
            }
            _ => {}
        }
        Ok(Type {
            name: RANGE.to_string(),
            pos,
            args: Vec::new(),
            function: false,
            range: Some((first, end)),
        })
    }

    /// The bound that begins with the name just read: the name, or its `size`
    /// when `.size` follows. A bound is nothing else, and is not worked out.
    fn name_bound(&mut self, name: String, pos: Pos) -> Result<Bound, Error> {
        let refused = || {
            Error::new(
                pos,
                "a bound is a whole number, a name, or the `size` of a name",
            )
            .with_fix("bind the number to a name first, and write the name")
        };
        if self.tok() != &Tok::Punct(".") {
            return Ok(Bound::Name(name));
        }
        self.bump();
        match self.tok() {
            Tok::Ident(field) if field == "size" => {
                self.bump();
                Ok(Bound::Size(name))
            }
            _ => Err(refused()),
        }
    }

    /// Parses a bound of a range type: a whole number, with its `-` if it
    /// has one directly before it, or a name.
    fn range_bound(&mut self) -> Result<Bound, Error> {
        let pos = self.pos();
        if let Tok::Ident(name) = self.tok() {
            let name = name.clone();
            self.bump();
            return self.name_bound(name, pos);
        }
        let negative = self.tok() == &Tok::Punct("-");
        if negative {
            self.bump();
        }
        let Tok::Number(text) = self.tok().clone() else {
            let tok = self.tok().clone();
            return Err(self.expected("a whole number or a name as a bound of a range", &tok));
        };
        self.bump();
        match self.number_pattern(text, negative, pos)? {
            Pattern::Number {
                magnitude,
                negative,
                ..
            } if magnitude <= i128::MAX as u128 => Ok(Bound::Number(if negative {
                -(magnitude as i128)
            } else {
                magnitude as i128
            })),
            _ => Err(Error::new(pos, "a bound of a range is a whole number")),
        }
    }

    /// Parses the items of a tuple, separated by commas, from its `(`
    /// through its `)`. There may be none.
    fn list<T>(&mut self, item: fn(&mut Self) -> Result<T, Error>) -> Result<Vec<T>, Error> {
        self.list_until(")", item)
    }

    /// Parses items separated by commas, from the bracket that opens them
    /// through the `close` that ends them. There may be none.
    fn list_until<T>(
        &mut self,
        close: &'static str,
        item: fn(&mut Self) -> Result<T, Error>,
    ) -> Result<Vec<T>, Error> {
        let no_lambda = std::mem::replace(&mut self.no_lambda, false);
        let items = self.list_items(close, item);
        self.no_lambda = no_lambda;
        items
    }

    fn list_items<T>(
        &mut self,
        close: &'static str,
        item: fn(&mut Self) -> Result<T, Error>,
    ) -> Result<Vec<T>, Error> {
        self.bump();
        self.skip_newlines();
        let mut items = Vec::new();
        if self.tok() == &Tok::Punct(close) {
            self.bump();
            return Ok(items);
        }
        loop {
            items.push(item(self)?);
            self.skip_newlines();
            match self.tok() {
                Tok::Punct(",") => {
                    self.bump();
                    self.skip_newlines();
                    // A list written out may end with a comma.
                    if close == "]" && self.tok() == &Tok::Punct("]") {
                        break;
                    }
                }
                Tok::Punct(c) if *c == close => break,
                tok => return Err(self.expected(&format!("`,` or `{close}`"), tok)),
            }
        }
        self.bump();
        Ok(items)
    }

    /// Parses types separated by commas, after a `[`, through the `]`.
    fn types(&mut self) -> Result<Vec<Type>, Error> {
        let mut types = Vec::new();
        self.skip_newlines();
        loop {
            types.push(self.ty()?);
            self.skip_newlines();
            match self.tok() {
                Tok::Punct(",") => {
                    self.bump();
                    self.skip_newlines();
                    if self.tok() == &Tok::Punct("]") {
                        break;
                    }
                }
                Tok::Punct("]") => break,
                tok => return Err(self.expected("`,` or `]`", tok)),
            }
        }
        self.bump();
        Ok(types)
    }

    /// Parses a statement and the line break or `}` after it, and adds it
    /// to the block. A `with` passes the rest of the block to a call.
    fn statement_into(&mut self, block: &mut Block) -> Result<(), Error> {
        match self.statement_line()? {
            Line::With(wrapper) => block.wrap(wrapper),
            Line::Stmt(stmt) => block.inner.push(stmt),
        }
        Ok(())
    }

    fn statement_line(&mut self) -> Result<Line, Error> {
        let line = if self.tok() == &Tok::Keyword("with") {
            Line::With(self.with()?)
        } else {
            Line::Stmt(self.statement()?)
        };
        match self.tok() {
            Tok::Newline | Tok::Punct("}") => Ok(line),
            tok if continues_expression(tok) => Err(self.continuation()),
            tok => Err(self.expected("a line break or `}`", tok)),
        }
    }

    fn statement(&mut self) -> Result<Stmt, Error> {
        let pos = self.pos();
        match self.tok() {
            Tok::Ident(name) => match self.next_tok() {
                Tok::Punct("=" | ":") => {
                    let name = name.clone();
                    truth_named(&name, pos)?;
                    self.bump();
                    self.definition(name, pos, false)
                }
                _ => {
                    let expr = self.expr()?;
                    if self.tok() == &Tok::Punct("=")
                        && let Some((name, pos, fields)) = place(&expr)
                    {
                        self.bump();
                        let value = self.expr()?;
                        return Ok(Stmt::Assign {
                            name,
                            pos,
                            fields,
                            value,
                        });
                    }
                    self.assignment_not_built()?;
                    Ok(Stmt::Expr(expr))
                }
            },
            Tok::Keyword("var") => {
                self.bump();
                let (name, pos) = self.defined("the name of the `var`")?;
                self.definition(name, pos, true)
            }
            Tok::Punct("(") => self.tuple_statement(),
            Tok::Keyword("break") => {
                self.bump();
                Ok(Stmt::Expr(Expr::Break { pos }))
            }
            Tok::Keyword("return") => {
                self.bump();
                let value = match self.tok() {
                    Tok::Newline | Tok::Punct("}") | Tok::Eof => None,
                    _ => Some(Box::new(self.expr()?)),
                };
                Ok(Stmt::Expr(Expr::Return { value, pos }))
            }
            tok if starts_expression(tok) => Ok(Stmt::Expr(self.expr()?)),
            tok => Err(self.expected("a statement", tok)),
        }
    }

    /// Parses a statement that starts like a tuple: one that takes a value
    /// apart, when a pattern is followed by `:` or `=`, and otherwise an
    /// expression.
    fn tuple_statement(&mut self) -> Result<Stmt, Error> {
        let start = self.at;
        let mistake = match self.pattern() {
            Ok(pattern) if matches!(self.tok(), Tok::Punct(":" | "=")) => {
                return self.destructure(pattern);
            }
            Ok(_) => None,
            Err(e) => Some(e),
        };
        self.at = start;
        // When neither reads it, the mistake to report is the one the
        // reading that got further found.
        let expr = match (self.expr(), &mistake) {
            (Ok(expr), _) => expr,
            (Err(e), Some(m)) if m.pos > e.pos => return Err(mistake.expect("a mistake")),
            (Err(e), _) => return Err(e),
        };
        // A value followed by `=` is meant as a pattern, so the mistake that
        // keeps it from being one is the one to report.
        if let Some(e) = mistake
            && self.tok() == &Tok::Punct("=")
        {
            return Err(e);
        }
        self.assignment_not_built()?;
        Ok(Stmt::Expr(expr))
    }

    /// Parses the rest of a statement that takes a value apart, from the
    /// `:` or `=` after its pattern.
    fn destructure(&mut self, pattern: Pattern) -> Result<Stmt, Error> {
        let mut ty = None;
        if self.tok() == &Tok::Punct(":") {
            self.bump();
            ty = Some(self.ty()?);
        }
        self.assignment_not_built()?;
        self.expect_punct("=", "`=`")?;
        let value = self.expr()?;
        Ok(Stmt::Destructure { pattern, ty, value })
    }

    /// Parses the rest of a definition, from the `:` or `=` after its name.
    fn definition(&mut self, name: String, pos: Pos, var: bool) -> Result<Stmt, Error> {
        let mut ty = None;
        if self.tok() == &Tok::Punct(":") {
            self.bump();
            // A name that cannot change is a bound of a binding's type, and a
            // `var` can change.
            self.name_bound_ok = !var && self.tok() != &Tok::Keyword("fn");
            self.no_names = var.then_some(NoNames::Var);
            let written = self.ty();
            self.name_bound_ok = false;
            self.no_names = None;
            ty = Some(written?);
        }
        self.assignment_not_built()?;
        self.expect_punct("=", "`=`")?;
        let value = self.expr()?;
        Ok(Stmt::Define {
            name,
            pos,
            ty,
            var,
            value,
        })
    }

    /// The error for an accumulating assignment, which the compiler does not
    /// build yet.
    fn assignment_not_built(&self) -> Result<(), Error> {
        match self.tok() {
            Tok::Punct(op @ ("+=" | "-=" | "*=" | "/=")) => {
                Err(Error::unsupported(self.pos(), &format!("`{op}`")))
            }
            _ => Ok(()),
        }
    }

    /// The error for a token that would continue an expression, which the
    /// compiler does not build yet.
    fn continuation(&self) -> Error {
        let pos = self.pos();
        match self.tok() {
            Tok::Punct("(") => Error::unsupported(pos, "calling what this gives"),
            tok => Error::unsupported(pos, &format!("the operator {}", tok.describe())),
        }
    }

    /// Parses an expression, with its operators grouped as the precedence
    /// table in `knowledge/grammar.md` says.
    fn expr(&mut self) -> Result<Expr, Error> {
        let mut expr = self.either()?;
        while self.tok() == &Tok::Punct("|>") {
            expr = self.step(expr)?;
        }
        if continues_expression(self.tok()) {
            return Err(self.continuation());
        }
        Ok(expr)
    }

    /// Parses the call after a `|>`, and gives the call that has `left` in
    /// the place of its `_`, or in front of its arguments when it has none.
    fn step(&mut self, left: Expr) -> Result<Expr, Error> {
        self.bump();
        self.skip_newlines();
        let try_at = (self.tok() == &Tok::Keyword("try")).then(|| self.pos());
        if try_at.is_some() {
            self.bump();
        }
        let holes = self.holes.len();
        let pos = self.pos();
        let right = self.operand()?;
        let found = self.holes.split_off(holes);
        let Expr::Call(mut call) = right else {
            let error = Error::new(pos, "the right side of `|>` is not a call");
            return Err(match right {
                Expr::Name { name, .. } => error.with_fix(format!("write `{name}()`")),
                _ => error.with_fix("write a call, with its parentheses"),
            });
        };
        let whole: Vec<usize> = (call.args.iter().enumerate())
            .filter(|(_, arg)| matches!(&arg.value, Expr::Name { name, .. } if name == "_"))
            .map(|(at, _)| at)
            .collect();
        if let Some(&second) = whole.get(1) {
            return Err(Error::new(
                call.args[second].value.pos(),
                "a call has one `_`, and this is one more",
            )
            .with_fix("write the value to a name before the call, and the name in each place"));
        }
        let inside = found
            .iter()
            .find(|at| !whole.iter().any(|&arg| call.args[arg].value.pos() == **at));
        if let Some(&at) = inside {
            return Err(Error::new(
                at,
                "`_` is a whole argument, and this one is inside another expression",
            )
            .with_fix("write the call that holds it as a step of its own, as `x |> g() |> f(_)`"));
        }
        match whole.first() {
            Some(&at) => call.args[at].value = left,
            None => call.args.insert(
                0,
                Arg {
                    name: None,
                    value: left,
                    after: false,
                },
            ),
        }
        let call = Expr::Call(call);
        Ok(match try_at {
            Some(at) => split(at, call),
            None => call,
        })
    }

    /// Parses operands joined by `or`.
    fn either(&mut self) -> Result<Expr, Error> {
        let mut left = self.both()?;
        while self.tok() == &Tok::Keyword("or") {
            self.bump();
            self.skip_newlines();
            let right = self.both()?;
            left = logic(Form::Or, left, right);
        }
        Ok(left)
    }

    /// Parses operands joined by `and`.
    fn both(&mut self) -> Result<Expr, Error> {
        let mut left = self.negation()?;
        while self.tok() == &Tok::Keyword("and") {
            self.bump();
            self.skip_newlines();
            let right = self.negation()?;
            left = logic(Form::And, left, right);
        }
        Ok(left)
    }

    /// Parses `not` before what it covers, or a comparison.
    fn negation(&mut self) -> Result<Expr, Error> {
        if self.tok() != &Tok::Keyword("not") {
            return self.comparison();
        }
        let pos = self.pos();
        self.bump();
        self.skip_newlines();
        let operand = self.negation()?;
        Ok(negated(pos, operand))
    }

    /// Parses `loop`, through the block that ends it.
    fn loop_expr(&mut self) -> Result<Loop, Error> {
        let pos = self.pos();
        self.bump();
        let mut decreases = None;
        let mut over = None;
        let mut lead = Vec::new();
        let range = match self.tok() {
            Tok::Punct("{") => None,
            Tok::Ident(name) if name == "decreases" => {
                self.bump();
                let no_lambda = std::mem::replace(&mut self.no_lambda, true);
                let measure = self.expr();
                self.no_lambda = no_lambda;
                decreases = Some(measure?);
                None
            }
            tok if starts_pattern(tok)
                || matches!(tok, Tok::Ident(_)) && self.next_tok() == &Tok::Keyword("in") =>
            {
                let at = self.pos();
                let name = if let Tok::Ident(name) = self.tok() {
                    let name = name.clone();
                    self.bump();
                    name
                } else {
                    let pattern = self.binder()?;
                    let (name, _) = hidden(&pattern);
                    lead.push(taken_apart(pattern, &name, at));
                    name
                };
                if self.tok() != &Tok::Keyword("in") {
                    return Err(self.expected("`in`", self.tok()));
                }
                self.bump();
                let no_lambda = std::mem::replace(&mut self.no_lambda, true);
                let head = self.range_head();
                self.no_lambda = no_lambda;
                match head? {
                    (from, Some(to)) => Some(Range {
                        name,
                        pos: at,
                        from,
                        to,
                    }),
                    (value, None) => {
                        over = Some(Over {
                            name,
                            pos: at,
                            value,
                        });
                        None
                    }
                }
            }
            tok => return Err(self.expected("`{`, or a name and `in` and a range", tok)),
        };
        self.expect_punct("{", "`{` to open the body of the `loop`")?;
        let (body, end) = self.inner_block(lead)?;
        Ok(Loop {
            pos,
            range,
            over,
            decreases,
            body,
            end,
        })
    }

    /// Parses what a `loop` runs over: `a..b`, or a value alone, whose
    /// elements it runs over.
    fn range_head(&mut self) -> Result<(Expr, Option<Expr>), Error> {
        let from = self.sum()?;
        if self.tok() != &Tok::Punct("..") {
            return Ok((from, None));
        }
        self.bump();
        self.skip_newlines();
        let to = self.sum()?;
        Ok((from, Some(to)))
    }

    /// Parses `if`, through the block that ends it.
    fn if_expr(&mut self) -> Result<Match, Error> {
        let pos = self.pos();
        self.bump();
        let no_lambda = std::mem::replace(&mut self.no_lambda, true);
        let of = self.expr();
        self.no_lambda = no_lambda;
        let of = of?;
        self.expect_punct("{", "`{` to open the block of the `if`")?;
        let (then, then_end) = self.inner_block(Vec::new())?;
        let has_else = self.tok() == &Tok::Keyword("else");
        let (otherwise, end) = if has_else {
            self.bump();
            if self.tok() == &Tok::Keyword("if") {
                let inner = self.if_expr()?;
                let end = inner.arms[1].end;
                (vec![Stmt::Expr(Expr::Match(Box::new(inner)))], end)
            } else {
                self.expect_punct("{", "`{` to open the block of the `else`")?;
                self.inner_block(Vec::new())?
            }
        } else {
            (Vec::new(), then_end)
        };
        Ok(Match {
            pos,
            of,
            arms: vec![
                arm("true", pos, then, then_end),
                arm("false", pos, otherwise, end),
            ],
            form: Form::If {
                otherwise: has_else,
            },
        })
    }

    /// Parses the statements of a block after its `{`, where a `{` after a
    /// call opens a lambda again, through the `}` that closes it.
    fn inner_block(&mut self, lead: Vec<Stmt>) -> Result<(Vec<Stmt>, Pos), Error> {
        let no_lambda = std::mem::replace(&mut self.no_lambda, false);
        let block = self.block_rest(lead);
        self.no_lambda = no_lambda;
        block
    }

    /// Parses an operand: text, a name, a call, or any other expression
    /// that is not made by an operator, followed by the fields it reads.
    fn operand(&mut self) -> Result<Expr, Error> {
        let pos = self.pos();
        let mut expr = match self.tok() {
            Tok::Str(text) => {
                let text = text.clone();
                self.bump();
                Expr::Text { text, pos }
            }
            Tok::Interp(pieces) => {
                let pieces = pieces.clone();
                self.bump();
                self.interpolation(&pieces, pos)?
            }
            Tok::Number(text) => {
                let text = text.clone();
                self.number_literal(text, pos)?
            }
            // A `-` directly before a number, where a value may begin, is
            // part of the number.
            Tok::Punct("-") if self.negative_number_follows(pos) => {
                self.negative_number_literal(pos)?
            }
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                self.call_or_name(None, name, pos)?
            }
            Tok::Punct(".") => {
                self.bump();
                let (name, _) = self.ident("a case's name")?;
                let mut payload = None;
                if self.tok() == &Tok::Punct("(") {
                    // A case takes no lambda after it.
                    let no_lambda = std::mem::replace(&mut self.no_lambda, true);
                    let call = self.call_rest(None, name.clone(), pos);
                    self.no_lambda = no_lambda;
                    let call = call?;
                    let mut args = call.args.into_iter();
                    payload = args.next().map(|arg| match arg.name {
                        Some((_, at)) => Err(Error::new(
                            at,
                            "the value a case holds is given by position",
                        )),
                        None => Ok(Box::new(arg.value)),
                    });
                    if let Some(extra) = args.next() {
                        return Err(Error::new(
                            extra.value.pos(),
                            "a case holds one value, and this is one more",
                        ));
                    }
                }
                Expr::Case {
                    name,
                    pos,
                    payload: payload.transpose()?,
                }
            }
            Tok::Keyword("match") => Expr::Match(Box::new(self.match_expr()?)),
            Tok::Keyword("if") => Expr::Match(Box::new(self.if_expr()?)),
            Tok::Keyword("loop") => Expr::Loop(Box::new(self.loop_expr()?)),
            Tok::Keyword("try") => {
                self.bump();
                let operand = self.operand()?;
                return Ok(split(pos, operand));
            }
            // `-x` is the number that `0 - x` is, so its range is the negation of
            // the range of `x`.
            Tok::Punct("-") => {
                self.bump();
                let operand = self.operand()?;
                return Ok(Expr::Arith {
                    op: ArithOp::Sub,
                    left: Box::new(Expr::Number {
                        text: "0".to_string(),
                        magnitude: 0,
                        negative: false,
                        pos,
                    }),
                    right: Box::new(operand),
                    op_pos: pos,
                });
            }
            Tok::Punct("(") => {
                let mut items = self.list(Self::expr)?;
                if items.len() == 1 {
                    items.pop().expect("one")
                } else {
                    Expr::Tuple { pos, items }
                }
            }
            Tok::Punct("{") | Tok::Keyword("fn") => Expr::Lambda(Box::new(self.lambda()?)),
            Tok::Punct("[") => self.list_literal(pos)?,
            tok if starts_expression(tok) => {
                let feature = match tok {
                    Tok::Keyword(word) => {
                        return Err(Error::unsupported(pos, &format!("`{word}`")));
                    }
                    _ => "this expression",
                };
                return Err(Error::unsupported(pos, feature));
            }
            tok => return Err(self.expected("an expression", tok)),
        };
        while matches!(self.tok(), Tok::Punct("." | "[")) {
            if self.tok() == &Tok::Punct("[") {
                expr = self.index(expr)?;
                continue;
            }
            self.bump();
            let (name, pos) = self.ident("a field's name")?;
            expr = match expr {
                Expr::Name {
                    name: qualifier,
                    pos: at,
                } if self.modules.iter().any(|(q, _)| *q == qualifier) => {
                    let module = (self.modules.iter())
                        .find(|(q, _)| *q == qualifier)
                        .map(|(_, m)| m.clone())
                        .unwrap_or_default();
                    self.qualified.push(Qualified {
                        module: module.clone(),
                        written: (qualifier, at),
                        name: (name.clone(), pos),
                    });
                    let mut expr = self.call_or_name(None, format!("{name}@{module}"), pos)?;
                    match &mut expr {
                        Expr::Call(call) => call.pos = at,
                        Expr::Name { pos, .. } => *pos = at,
                        _ => {}
                    }
                    expr
                }
                Expr::Name {
                    name: qualifier,
                    pos: at,
                } => self.call_or_name(Some((qualifier, at)), name, pos)?,
                of => Expr::Field {
                    of: Box::new(of),
                    name,
                    pos,
                },
            };
        }
        Ok(expr)
    }

    /// Parses a `match`, through the `}` that closes it.
    fn match_expr(&mut self) -> Result<Match, Error> {
        let pos = self.pos();
        self.bump();
        let no_lambda = std::mem::replace(&mut self.no_lambda, true);
        let of = self.expr();
        self.no_lambda = no_lambda;
        let of = of?;
        self.expect_punct("{", "`{` to open the blocks of the `match`")?;
        let mut arms = Vec::new();
        loop {
            self.skip_newlines();
            if self.tok() == &Tok::Punct("}") {
                break;
            }
            let pattern = self.pattern()?;
            self.expect_punct("{", "`{` to open the pattern's block")?;
            let (body, end) = self.inner_block(Vec::new())?;
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            arms.push(Arm { pattern, body, end });
        }
        self.bump();
        Ok(Match {
            pos,
            of,
            arms,
            form: Form::Match,
        })
    }

    /// Parses a pattern.
    fn pattern(&mut self) -> Result<Pattern, Error> {
        let pos = self.pos();
        match self.tok() {
            // `true` and `false` are the cases of `bool` in a pattern too.
            Tok::Ident(name) if name == "true" || name == "false" => {
                let name = name.clone();
                self.bump();
                Ok(Pattern::Case {
                    name,
                    pos,
                    inner: None,
                })
            }
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                Ok(Pattern::Bind {
                    name,
                    pos,
                    var: None,
                })
            }
            Tok::Keyword("var") => {
                self.bump();
                let (name, at) = self.defined("the name of the `var`")?;
                Ok(Pattern::Bind {
                    name,
                    pos: at,
                    var: Some(pos),
                })
            }
            Tok::Punct(".") => {
                self.bump();
                let (name, _) = self.ident("a case's name")?;
                let mut inner = None;
                if self.tok() == &Tok::Punct("(") {
                    self.bump();
                    // `.a(())` is `.a`, as a case that holds `()` holds
                    // nothing.
                    if self.tok() == &Tok::Punct("(") && self.next_tok() == &Tok::Punct(")") {
                        self.bump();
                        self.bump();
                    } else {
                        inner = Some(Box::new(self.pattern()?));
                    }
                    self.expect_punct(")", "`)`")?;
                }
                Ok(Pattern::Case { name, pos, inner })
            }
            Tok::Punct("(") => {
                let items = self.list(Self::pattern)?;
                match items.len() {
                    0 => Err(Error::new(pos, "expected a pattern, found `()`")),
                    1 => Err(Error::unsupported(pos, "one pattern in parentheses")),
                    _ => Ok(Pattern::Tuple { pos, items }),
                }
            }
            Tok::Number(text) => {
                let text = text.clone();
                self.bump();
                self.number_pattern(text, false, pos)
            }
            Tok::Punct("-")
                if matches!(self.next_tok(), Tok::Number(_))
                    && self.tokens[self.at + 1].pos
                        == (Pos {
                            line: pos.line,
                            col: pos.col + 1,
                        }) =>
            {
                self.bump();
                let Tok::Number(text) = self.tok().clone() else {
                    unreachable!("a number")
                };
                self.bump();
                self.number_pattern(text, true, pos)
            }
            Tok::Str(text) => {
                let text = text.clone();
                self.bump();
                Ok(Pattern::Text { text, pos })
            }
            Tok::Interp(_) => Err(Error::new(pos, "a text in a pattern holds no expression")),
            tok => Err(self.expected("a pattern", tok)),
        }
    }

    /// The pattern for a whole number or a character written as `text`.
    fn number_pattern(&mut self, text: String, negative: bool, pos: Pos) -> Result<Pattern, Error> {
        match number(text, negative, pos)? {
            Expr::Number {
                text,
                magnitude,
                negative,
                pos,
            } => Ok(Pattern::Number {
                text,
                magnitude,
                negative,
                pos,
            }),
            _ => unreachable!("a number"),
        }
    }

    /// Parses the statements of a block, after its `{`, through the `}` that
    /// closes it, and gives where that is.
    fn block_rest(&mut self, lead: Vec<Stmt>) -> Result<(Vec<Stmt>, Pos), Error> {
        let mut block = Block {
            inner: lead,
            ..Block::default()
        };
        loop {
            self.skip_newlines();
            if self.tok() == &Tok::Punct("}") {
                let end = self.pos();
                self.bump();
                return Ok((block.finish(end), end));
            }
            let start = self.at;
            match self.statement_into(&mut block) {
                Ok(()) => {}
                Err(e) if e.unbuilt => {
                    if !self.read_past_statement(start, e.clone(), &mut block) {
                        return Err(e);
                    }
                }
                Err(e) => return Err(e),
            }
        }
    }

    /// Reads past a statement that uses what is not built yet, from the
    /// token `start`, so that the statements after it are checked. In its
    /// place the block holds a binding of the name it defines, if it defines
    /// one, to what is not built, so that uses of the name are not said to
    /// have none. Gives whether the end of the statement was found.
    fn read_past_statement(&mut self, start: usize, error: Error, block: &mut Block) -> bool {
        let pos = error.pos;
        self.unbuilt.push(error);
        let found = self.skip_statement(start);
        let var = self.tokens[start].tok == Tok::Keyword("var");
        let first = start + usize::from(var);
        // The names the statement binds: one, or those of a tuple taken
        // apart, which are the names in its parentheses.
        let names: Vec<(&String, Pos)> = match &self.tokens[first].tok {
            Tok::Ident(n)
                if matches!(
                    self.tokens.get(first + 1).map(|t| &t.tok),
                    Some(Tok::Punct("=" | ":"))
                ) =>
            {
                vec![(n, self.tokens[first].pos)]
            }
            Tok::Punct("(") => {
                let close = self.tokens[first..]
                    .iter()
                    .position(|t| t.tok == Tok::Punct(")"))
                    .map_or(first, |i| first + i);
                if self.tokens.get(close + 1).map(|t| &t.tok) == Some(&Tok::Punct("=")) {
                    (self.tokens[first..close].iter())
                        .filter_map(|t| match &t.tok {
                            Tok::Ident(n) => Some((n, t.pos)),
                            _ => None,
                        })
                        .collect()
                } else {
                    Vec::new()
                }
            }
            _ => Vec::new(),
        };
        if names.is_empty() {
            block.inner.push(Stmt::Expr(Expr::Unbuilt { pos }));
        }
        for (name, at) in names {
            block.inner.push(Stmt::Define {
                name: name.clone(),
                pos: at,
                ty: None,
                var,
                value: Expr::Unbuilt { pos },
            });
        }
        found
    }
}

/// The items shared, as a `File` holds them.
fn share<T>(items: Vec<T>) -> Vec<Arc<T>> {
    items.into_iter().map(Arc::new).collect()
}
