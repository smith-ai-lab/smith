//! The structs that hold the arguments of the functions of a file, which
//! `Function` is given for. A struct is made for each function that can be
//! asked of `Function`, with the parameters as its fields: the same names, in
//! the same order, with the same types and defaults. It is made only when the
//! file writes `Function`, so a program that does not pays nothing for it.

use crate::comments;
use crate::parser::{self, Field, File, Struct};

/// Whether the file writes the name of the trait `Function`.
pub(crate) fn asks(file: &File) -> bool {
    file.idents.iter().any(|(name, _)| name == "Function")
}

/// Makes the struct of the arguments of each function of the file that has
/// one, and notes each with the comment on its function. `number` is the
/// number of the module, whose places are that many steps on. A function that is
/// an operation of an effect, has type parameters, or takes a function, has
/// none.
pub(crate) fn add(file: &mut File, source: &str, number: usize) {
    let mut made = Vec::new();
    for f in &file.functions {
        if parser::is_operation(f)
            || !f.generics.is_empty()
            || f.params.iter().any(|p| p.ty.function)
        {
            continue;
        }
        let name = format!("{}.Args", f.name);
        let fields = (f.params.iter())
            .map(|p| Field {
                name: p.name.clone(),
                pos: p.pos,
                ty: p.ty.clone(),
                default: p.default.clone(),
                // The fields of the struct are those of the function, which
                // any module that has the function may read.
                public: Some(p.pos),
            })
            .collect();
        made.push((
            f.pos,
            comments::above(source, f.pos.line - number * crate::check::STEP),
            Struct {
                name,
                pos: f.pos,
                generics: Vec::new(),
                fields,
            },
        ));
    }
    for (pos, about, made) in made {
        file.args.push((pos, made.name.clone(), about));
        file.structs.push(std::sync::Arc::new(made));
    }
}
