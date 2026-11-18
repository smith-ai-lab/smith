//! Lowers the implementations a type is given: `Repr`, `Eq`, and `Fmt`, each
//! made of the same trait of the type's parts.

use super::*;

impl<'a> Lowering<'a> {
    /// The function of the implementation of trait `k` given to `ty`. `Fmt`
    /// gives what the type's `Repr` gives, `Repr` writes the type's value as
    /// the source that builds it, with each part written by its own `Repr`,
    /// and `Eq` says whether two values are the same case with equal parts,
    /// by each part's own `Eq`.
    pub(super) fn given(&mut self, k: usize, ty: &Ty) -> form::Function {
        let defs = self.typed.defs;
        let mut out = Out::new();
        let types = self.scalars(ty);
        let a = out.params(&types);
        let (result, results) = if k == defs.fmt {
            let text = self.run(&mut out, defs.repr, ty, a);
            (text, self.scalars(&Ty::Text))
        } else if k == defs.repr {
            (self.repr(&mut out, ty, &a), self.scalars(&Ty::Text))
        } else {
            let b = out.params(&types);
            let same = self.equal(&mut out, ty, &a, &b);
            (vec![same], self.scalars(&defs.bool_ty()))
        };
        out.blocks[out.current].end = form::End::Return(result);
        form::Function {
            types: out.types,
            results,
            blocks: out.blocks,
        }
    }

    /// Runs the function of trait `k`, which gives text or a `bool`, for
    /// values of the type, and gives what it gives.
    fn run(
        &mut self,
        out: &mut Out<'a>,
        k: usize,
        ty: &Ty,
        args: Vec<form::Value>,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let (instance, _) = defs.chosen(k, 0, vec![ty.clone()]);
        let function = self.place(Body::Copy(instance));
        let types = self.scalars(&defs.trait_sigs[k][0].gives);
        out.emit(form::Op::Call { function, args }, types)
    }

    /// Whether the values `a` and `b` of the type are equal, as a `bool`.
    fn equal(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        a: &[form::Value],
        b: &[form::Value],
    ) -> form::Value {
        let defs = self.typed.defs;
        let parts: Vec<Ty> = match ty {
            Ty::Text => {
                let args = [a, b].concat();
                let kind = Runtime::Equal;
                return self.call_runtime(out, kind, args, vec![BYTE])[0];
            }
            Ty::Enum(e, args) => {
                let layout = self.layout(*e, args);
                let bool_types = self.scalars(&defs.bool_ty());
                let holds: Vec<Option<Ty>> = (defs.cases[*e].iter())
                    .map(|t| t.as_ref().map(|t| t.subst(args)))
                    .collect();
                let mut ends = Vec::new();
                let on_a = out.switch(a[0], holds.len());
                for (k, held) in holds.iter().enumerate() {
                    out.current = on_a[k];
                    let on_b = out.switch(b[0], holds.len());
                    for (j, &block) in on_b.iter().enumerate() {
                        out.current = block;
                        let same = if j != k {
                            self.truth(out, false)
                        } else {
                            match held {
                                Some(t) => {
                                    let take = |values: &[form::Value]| -> Vec<form::Value> {
                                        layout.cases[k].iter().map(|&p| values[1 + p]).collect()
                                    };
                                    let args = [take(a), take(b)].concat();
                                    self.run(out, defs.eq, t, args)[0]
                                }
                                None => self.truth(out, true),
                            }
                        };
                        ends.push((out.current, vec![same]));
                    }
                }
                return out.join(&bool_types, ends)[0];
            }
            Ty::Struct(s, args) => defs.fields[*s].iter().map(|t| t.subst(args)).collect(),
            Ty::Tuple(of) => of.clone(),
            Ty::Nothing => Vec::new(),
            _ => unreachable!("a type given `Eq`"),
        };
        let mut at = 0;
        let mut pairs = Vec::new();
        for t in &parts {
            let size = self.size(t);
            pairs.push((t, a[at..at + size].to_vec(), b[at..at + size].to_vec()));
            at += size;
        }
        self.all_equal(out, &pairs)
    }

