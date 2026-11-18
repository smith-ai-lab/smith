//! Lowers the implementations a type is given: `Repr`, `Eq`, `Ord`, `Hash`, and
//! `Fmt`, each made of the same trait of the type's parts.

use super::*;

/// The type of a hash, and of the numbers it is worked out from.
const WORD: form::Type = form::Type::Int(form::Bits::B64);

/// The multiplier that spreads one part of a hash over the next, which is
/// the fraction of the golden ratio in 64 bits.
const SPREAD: u64 = 0x9e37_79b9_7f4a_7c15;

/// The two multipliers and the shift of `mix`, which are those of the
/// finalizer of MurmurHash3.
const MIX: [u64; 2] = [0xff51_afd7_ed55_8ccd, 0xc4ce_b9fe_1a85_ec53];
const MIX_SHIFT: i128 = 33;

/// What a hash of text starts from, and what it multiplies by for each byte,
/// which are those of 64-bit FNV-1a.
const BASIS: u64 = 0xcbf2_9ce4_8422_2325;
const PRIME: u64 = 0x0000_0100_0000_01b3;

impl<'a> Lowering<'a> {
    /// The function of the implementation of trait `k` given to `ty`. `Fmt`
    /// gives what the type's `Repr` gives, `Repr` writes the type's value as
    /// the source that builds it, with each part written by its own `Repr`,
    /// and `Eq` says whether two values are the same case with equal parts,
    /// by each part's own `Eq`. `Ord` orders by the first part where two
    /// values differ, as `ordered` says.
    pub(super) fn given(&mut self, k: usize, given: &[Ty]) -> form::Function {
        let defs = self.typed.defs;
        if defs.serialize == Some(k) {
            return self.serialized(&given[0], &given[1]);
        }
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
        } else if k == defs.hash() {
            (vec![self.hash(&mut out, ty, &a)], vec![WORD])
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

    /// The function that writes a value of `ty` to a writer of type `w`, which
    /// takes the value and the writer and gives the writer back. It calls the
    /// function of the writer's `Writer` for each shape of the value, and the
    /// part's own `Serialize` for each part, and builds nothing between.
    fn serialized(&mut self, ty: &Ty, w: &Ty) -> form::Function {
        let mut out = Out::new();
        let types = self.scalars(ty);
        let writer = self.scalars(w);
        let value = out.params(&types);
        let to = out.params(&writer);
        let to = self.written(&mut out, ty, &value, to, w);
        out.blocks[out.current].end = form::End::Return(to);
        form::Function {
            types: out.types,
            results: writer,
            blocks: out.blocks,
        }
    }

    /// Calls the function `name` of the writer's `Writer`, which is given the
    /// writer and then `args`, and gives the writer back.
    fn writer_call(
        &mut self,
        out: &mut Out<'a>,
        w: &Ty,
        name: &str,
        to: Vec<form::Value>,
        args: Vec<Vec<form::Value>>,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let writer = defs.writer.expect("a program that writes has the module");
        let at = crate::check::traits::trait_function(&defs.file.traits[writer], name)
            .expect("a function of `Writer`");
        let (instance, _) = defs.chosen(writer, at, vec![w.clone()], Vec::new());
        let function = self.place(Body::Copy(instance));
        let types = self.scalars(w);
        let args = std::iter::once(to).chain(args).flatten().collect();
        out.emit(form::Op::Call { function, args }, types)
    }

    /// A number the writer is given as a `Nat`, which is 64 bits.
    fn count(&mut self, out: &mut Out<'a>, n: usize) -> Vec<form::Value> {
        vec![out.int(form::Bits::B64, n as i128)]
    }

    /// The writer after the value is written to it by the `Serialize` of its
    /// type, which is the one the type is given or writes itself.
    fn written_part(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        values: Vec<form::Value>,
        to: Vec<form::Value>,
        w: &Ty,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let serialize = defs
            .serialize
            .expect("a program that writes has the module");
        let (instance, _) = defs.chosen(serialize, 0, vec![ty.clone()], vec![w.clone()]);
        let function = self.place(Body::Copy(instance));
        let types = self.scalars(w);
        let args = values.into_iter().chain(to).collect();
        out.emit(form::Op::Call { function, args }, types)
    }

    /// The writer after the value of the type, whose values are `values`, is
    /// written to `to`, by the shape of the type: a number as `signed` or
    /// `unsigned`, text as `text`, a struct as its name, each field's name
    /// with the field, and its end, an enum as its case and what the case
    /// holds, and a list as its size, each element, and its end.
    fn written(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        values: &[form::Value],
        to: Vec<form::Value>,
        w: &Ty,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        match ty {
            Ty::Int(t) => {
                let signed = t.signed();
                let n = out.resize(signed, values[0], form::Bits::B64);
                let name = if signed { "signed" } else { "unsigned" };
                self.writer_call(out, w, name, to, vec![vec![n]])
            }
            Ty::Text => self.writer_call(out, w, "text", to, vec![values.to_vec()]),
            Ty::Nothing => self.writer_call(out, w, "nothing", to, Vec::new()),
            Ty::Tuple(of) => {
                let size = self.count(out, of.len());
                let mut to = self.writer_call(out, w, "tuple", to, vec![size]);
                for (t, part) in of.iter().zip(self.elements(of, values)) {
                    to = self.written_part(out, t, part, to, w);
                }
                self.writer_call(out, w, "end_tuple", to, Vec::new())
            }
            Ty::Struct(s, args) => {
                let def = &defs.file.structs[*s];
                let name = self.text(out, crate::check::written_name(&def.name).as_bytes());
                let size = self.count(out, def.fields.len());
                let mut to = self.writer_call(out, w, "structure", to, vec![name, size]);
                let mut at = 0;
                for (field, t) in def.fields.iter().zip(&defs.fields[*s]) {
                    let t = t.subst(args);
                    let size = self.size(&t);
                    let name = self.text(out, field.name.as_bytes());
                    to = self.writer_call(out, w, "field", to, vec![name]);
                    to = self.written_part(out, &t, values[at..at + size].to_vec(), to, w);
                    at += size;
                }
                self.writer_call(out, w, "end_structure", to, Vec::new())
            }
            Ty::List(of) => {
                let size = vec![values[1]];
                let to = self.writer_call(out, w, "sequence", to, vec![size]);
                let to = self.each_written(out, &of[0], values, to, w);
                self.writer_call(out, w, "end_sequence", to, Vec::new())
            }
            Ty::Enum(e, args) => {
                if *e == defs.boolean {
                    return self.writer_call(out, w, "boolean", to, vec![vec![values[0]]]);
                }
                let def = &defs.file.enums[*e];
                let layout = self.layout(*e, args);
                let arms = out.switch(values[0], def.cases.len());
                let mut ends = Vec::new();
                for (k, ((case, holds), block)) in
                    def.cases.iter().zip(&defs.cases[*e]).zip(arms).enumerate()
                {
                    out.current = block;
                    let to = to.clone();
                    let mut to = if *e == defs.option {
                        let name = if holds.is_some() { "present" } else { "absent" };
                        self.writer_call(out, w, name, to, Vec::new())
                    } else {
                        let of = self.text(out, crate::check::written_name(&def.name).as_bytes());
                        let name = self.text(out, case.name.as_bytes());
                        let holds = out.int(form::Bits::B8, i128::from(holds.is_some()));
                        self.writer_call(out, w, "case", to, vec![of, name, vec![holds]])
                    };
                    if let Some(t) = holds {
                        let part = layout.cases[k].iter().map(|&p| values[1 + p]).collect();
                        to = self.written_part(out, &t.subst(args), part, to, w);
                    }
                    ends.push((out.current, to));
                }
                out.join(&self.scalars(w), ends)
            }
            _ => unreachable!("a type given `Serialize`"),
        }
    }

    /// The writer after each element of the list, whose address and size are
    /// the first two of `values`, is written to it.
    fn each_written(
        &mut self,
        out: &mut Out<'a>,
        element: &Ty,
        values: &[form::Value],
        to: Vec<form::Value>,
        w: &Ty,
    ) -> Vec<form::Value> {
        let scalars = self.scalars(element);
        let (places, stride) = form::element(&scalars);
        let places: Vec<(form::Type, u64)> = scalars.into_iter().zip(places).collect();
        let stride = out.int(form::Bits::B64, i128::from(stride));
        let zero = out.int(form::Bits::B64, 0);
        let (address, size) = (values[0], values[1]);
        let header = out.new_block();
        let i = out.fresh(WORD);
        let carried: Vec<form::Value> = (self.scalars(w).into_iter())
            .map(|t| out.fresh(t))
            .collect();
        out.blocks[header].params = std::iter::once(i).chain(carried.iter().copied()).collect();
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: std::iter::once(zero).chain(to).collect(),
        });
        out.current = header;
        let more = out.compare(form::Compare::LessU, i, size);
        let arms = out.switch(more, 2);
        out.current = arms[1];
        let offset = out.binary(form::Binary::Mul, i, stride);
        let at = out.binary(form::Binary::Add, address, offset);
        let part = self.load(out, at, &places);
        let next = self.written_part(out, element, part, carried.clone(), w);
        let one = out.int(form::Bits::B64, 1);
        let after = out.binary(form::Binary::Add, i, one);
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: std::iter::once(after).chain(next).collect(),
        });
        out.current = arms[0];
        carried
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
        let (instance, _) = defs.chosen(k, 0, vec![ty.clone()], Vec::new());
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

    /// The hash of the values of a value of the type: for a number, its 64
    /// bits mixed; for text, FNV-1a over its bytes and then its size, mixed;
    /// for the rest, the hashes of its parts one after another with
    /// `combine`, and for an enum those of the case's number and what it
    /// holds. Each part is hashed by its own type's `Hash`.
    fn hash(&mut self, out: &mut Out<'a>, ty: &Ty, values: &[form::Value]) -> form::Value {
        let defs = self.typed.defs;
        let parts: Vec<Ty> = match ty {
            Ty::Int(t) => {
                let word = out.resize(t.signed(), values[0], form::Bits::B64);
                return self.mix(out, word);
            }
            Ty::Text => {
                let basis = out.int(form::Bits::B64, i128::from(BASIS));
                let prime = out.int(form::Bits::B64, i128::from(PRIME));
                let (address, size) = (values[0], values[1]);
                let h = self.fold(out, size, basis, |_, out, i, h| {
                    let at = out.binary(form::Binary::Add, address, i);
                    let byte = out.emit(
                        form::Op::Load {
                            ty: BYTE,
                            address: at,
                        },
                        vec![BYTE],
                    )[0];
                    let byte = out.widen(false, byte);
                    let mixed = out.binary(form::Binary::Xor, h, byte);
                    out.binary(form::Binary::Mul, mixed, prime)
                });
                let sized = out.binary(form::Binary::Xor, h, size);
                return self.mix(out, sized);
            }
            Ty::List(of) => {
                let scalars = self.scalars(&of[0]);
                let (places, stride) = form::element(&scalars);
                let places: Vec<(form::Type, u64)> = scalars.into_iter().zip(places).collect();
                let stride = out.int(form::Bits::B64, i128::from(stride));
                let zero = out.int(form::Bits::B64, 0);
                let (address, size) = (values[0], values[1]);
                let h = self.fold(out, size, zero, |this, out, i, h| {
                    let offset = out.binary(form::Binary::Mul, i, stride);
                    let at = out.binary(form::Binary::Add, address, offset);
                    let element = this.load(out, at, &places);
                    let part = this.run(out, defs.hash(), &of[0], element)[0];
                    this.combine(out, h, part)
                });
                return self.combine(out, h, size);
            }
            Ty::Enum(e, args) => {
                let layout = self.layout(*e, args);
                let holds: Vec<Option<Ty>> = (defs.cases[*e].iter())
                    .map(|t| t.as_ref().map(|t| t.subst(args)))
                    .collect();
                let arms = out.switch(values[0], holds.len());
                let mut ends = Vec::new();
                for (k, (held, block)) in holds.iter().zip(arms).enumerate() {
                    out.current = block;
                    let zero = out.int(form::Bits::B64, 0);
                    let case = out.int(form::Bits::B64, k as i128);
                    let mut h = self.combine(out, zero, case);
                    if let Some(t) = held {
                        let part: Vec<form::Value> =
                            layout.cases[k].iter().map(|&p| values[1 + p]).collect();
                        let part = self.run(out, defs.hash(), t, part)[0];
                        h = self.combine(out, h, part);
                    }
                    ends.push((out.current, vec![h]));
                }
                return out.join(&[WORD], ends)[0];
            }
            Ty::Struct(s, args) => defs.fields[*s].iter().map(|t| t.subst(args)).collect(),
            Ty::Tuple(of) => of.clone(),
            Ty::Nothing => Vec::new(),
            _ => unreachable!("a type given `Hash`"),
        };
        let mut h = out.int(form::Bits::B64, 0);
        let mut at = 0;
        for t in &parts {
            let size = self.size(t);
            let part = self.run(out, defs.hash(), t, values[at..at + size].to_vec())[0];
            h = self.combine(out, h, part);
            at += size;
        }
        h
    }

    /// A hash of the hash `h` followed by the hash `part`, as a function of
    /// both whose result differs for every `part` and for every `h`.
    fn combine(&mut self, out: &mut Out<'a>, h: form::Value, part: form::Value) -> form::Value {
        let spread = out.int(form::Bits::B64, i128::from(SPREAD));
        let spread = out.binary(form::Binary::Mul, h, spread);
        let sum = out.binary(form::Binary::Add, spread, part);
        self.mix(out, sum)
    }

    /// `x` with its bits spread over all 64: the finalizer of MurmurHash3,
    /// which takes every 64-bit number to a different one.
    fn mix(&mut self, out: &mut Out<'a>, x: form::Value) -> form::Value {
        use form::Binary::*;
        let shift = out.int(form::Bits::B64, MIX_SHIFT);
        let mut x = x;
        for by in MIX {
            let high = out.binary(ShiftRightU, x, shift);
            x = out.binary(Xor, x, high);
            let by = out.int(form::Bits::B64, i128::from(by));
            x = out.binary(Mul, x, by);
        }
        let high = out.binary(ShiftRightU, x, shift);
        out.binary(Xor, x, high)
    }

    /// A loop that, for each `i` below `count`, takes the 64-bit `h`, which
    /// starts as `seed`, to what `step` makes of `i` and `h`, and gives the
    /// `h` after the last.
    fn fold(
        &mut self,
        out: &mut Out<'a>,
        count: form::Value,
        seed: form::Value,
        step: impl FnOnce(&mut Self, &mut Out<'a>, form::Value, form::Value) -> form::Value,
    ) -> form::Value {
        let zero = out.int(form::Bits::B64, 0);
        let header = out.new_block();
        let (i, h) = (out.fresh(WORD), out.fresh(WORD));
        out.blocks[header].params = vec![i, h];
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: vec![zero, seed],
        });
        out.current = header;
        let more = out.compare(form::Compare::LessU, i, count);
        let arms = out.switch(more, 2);
        out.current = arms[1];
        let next = step(self, out, i, h);
        let one = out.int(form::Bits::B64, 1);
        let after = out.binary(form::Binary::Add, i, one);
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: vec![after, next],
        });
        out.current = arms[0];
        h
    }
}
