//! Lowers the implementations a type is given: `Repr`, `Eq`, `Ord`, and
//! `Fmt`, each made of the same trait of the type's parts.

use super::*;

impl<'a> Lowering<'a> {
    /// The number `value` of type `from` as a number of type `to`, which
    /// holds every value of `from`.
    pub(super) fn widening(
        &mut self,
        out: &mut Out<'a>,
        from: Int,
        to: Int,
        value: form::Value,
    ) -> Vec<form::Value> {
        vec![out.resize(from.signed(), value, bits_of(to))]
    }

    /// The function of the implementation of trait `k` given to `ty`. `Fmt`
    /// gives what the type's `Repr` gives, `Repr` writes the type's value as
    /// the source that builds it, with each part written by its own `Repr`,
    /// and `Eq` says whether two values are the same case with equal parts,
    /// by each part's own `Eq`. `Ord` orders by the first part where two
    /// values differ, as `ordered` says.
    pub(super) fn given(&mut self, k: usize, given: &[Ty]) -> form::Function {
        let defs = self.typed.defs;
        let ty = &given[0];
        let mut out = Out::new();
        let types = self.scalars(ty);
        let a = out.params(&types);
        let (result, results) = if k == defs.convert() {
            (
                self.case_of(&mut out, &given[1], ty, &a),
                self.scalars(&given[1]),
            )
        } else if k == defs.fmt {
            let text = self.run(&mut out, defs.repr, ty, a);
            (text, self.scalars(&Ty::Text))
        } else if k == defs.repr {
            (self.repr(&mut out, ty, &a), self.scalars(&Ty::Text))
        } else if k == defs.ord() {
            let b = out.params(&types);
            let order = self.ordered(&mut out, ty, &a, &b);
            (vec![order], self.scalars(&Ty::Enum(defs.order, Vec::new())))
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

    /// The value of the enum `to` that is the case holding `value`, of type `held`.
    fn case_of(
        &mut self,
        out: &mut Out<'a>,
        to: &Ty,
        held: &Ty,
        value: &[form::Value],
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let Ty::Enum(e, args) = to else {
            unreachable!("a conversion to an enum")
        };
        let k = (defs.cases[*e].iter())
            .position(|t| t.as_ref().is_some_and(|t| t.subst(args) == *held))
            .expect("a case holds it");
        let layout = self.layout(*e, args);
        let mut places: Vec<Option<form::Value>> = vec![None; layout.places.len()];
        for (v, &place) in value.iter().zip(&layout.cases[k]) {
            places[place] = Some(*v);
        }
        let case = out.constant(
            form::Constant::Int(k as u64),
            form::Type::Int(form::Bits::B8),
        );
        let mut values = vec![case];
        for (place, ty) in places.into_iter().zip(&layout.places) {
            values.push(place.unwrap_or_else(|| out.zero(*ty)));
        }
        values
    }

    /// Runs the function of trait `k`, which gives text, a `bool`, or an `Order`, for
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

    /// The case `name` of `Order`.
    fn order_case(&self, out: &mut Out<'a>, name: &str) -> form::Value {
        let defs = self.typed.defs;
        let k = defs.case_of(defs.order, name).expect("a case");
        out.constant(form::Constant::Int(k as u64), BYTE)
    }

    /// How the values `a` and `b` of the type stand, as an `Order`: a struct
    /// or a tuple by its parts in order, an enum by its case and then by what
    /// the case holds, and text by its bytes, then by its length.
    fn ordered(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        a: &[form::Value],
        b: &[form::Value],
    ) -> form::Value {
        let defs = self.typed.defs;
        let parts: Vec<Ty> = match ty {
            Ty::Text => return self.ordered_text(out, a, b),
            Ty::Enum(e, args) => {
                let layout = self.layout(*e, args);
                let holds: Vec<Option<Ty>> = (defs.cases[*e].iter())
                    .map(|t| t.as_ref().map(|t| t.subst(args)))
                    .collect();
                let below = out.compare(form::Compare::LessU, a[0], b[0]);
                let same = out.compare(form::Compare::Equal, a[0], b[0]);
                let arms = out.switch(same, 2);
                out.current = arms[0];
                let (less, greater) = (
                    self.order_case(out, "less"),
                    self.order_case(out, "greater"),
                );
                let differ = out.choose(below, less, greater);
                let mut ends = vec![(out.current, vec![differ])];
                out.current = arms[1];
                let mut inner = Vec::new();
                let on_a = out.switch(a[0], holds.len());
                for (k, held) in holds.iter().enumerate() {
                    out.current = on_a[k];
                    let order = match held {
                        Some(t) => {
                            let take = |values: &[form::Value]| -> Vec<form::Value> {
                                layout.cases[k].iter().map(|&p| values[1 + p]).collect()
                            };
                            let args = [take(a), take(b)].concat();
                            self.part_order(out, t, args)
                        }
                        None => self.order_case(out, "equal"),
                    };
                    inner.push((out.current, vec![order]));
                }
                let order = out.join(&[BYTE], inner);
                ends.push((out.current, order));
                return out.join(&[BYTE], ends)[0];
            }
            Ty::Struct(s, args) => defs.fields[*s].iter().map(|t| t.subst(args)).collect(),
            Ty::Tuple(of) => of.clone(),
            Ty::Nothing => Vec::new(),
            _ => unreachable!("a type given `Ord`"),
        };
        let mut at = 0;
        let mut pairs = Vec::new();
        for t in &parts {
            let size = self.size(t);
            pairs.push((t, a[at..at + size].to_vec(), b[at..at + size].to_vec()));
            at += size;
        }
        self.first_unequal(out, &pairs)
    }

    /// How the two values that `args` holds, one after the other, of type
    /// `t` stand, by the `Ord` of `t`. Whole numbers are compared where they
    /// are, as no program writes an `Ord` for them, and so a comparison of a
    /// struct of them is no call.
    fn part_order(&mut self, out: &mut Out<'a>, t: &Ty, args: Vec<form::Value>) -> form::Value {
        let Ty::Int(int) = t else {
            return self.run(out, self.typed.defs.ord(), t, args)[0];
        };
        let (a, b) = (args[0], args[1]);
        let less = if int.signed() {
            form::Compare::LessS
        } else {
            form::Compare::LessU
        };
        let below = out.compare(less, a, b);
        let same = out.compare(form::Compare::Equal, a, b);
        let (l, e, g) = (
            self.order_case(out, "less"),
            self.order_case(out, "equal"),
            self.order_case(out, "greater"),
        );
        let after = out.choose(same, e, g);
        out.choose(below, l, after)
    }

    /// How the first pair that is not equal stands, each by its type's `Ord`,
    /// where the pairs after one run only when it is equal, and `equal` when
    /// every pair is.
    fn first_unequal(
        &mut self,
        out: &mut Out<'a>,
        pairs: &[(&Ty, Vec<form::Value>, Vec<form::Value>)],
    ) -> form::Value {
        let defs = self.typed.defs;
        let Some(((t, x, y), rest)) = pairs.split_first() else {
            return self.order_case(out, "equal");
        };
        let order = self.part_order(out, t, [x.clone(), y.clone()].concat());
        if rest.is_empty() {
            return order;
        }
        let arms = out.switch(order, 3);
        let mut ends = Vec::new();
        for name in ["less", "equal", "greater"] {
            let at = defs.case_of(defs.order, name).expect("a case");
            out.current = arms[at];
            let value = if name == "equal" {
                self.first_unequal(out, rest)
            } else {
                order
            };
            ends.push((out.current, vec![value]));
        }
        out.join(&[BYTE], ends)[0]
    }

    /// How two texts stand: by the first byte where they differ, and by their
    /// lengths when one is the start of the other.
    fn ordered_text(
        &mut self,
        out: &mut Out<'a>,
        a: &[form::Value],
        b: &[form::Value],
    ) -> form::Value {
        let (x, x_count, y, y_count) = (a[0], a[1], b[0], b[1]);
        let zero = out.int(form::Bits::B64, 0);
        let head = out.new_block();
        let place = out.fresh(form::Type::Int(form::Bits::B64));
        out.blocks[head].params = vec![place];
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: head,
            args: vec![zero],
        });
        out.current = head;
        // The loop leaves when a text ends, and where two bytes differ.
        let at_x = out.compare(form::Compare::Equal, place, x_count);
        let at_y = out.compare(form::Compare::Equal, place, y_count);
        let arms = out.switch(at_x, 2);
        let mut ends = Vec::new();
        out.current = arms[1];
        // `a` ended: it is below `b` unless `b` ended too.
        let (less, equal) = (self.order_case(out, "less"), self.order_case(out, "equal"));
        let ended = out.choose(at_y, equal, less);
        ends.push((out.current, vec![ended]));
        out.current = arms[0];
        let arms = out.switch(at_y, 2);
        out.current = arms[1];
        let greater = self.order_case(out, "greater");
        ends.push((out.current, vec![greater]));
        out.current = arms[0];
        let (p, q) = (
            out.binary(form::Binary::Add, x, place),
            out.binary(form::Binary::Add, y, place),
        );
        let load = |out: &mut Out<'a>, address| {
            out.emit(form::Op::Load { ty: BYTE, address }, vec![BYTE])[0]
        };
        let (byte_x, byte_y) = (load(out, p), load(out, q));
        let same = out.compare(form::Compare::Equal, byte_x, byte_y);
        let arms = out.switch(same, 2);
        out.current = arms[0];
        let below = out.compare(form::Compare::LessU, byte_x, byte_y);
        let (less, greater) = (
            self.order_case(out, "less"),
            self.order_case(out, "greater"),
        );
        let differ = out.choose(below, less, greater);
        ends.push((out.current, vec![differ]));
        out.current = arms[1];
        let one = out.int(form::Bits::B64, 1);
        let next = out.binary(form::Binary::Add, place, one);
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: head,
            args: vec![next],
        });
        out.join(&[BYTE], ends)[0]
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
