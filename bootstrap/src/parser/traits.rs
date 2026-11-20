//! Parses the declarations of traits: a trait, an implementation of one, and
//! `without`, which takes away a trait a type is given.

use super::*;

/// `without Trait for Type`, which takes away a trait the type would be
/// given automatically.
#[derive(Debug, PartialEq, Eq)]
pub struct Without {
    pub pos: Pos,
    /// The trait, and where its name is.
    pub name: String,
    pub name_pos: Pos,
    /// The type, and where its name is.
    pub ty: String,
    pub ty_pos: Pos,
}

/// A trait: a name, its types, and the signatures of its functions, each a
/// function with no body.
#[derive(Debug, PartialEq, Eq)]
pub struct Trait {
    pub name: String,
    pub pos: Pos,
    pub generics: Vec<Generic>,
    pub functions: Vec<Function>,
}

/// An implementation of a trait for the types in its brackets.
#[derive(Debug, PartialEq, Eq)]
pub struct Impl {
    /// Its own type parameters, written after `impl`.
    pub generics: Vec<Generic>,
    /// The trait, and where its name is.
    pub name: String,
    pub pos: Pos,
    pub args: Vec<Type>,
    /// What the types it is used with must meet, one expression for each
    /// `requires`.
    pub requires: Vec<Expr>,
    pub functions: Vec<Function>,
}

impl Parser<'_> {
    /// Parses `without Trait for Type` and the line break after it.
    pub(super) fn without(&mut self) -> Result<Without, Error> {
        let pos = self.pos();
        self.bump();
        let (name, name_pos) = self.ident("the name of a trait")?;
        if self.tok() != &Tok::Keyword("for") {
            return Err(self.expected("`for` and a type", self.tok()));
        }
        self.bump();
        let (ty, ty_pos) = self.ident("the name of a type")?;
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(Without {
                pos,
                name,
                name_pos,
                ty,
                ty_pos,
            }),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses a trait and the line break after it.
    pub(super) fn declare_trait(&mut self) -> Result<Trait, Error> {
        self.bump();
        let (name, pos) = self.defined("the trait's name")?;
        if self.tok() != &Tok::Punct("[") {
            return Err(self.expected("`[` and the trait's types", self.tok()));
        }
        let generics = self.generics()?;
        self.expect_punct("{", "`{` to open the trait's functions")?;
        let mut functions = Vec::new();
        loop {
            self.skip_newlines();
            match self.tok() {
                Tok::Punct("}") => break,
                Tok::Keyword("fn") => {}
                tok => return Err(self.expected("a function's signature or `}`", tok)),
            }
            let (name, pos, generics, params, result, result_name, effects) = self.signature()?;
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                Tok::Keyword("requires" | "ensures") => {
                    return Err(Error::unsupported(self.pos(), "a contract"));
                }
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            functions.push(Function {
                name,
                pos,
                generics,
                params,
                result,
                result_name,
                effects,
                requires: Vec::new(),
                needs: Vec::new(),
                decreases: Vec::new(),
                ensures: Vec::new(),
                body: Vec::new(),
                end: pos,
            });
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(Trait {
                name,
                pos,
                generics,
                functions,
            }),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses an implementation. A mistake in its first line or in the
    /// signature of one of its functions is the one error reported for it,
    /// and a mistake in a statement of a function is reported along with
    /// those in its other statements, which gives `None`.
    pub(super) fn implementation(&mut self) -> Result<Option<Impl>, Error> {
        self.bump();
        let generics = self.generics()?;
        let (name, pos) = self.ident("the trait's name")?;
        if self.tok() != &Tok::Punct("[") {
            return Err(self.expected("`[` and the types it is implemented for", self.tok()));
        }
        self.bump();
        let args = self.types()?;
        let mut requires = Vec::new();
        loop {
            if self.tok() == &Tok::Newline
                && self.tokens.get(self.at + 1).map(|t| &t.tok) == Some(&Tok::Keyword("requires"))
            {
                self.bump();
            }
            if self.tok() != &Tok::Keyword("requires") {
                break;
            }
            self.bump();
            let no_lambda = std::mem::replace(&mut self.no_lambda, true);
            let statement = self.expr();
            self.no_lambda = no_lambda;
            requires.push(statement?);
        }
        if !requires.is_empty()
            && self.tok() == &Tok::Newline
            && self.tokens.get(self.at + 1).map(|t| &t.tok) == Some(&Tok::Punct("{"))
        {
            self.bump();
        }
        self.expect_punct("{", "`{` to open the implementation's functions")?;
        let mut functions = Vec::new();
        let mut ok = true;
        loop {
            self.skip_newlines();
            let start = self.at;
            match self.tok() {
                Tok::Punct("}") => break,
                Tok::Keyword("fn") => {}
                tok => return Err(self.expected("a function or `}`", tok)),
            }
            match self.function(start, true)? {
                Some(f) => functions.push(f),
                None => ok = false,
            }
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(ok.then_some(Impl {
                generics,
                name,
                pos,
                args,
                requires,
                functions,
            })),
            tok => Err(self.expected("a line break", tok)),
        }
    }
}