    /// Whether every pair is equal, each by its type's `Eq`, where the pairs
    /// after one run only when it is equal.
    fn all_equal(
        &mut self,
        out: &mut Out<'a>,
        pairs: &[(&Ty, Vec<form::Value>, Vec<form::Value>)],
    ) -> form::Value {
        let defs = self.typed.defs;
        let Some(((t, x, y), rest)) = pairs.split_first() else {
            return self.truth(out, true);
        };
        let same = self.run(out, defs.eq, t, [x.clone(), y.clone()].concat())[0];
        if rest.is_empty() {
            return same;
        }
        let types = self.scalars(&defs.bool_ty());
        let arms = out.switch(same, 2);
        let (no, yes) = (
            defs.case_of(defs.boolean, "false").expect("a case"),
            defs.case_of(defs.boolean, "true").expect("a case"),
        );
        out.current = arms[no];
        let false_ = self.truth(out, false);
        let mut ends = vec![(out.current, vec![false_])];
        out.current = arms[yes];
        let rest = self.all_equal(out, rest);
        ends.push((out.current, vec![rest]));
        out.join(&types, ends)[0]
    }

    /// The text `Repr` gives for the values of a value of the type, as
    /// the implementation it is given writes it.
    fn repr(&mut self, out: &mut Out<'a>, ty: &Ty, values: &[form::Value]) -> Vec<form::Value> {
        let defs = self.typed.defs;
        match ty {
            Ty::Nothing => self.text(out, b"()"),
            Ty::Text => self.call_runtime(out, Runtime::Escape, values.to_vec(), TEXT.to_vec()),
            Ty::Struct(s, args) => {
                let def = &defs.file.structs[*s];
                let mut at = 0;
                let mut items = Vec::new();
                for (field, t) in def.fields.iter().zip(&defs.fields[*s]) {
                    let t = t.subst(args);
                    let size = self.size(&t);
                    items.push((
                        format!("{}: ", field.name),
                        t,
                        values[at..at + size].to_vec(),
                    ));
                    at += size;
                }
                self.listed(out, &format!("{}(", def.name), items)
            }
            Ty::Tuple(of) => {
                let items = (of.iter().zip(self.elements(of, values)))
                    .map(|(t, values)| (String::new(), t.clone(), values))
                    .collect();
                self.listed(out, "(", items)
            }
            Ty::Enum(e, args) => {
                let def = &defs.file.enums[*e];
                let layout = self.layout(*e, args);
                let arms = out.switch(values[0], def.cases.len());
                let mut ends = Vec::new();
                for (k, ((case, holds), block)) in
                    def.cases.iter().zip(&defs.cases[*e]).zip(arms).enumerate()
                {
                    out.current = block;
                    let text = match holds {
                        Some(t) => {
                            let held = layout.cases[k].iter().map(|&p| values[1 + p]).collect();
                            let item = (String::new(), t.subst(args), held);
                            self.listed(out, &format!(".{}(", case.name), vec![item])
                        }
                        // The cases of `bool` are written as bare names.
                        None if *e == defs.boolean => self.text(out, case.name.as_bytes()),
                        None => self.text(out, format!(".{}", case.name).as_bytes()),
                    };
                    ends.push((out.current, text));
                }
                out.join(&TEXT, ends)
            }
            _ => unreachable!("a type given `Repr`"),
        }
    }

    /// The text `open`, then the label of each item and the text its value's
    /// `Repr` gives, the items separated by commas, and then `)`.
    fn listed(
        &mut self,
        out: &mut Out<'a>,
        open: &str,
        items: Vec<(String, Ty, Vec<form::Value>)>,
    ) -> Vec<form::Value> {
        let repr = self.typed.defs.repr;
        let mut parts = Vec::new();
        let mut known = open.as_bytes().to_vec();
        for (i, (label, t, values)) in items.into_iter().enumerate() {
            if i > 0 {
                known.extend(b", ");
            }
            known.extend(label.bytes());
            let text = self.run(out, repr, &t, values);
            parts.push(self.text(out, &std::mem::take(&mut known)));
            parts.push(text);
        }
        known.push(b')');
        let close = self.text(out, &known);
        if parts.is_empty() {
            return close;
        }
        parts.push(close);
        let count = parts.len();
        let args = parts.concat();
        self.call_runtime(out, Runtime::Join(count), args, TEXT.to_vec())
    }
}
