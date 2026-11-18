//! Lowers lists and text: a string literal, a string that holds expressions,
//! a list written out, `push`, and reading an element of a list.

use super::*;

impl<'a> Lowering<'a> {
    pub(super) fn text(&mut self, out: &mut Out<'a>, text: &[u8]) -> Vec<form::Value> {
        let at = match self.data.iter().position(|t| t == text) {
            Some(at) => at,
            None => {
                self.data.push(text.to_vec());
                self.data.len() - 1
            }
        };
        let address = out.constant(form::Constant::Data(at), form::Type::Address);
        let length = out.constant(
            form::Constant::Int(text.len() as u64),
            form::Type::Int(form::Bits::B64),
        );
        vec![address, length]
    }

    /// The types of the scalars of each element of a list of the type.
    pub(super) fn element(&self, list: &Ty) -> Vec<form::Type> {
        match list {
            Ty::List(of) => self.scalars(&of[0]),
            _ => unreachable!("checked"),
        }
    }

    /// Lowers `push`, which gives the list with the item after its last
    /// element, in place when the list may grow where it is.
    pub(super) fn push(
        &mut self,
        out: &mut Out<'a>,
        call: &'a Call,
        env: &[Ty],
        in_place: bool,
    ) -> (Ty, Vec<form::Value>) {
        let (ty, mut args) = self.expr(out, &call.args[0].value, env);
        args.extend(self.expr(out, &call.args[1].value, env).1);
        args.push(out.int(form::Bits::B8, i128::from(in_place)));
        let results = self.scalars(&ty);
        let kind = Runtime::Push(self.element(&ty));
        let values = self.call_runtime(out, kind, args, results);
        (ty, values)
    }

    /// Works out where the element or the part of one that `expr` reads is,
    /// without reading it: its type, the address of its first byte, and each
    /// of its scalars with its place from the address. `expr` is an index,
    /// or a field, or the `size`, of a part of one.
    pub(super) fn element_at(
        &mut self,
        out: &mut Out<'a>,
        expr: &'a Expr,
        env: &[Ty],
    ) -> (Ty, form::Value, Vec<(form::Type, u64)>) {
        let defs = self.typed.defs;
        match expr {
            Expr::Index { of, index, .. } => {
                let (list, values) = self.expr(out, of, env);
                let (at, number) = self.expr(out, index, env);
                let scalars = self.element(&list);
                let (places, stride) = form::element(&scalars);
                let place = match at {
                    Ty::Int(t) if bits_of(t) == form::Bits::B64 => number[0],
                    _ => out.widen(false, number[0]),
                };
                let width = out.int(form::Bits::B64, i128::from(stride));
                let offset = out.binary(form::Binary::Mul, place, width);
                let address = out.binary(form::Binary::Add, values[0], offset);
                let Ty::List(of) = list else {
                    unreachable!("checked")
                };
                (
                    of[0].clone(),
                    address,
                    scalars.into_iter().zip(places).collect(),
                )
            }
            Expr::Field { of, name, .. } => {
                let (ty, address, scalars) = self.element_at(out, of, env);
                let (start, size, ty) = match ty {
                    Ty::List(_) => (1, 1, Ty::Int(Int::U64)),
                    Ty::Struct(s, args) => {
                        let mut start = 0;
                        let mut found = None;
                        for (field, t) in defs.file.structs[s].fields.iter().zip(&defs.fields[s]) {
                            let t = t.subst(&args);
                            if &field.name == name {
                                found = Some(t);
                                break;
                            }
                            start += self.size(&t);
                        }
                        let t = found.expect("checked");
                        (start, self.size(&t), t)
                    }
                    _ => unreachable!("checked"),
                };
                (ty, address, scalars[start..start + size].to_vec())
            }
            _ => unreachable!("checked"),
        }
    }

    /// Reads the scalars at their places from the address.
    pub(super) fn load(
        &mut self,
        out: &mut Out<'a>,
        address: form::Value,
        scalars: &[(form::Type, u64)],
    ) -> Vec<form::Value> {
        scalars
            .iter()
            .map(|&(ty, place)| {
                let at = match place {
                    0 => address,
                    _ => {
                        let by = out.int(form::Bits::B64, i128::from(place));
                        out.binary(form::Binary::Add, address, by)
                    }
                };
                out.emit(form::Op::Load { ty, address: at }, vec![ty])[0]
            })
            .collect()
    }

    /// Lowers a string literal that holds expressions: the parts are joined
    /// into one text.
    pub(super) fn interpolation(
        &mut self,
        out: &mut Out<'a>,
        parts: &'a [Part],
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let count = parts.len();
        let mut args = Vec::new();
        for part in parts {
            args.extend(match part {
                Part::Text(text) => self.text(out, text),
                Part::Expr(e) => self.expr(out, e, env).1,
            });
        }
        let values = self.call_runtime(out, Runtime::Join(count), args, TEXT.to_vec());
        (Ty::Text, values)
    }

    /// Lowers a list written out, `[]` or `[a, b, c]`.
    pub(super) fn list_literal(
        &mut self,
        out: &mut Out<'a>,
        expr: &'a Expr,
        items: &'a [Expr],
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        let ty = self.typed.type_of(expr, env);
        let scalars = self.element(&ty);
        let (places, stride) = form::element(&scalars);
        let elements: Vec<Vec<form::Value>> = items
            .iter()
            .map(|item| self.expr(out, item, env).1)
            .collect();
        let count = out.int(form::Bits::B64, items.len() as i128);
        if items.is_empty() {
            let none = out.constant(form::Constant::Null, form::Type::Address);
            return (ty, vec![none, count, count]);
        }
        let bytes = out.int(form::Bits::B64, items.len() as i128 * stride as i128);
        let start = out.emit(
            form::Op::System {
                call: form::System::Allocate,
                args: vec![bytes],
            },
            vec![form::Type::Address],
        )[0];
        for (k, element) in elements.into_iter().enumerate() {
            for (value, place) in element.into_iter().zip(&places) {
                let offset = out.int(form::Bits::B64, (k as u64 * stride + place) as i128);
                let address = out.binary(form::Binary::Add, start, offset);
                out.emit(
                    form::Op::Store {
                        ty: out.types[value.0],
                        address,
                        value,
                    },
                    Vec::new(),
                );
            }
        }
        (ty, vec![start, count, count])
    }
}

/// Whether the expression is an index, or a field, or the `size`, of a part of
/// one, so that only the scalars it reads need be read.
pub(super) fn indexed(e: &Expr) -> bool {
    match e {
        Expr::Index { .. } => true,
        Expr::Field { of, .. } => indexed(of),
        _ => false,
    }
}
