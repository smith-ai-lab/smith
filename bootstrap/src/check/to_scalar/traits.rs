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
    pub(super) fn given(&mut self, k: usize, fi: usize, given: &[Ty]) -> form::Function {
        let defs = self.typed.defs;
        if k == defs.function {
            return self.function_of(fi, &given[0]);
        }
        if defs.serialize == Some(k) {
            return self.serialized(&given[0], &given[1]);
        }
        if defs.deserialize == Some(k) {
            return self.deserialized(&given[0], &given[1]);
        }
        if defs.describe == Some(k) {
            return self.described(&given[0], &given[1]);
        }
        if defs.from_text == Some(k) {
            return self.text_reading(fi, &given[0]);
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
            cold: false,
            name: String::new(),
        }
    }

    /// The function `fi` of `Function` given to the type of the name of a
    /// function: `name` and `about` give a constant text, and `call` calls the
    /// function with the scalars of its arguments, which are those of the
    /// struct that holds them, in the order of the parameters.
    fn function_of(&mut self, fi: usize, ty: &Ty) -> form::Function {
        let defs = self.typed.defs;
        let Ty::Named(f) = ty else {
            unreachable!("`Function` is given to the name of a function")
        };
        let mut out = Out::new();
        let (values, results) = match fi {
            0 | 1 => {
                let text = if fi == 0 {
                    written_name(&defs.file.functions[*f].name)
                } else {
                    defs.about_of(*f)
                };
                (
                    self.text(&mut out, text.as_bytes()),
                    self.scalars(&Ty::Text),
                )
            }
            _ => {
                let args = Ty::Struct(defs.args_of(*f).expect("made for it"), Vec::new());
                let args = out.params(&self.scalars(&args));
                let results = self.scalars(&defs.signatures[*f].gives);
                let function = self.place(Body::Copy(Instance {
                    template: Template::Free(*f),
                    args: Vec::new(),
                }));
                let values = out.emit(form::Op::Call { function, args }, results.clone());
                (values, results)
            }
        };
        out.blocks[out.current].end = form::End::Return(values);
        form::Function {
            types: out.types,
            results,
            blocks: out.blocks,
            cold: false,
            name: String::new(),
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
            cold: false,
            name: String::new(),
        }
    }

    /// The function `fi` of `FromText` given to `ty`: `parse` reads the text
    /// as a whole number of the range, `true` or `false`, or the text itself,
    /// and `expects` says in words what it reads.
    fn text_reading(&mut self, fi: usize, ty: &Ty) -> form::Function {
        let mut out = Out::new();
        let (values, results) = if fi == 1 {
            let words = match ty {
                Ty::Int(t) => format!("a whole number from {} to {}", t.min(), t.max()),
                Ty::Text => "a text".to_string(),
                _ => "true or false".to_string(),
            };
            (
                self.text(&mut out, words.as_bytes()),
                self.scalars(&Ty::Text),
            )
        } else {
            let text = out.params(&self.scalars(&Ty::Text));
            let values = match ty {
                Ty::Int(t) => self.read_whole(&mut out, *t, &text, ty),
                Ty::Text => self.some_of(&mut out, ty, text),
                _ => self.read_truth(&mut out, ty, &text),
            };
            let option = Ty::Enum(self.typed.defs.option, vec![ty.clone()]);
            (values, self.scalars(&option))
        };
        out.blocks[out.current].end = form::End::Return(values);
        form::Function {
            types: out.types,
            results,
            blocks: out.blocks,
            cold: false,
            name: String::new(),
        }
    }

    /// The `Option` of `ty` that holds `value`, which is the scalars of a value.
    fn some_of(&mut self, out: &mut Out<'a>, ty: &Ty, value: Vec<form::Value>) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let layout = self.layout(defs.option, std::slice::from_ref(ty));
        let k = defs.case_of(defs.option, "some").expect("a case");
        let mut places: Vec<Option<form::Value>> = vec![None; layout.places.len()];
        for (v, &place) in value.iter().zip(&layout.cases[k]) {
            places[place] = Some(*v);
        }
        let case = out.constant(form::Constant::Int(k as u64), BYTE);
        let mut values = vec![case];
        for (place, ty) in places.into_iter().zip(&layout.places) {
            values.push(place.unwrap_or_else(|| out.zero(*ty)));
        }
        values
    }

    /// The `Option` of `ty` that holds nothing.
    fn none_of(&mut self, out: &mut Out<'a>, ty: &Ty) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let layout = self.layout(defs.option, std::slice::from_ref(ty));
        let k = defs.case_of(defs.option, "none").expect("a case");
        let case = out.constant(form::Constant::Int(k as u64), BYTE);
        let mut values = vec![case];
        for ty in &layout.places {
            values.push(out.zero(*ty));
        }
        values
    }

    /// `.some(true)` for the text `true`, `.some(false)` for `false`, and
    /// `.none` for any other.
    fn read_truth(&mut self, out: &mut Out<'a>, ty: &Ty, text: &[form::Value]) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let mut ends = Vec::new();
        for (word, truth) in [("true", 1), ("false", 0)] {
            let is = self.text(out, word.as_bytes());
            let args = [text.to_vec(), is].concat();
            let same = self.run(out, defs.eq, &Ty::Text, args)[0];
            let arms = out.switch(same, 2);
            out.current = arms[1];
            let held = out.int(form::Bits::B8, truth);
            let value = self.some_of(out, ty, vec![held]);
            ends.push((out.current, value));
            out.current = arms[0];
        }
        let none = self.none_of(out, ty);
        ends.push((out.current, none));
        let option = Ty::Enum(defs.option, vec![ty.clone()]);
        out.join(&self.scalars(&option), ends)
    }

    /// `.some` of the number the text spells in decimal, with a `-` before it
    /// where the range has numbers below 0, when the number is in the range,
    /// and `.none` for any other text.
    fn read_whole(
        &mut self,
        out: &mut Out<'a>,
        t: Int,
        text: &[form::Value],
        ty: &Ty,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let (address, len) = (text[0], text[1]);
        let mut ends: Vec<(usize, Vec<form::Value>)> = Vec::new();
        let (zero, one) = (out.int(form::Bits::B64, 0), out.int(form::Bits::B64, 1));
        let load = |out: &mut Out<'a>, at: form::Value| {
            out.emit(
                form::Op::Load {
                    ty: BYTE,
                    address: at,
                },
                vec![BYTE],
            )[0]
        };
        // Where the digits begin, and whether a `-` came first.
        let (start, negative) = if t.min() < 0 {
            let has = out.compare(form::Compare::LessU, zero, len);
            let arms = out.switch(has, 2);
            out.current = arms[0];
            let none = self.none_of(out, ty);
            ends.push((out.current, none));
            out.current = arms[1];
            let first = load(out, address);
            let dash = out.int(form::Bits::B8, i128::from(b'-'));
            let minus = out.compare(form::Compare::Equal, first, dash);
            (out.choose(minus, one, zero), minus)
        } else {
            (zero, out.int(form::Bits::B8, 0))
        };
        // At least one digit.
        let some = out.compare(form::Compare::LessU, start, len);
        let arms = out.switch(some, 2);
        out.current = arms[0];
        let none = self.none_of(out, ty);
        ends.push((out.current, none));
        out.current = arms[1];
        let head = out.new_block();
        let (i, acc) = (
            out.fresh(form::Type::Int(form::Bits::B64)),
            out.fresh(form::Type::Int(form::Bits::B64)),
        );
        out.blocks[head].params = vec![i, acc];
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: head,
            args: vec![start, zero],
        });
        out.current = head;
        let done = out.compare(form::Compare::Equal, i, len);
        let arms = out.switch(done, 2);
        let reading = arms[0];
        // The text ends: the number is in the range, or the text is no number.
        out.current = arms[1];
        let signed = t.signed();
        let below_most = (-t.min()).max(0);
        let most = t.max().max(0);
        let (down, up) = (
            out.int(form::Bits::B64, below_most),
            out.int(form::Bits::B64, most),
        );
        let limit = out.choose(negative, down, up);
        let over = out.compare(form::Compare::LessU, limit, acc);
        let arms = out.switch(over, 2);
        out.current = arms[1];
        let none = self.none_of(out, ty);
        ends.push((out.current, none));
        out.current = arms[0];
        let flipped = out.binary(form::Binary::Sub, zero, acc);
        let value = out.choose(negative, flipped, acc);
        let less = if signed {
            form::Compare::LessS
        } else {
            form::Compare::LessU
        };
        let (least, greatest) = (
            out.int(form::Bits::B64, t.min()),
            out.int(form::Bits::B64, t.max()),
        );
        for (a, b) in [(value, least), (greatest, value)] {
            let outside = out.compare(less, a, b);
            let arms = out.switch(outside, 2);
            out.current = arms[1];
            let none = self.none_of(out, ty);
            ends.push((out.current, none));
            out.current = arms[0];
        }
        let bits = bits_of(t);
        let held = if bits == form::Bits::B64 {
            value
        } else {
            out.narrow(value, bits)
        };
        let found = self.some_of(out, ty, vec![held]);
        ends.push((out.current, found));
        // Another digit.
        out.current = reading;
        let at = out.binary(form::Binary::Add, address, i);
        let byte = load(out, at);
        let ascii_zero = out.int(form::Bits::B8, i128::from(b'0'));
        let digit = out.binary(form::Binary::Sub, byte, ascii_zero);
        let ten_byte = out.int(form::Bits::B8, 10);
        let is_digit = out.compare(form::Compare::LessU, digit, ten_byte);
        let arms = out.switch(is_digit, 2);
        out.current = arms[0];
        let none = self.none_of(out, ty);
        ends.push((out.current, none));
        out.current = arms[1];
        let digit = out.widen(false, digit);
        let ten = out.int(form::Bits::B64, 10);
        let (times, wrapped) = out.overflows(form::Binary::Mul, false, acc, ten);
        let (plus, carried) = out.overflows(form::Binary::Add, false, times, digit);
        let bad = out.binary(form::Binary::Or, wrapped, carried);
        let arms = out.switch(bad, 2);
        out.current = arms[1];
        let none = self.none_of(out, ty);
        ends.push((out.current, none));
        out.current = arms[0];
        let next = out.binary(form::Binary::Add, i, one);
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: head,
            args: vec![next, plus],
        });
        let option = Ty::Enum(defs.option, vec![ty.clone()]);
        out.join(&self.scalars(&option), ends)
    }

    /// The function that describes the type `ty` to a describer of type `d`,
    /// which takes the describer and gives it back. It calls the function of the
    /// describer's `Describer` for each shape of the type, and the part's own
    /// `Describe` for each part, and takes no value, as the type has none.
    fn described(&mut self, ty: &Ty, d: &Ty) -> form::Function {
        let mut out = Out::new();
        let describer = self.scalars(d);
        let to = out.params(&describer);
        let to = self.describe_shape(&mut out, ty, to, d);
        out.blocks[out.current].end = form::End::Return(to);
        form::Function {
            types: out.types,
            results: describer,
            blocks: out.blocks,
            cold: false,
            name: String::new(),
        }
    }

    /// The describer after the type is described to `to` by the `Describe` of
    /// its type, which is the one the type is given or writes itself.
    fn described_part(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        to: Vec<form::Value>,
        d: &Ty,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let describe = defs
            .describe
            .expect("a program that describes has the module");
        let (instance, _) = defs.chosen(describe, 0, vec![ty.clone()], vec![d.clone()]);
        let function = self.place(Body::Copy(instance));
        let types = self.scalars(d);
        out.emit(form::Op::Call { function, args: to }, types)
    }

    /// The describer after the shape of the type is told to it: a whole number
    /// with its least and greatest, text, a struct with its name, its comment,
    /// and each field with its name, its comment, and whether it may be left
    /// out, and an enum with its name, its comment, and each case.
    fn describe_shape(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        to: Vec<form::Value>,
        d: &Ty,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let describer = defs
            .describer
            .expect("a program that describes has the module");
        let call = |this: &mut Self, out: &mut Out<'a>, name: &str, to, args| {
            this.format_call(out, describer, d, name, to, args)
        };
        match ty {
            Ty::Int(t) => {
                let fits = t.min() >= i128::from(i64::MIN) && t.max() <= i128::from(i64::MAX);
                let name = if fits { "signed" } else { "unsigned" };
                let least = out.int(form::Bits::B64, t.min());
                let greatest = out.int(form::Bits::B64, t.max());
                call(self, out, name, to, vec![vec![least], vec![greatest]])
            }
            Ty::Text => call(self, out, "text", to, Vec::new()),
            Ty::Nothing => call(self, out, "nothing", to, Vec::new()),
            Ty::Tuple(of) => {
                let size = self.count(out, of.len());
                let mut to = call(self, out, "tuple", to, vec![size]);
                for t in of {
                    to = self.described_part(out, t, to, d);
                }
                call(self, out, "end_tuple", to, Vec::new())
            }
            Ty::List(of) => {
                let to = call(self, out, "sequence", to, Vec::new());
                let to = self.described_part(out, &of[0], to, d);
                call(self, out, "end_sequence", to, Vec::new())
            }
            Ty::Struct(s, args) => {
                let def = &defs.file.structs[*s];
                let name = self.text(out, crate::check::written_name(&def.name).as_bytes());
                let about = self.text(out, defs.note_of(&def.name).as_bytes());
                let size = self.count(out, def.fields.len());
                let mut to = call(self, out, "structure", to, vec![name, about, size]);
                for (field, t) in def.fields.iter().zip(&defs.fields[*s]) {
                    let t = t.subst(args);
                    let name = self.text(out, field.name.as_bytes());
                    let key = format!("{}.{}", def.name, field.name);
                    let about = self.text(out, defs.note_of(&key).as_bytes());
                    let may = field.default.is_some()
                        || matches!(&t, Ty::Enum(e, _) if *e == defs.option);
                    let may = out.int(form::Bits::B8, i128::from(may));
                    to = call(self, out, "field", to, vec![name, about, vec![may]]);
                    to = self.described_part(out, &t, to, d);
                }
                call(self, out, "end_structure", to, Vec::new())
            }
            Ty::Enum(e, args) => {
                if *e == defs.boolean {
                    return call(self, out, "boolean", to, Vec::new());
                }
                if *e == defs.option {
                    let to = call(self, out, "optional", to, Vec::new());
                    let to = self.described_part(out, &args[0], to, d);
                    return call(self, out, "end_optional", to, Vec::new());
                }
                let def = &defs.file.enums[*e];
                let name = self.text(out, crate::check::written_name(&def.name).as_bytes());
                let about = self.text(out, defs.note_of(&def.name).as_bytes());
                let size = self.count(out, def.cases.len());
                let mut to = call(self, out, "enumeration", to, vec![name, about, size]);
                for (case, holds) in def.cases.iter().zip(&defs.cases[*e]) {
                    let name = self.text(out, case.name.as_bytes());
                    let key = format!("{}.{}", def.name, case.name);
                    let about = self.text(out, defs.note_of(&key).as_bytes());
                    let held = out.int(form::Bits::B8, i128::from(holds.is_some()));
                    to = call(self, out, "case", to, vec![name, about, vec![held]]);
                    if let Some(t) = holds {
                        to = self.described_part(out, &t.subst(args), to, d);
                    }
                    to = call(self, out, "end_case", to, Vec::new());
                }
                call(self, out, "end_enumeration", to, Vec::new())
            }
            _ => unreachable!("a type given `Describe`"),
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
        let writer = self
            .typed
            .defs
            .writer
            .expect("a program that writes has the module");
        self.format_call(out, writer, w, name, to, args)
    }

    /// Calls the function `name` of trait `k`, which the format `w` has, with
    /// the format and then `args`, and gives the format back.
    fn format_call(
        &mut self,
        out: &mut Out<'a>,
        k: usize,
        w: &Ty,
        name: &str,
        to: Vec<form::Value>,
        args: Vec<Vec<form::Value>>,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let at = crate::check::traits::trait_function(&defs.file.traits[k], name)
            .expect("a function of the format's trait");
        let (instance, _) = defs.chosen(k, at, vec![w.clone()], Vec::new());
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
            Ty::List(of) | Ty::Slice(of) => return self.equal_elements(out, &of[0], a, b),
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

    /// Whether two lists or slices, whose addresses and sizes are the first
    /// two of `a` and of `b`, have the same size and equal elements in order,
    /// each by its type's `Eq`, stopping at the first that differ.
    fn equal_elements(
        &mut self,
        out: &mut Out<'a>,
        element: &Ty,
        a: &[form::Value],
        b: &[form::Value],
    ) -> form::Value {
        let defs = self.typed.defs;
        let bool_types = self.scalars(&defs.bool_ty());
        let scalars = self.scalars(element);
        let (places, stride) = form::element(&scalars);
        let places: Vec<(form::Type, u64)> = scalars.into_iter().zip(places).collect();
        let stride = out.int(form::Bits::B64, i128::from(stride));
        let zero = out.int(form::Bits::B64, 0);
        let mut ends = Vec::new();
        let same_size = out.compare(form::Compare::Equal, a[1], b[1]);
        let arms = out.switch(same_size, 2);
        out.current = arms[0];
        let no = self.truth(out, false);
        ends.push((out.current, vec![no]));
        out.current = arms[1];
        let header = out.new_block();
        let i = out.fresh(WORD);
        out.blocks[header].params = vec![i];
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: vec![zero],
        });
        out.current = header;
        let more = out.compare(form::Compare::LessU, i, a[1]);
        let arms = out.switch(more, 2);
        out.current = arms[0];
        let yes = self.truth(out, true);
        ends.push((out.current, vec![yes]));
        out.current = arms[1];
        let offset = out.binary(form::Binary::Mul, i, stride);
        let (p, q) = (
            out.binary(form::Binary::Add, a[0], offset),
            out.binary(form::Binary::Add, b[0], offset),
        );
        let (x, y) = (self.load(out, p, &places), self.load(out, q, &places));
        let same = self.run(out, defs.eq, element, [x, y].concat())[0];
        let arms = out.switch(same, 2);
        let (no_at, yes_at) = (
            defs.case_of(defs.boolean, "false").expect("a case"),
            defs.case_of(defs.boolean, "true").expect("a case"),
        );
        out.current = arms[no_at];
        let no = self.truth(out, false);
        ends.push((out.current, vec![no]));
        out.current = arms[yes_at];
        let one = out.int(form::Bits::B64, 1);
        let next = out.binary(form::Binary::Add, i, one);
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: vec![next],
        });
        out.join(&bool_types, ends)[0]
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
            Ty::List(of) | Ty::Slice(of) => self.repr_elements(out, &of[0], values),
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

    /// The text `Repr` gives for a list or a slice, whose address and size are
    /// the first two of `values`: `[`, the `Repr` of each element with `, `
    /// after it, which the last loses, and `]`. The text is built in one list
    /// of bytes that grows as `push` does, and not joined again for each
    /// element.
    fn repr_elements(
        &mut self,
        out: &mut Out<'a>,
        element: &Ty,
        values: &[form::Value],
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let scalars = self.scalars(element);
        let (places, stride) = form::element(&scalars);
        let places: Vec<(form::Type, u64)> = scalars.into_iter().zip(places).collect();
        let stride = out.int(form::Bits::B64, i128::from(stride));
        let zero = out.int(form::Bits::B64, 0);
        let (address, size) = (values[0], values[1]);
        let bytes = self.scalars(&Ty::List(vec![Ty::Int(Int::U8)]));
        let kind = Runtime::Append(vec![BYTE]);
        let none = out.constant(form::Constant::Null, form::Type::Address);
        let open = self.text(out, b"[");
        let empty = vec![none, zero, zero];
        let start = self.call_runtime(out, kind.clone(), [empty, open].concat(), bytes.clone());
        let header = out.new_block();
        let i = out.fresh(WORD);
        let carried: Vec<form::Value> = bytes.iter().map(|&t| out.fresh(t)).collect();
        out.blocks[header].params = std::iter::once(i).chain(carried.iter().copied()).collect();
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: std::iter::once(zero).chain(start).collect(),
        });
        out.current = header;
        let more = out.compare(form::Compare::LessU, i, size);
        let arms = out.switch(more, 2);
        out.current = arms[1];
        let offset = out.binary(form::Binary::Mul, i, stride);
        let at = out.binary(form::Binary::Add, address, offset);
        let part = self.load(out, at, &places);
        let text = self.run(out, defs.repr, element, part);
        let grown = self.call_runtime(
            out,
            kind.clone(),
            [carried.clone(), text].concat(),
            bytes.clone(),
        );
        let comma = self.text(out, b", ");
        let next = self.call_runtime(out, kind.clone(), [grown, comma].concat(), bytes.clone());
        let one = out.int(form::Bits::B64, 1);
        let after = out.binary(form::Binary::Add, i, one);
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: std::iter::once(after).chain(next).collect(),
        });
        out.current = arms[0];
        // A list that has elements has a `, ` too many at its end.
        let none_in = out.compare(form::Compare::Equal, size, zero);
        let two = out.int(form::Bits::B64, 2);
        let cut = out.choose(none_in, zero, two);
        let kept = out.binary(form::Binary::Sub, carried[1], cut);
        let close = self.text(out, b"]");
        let list = vec![carried[0], kept, carried[2]];
        let done = self.call_runtime(out, kind, [list, close].concat(), bytes);
        done[..2].to_vec()
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

/// What reading one value shares: the type of the whole value, which the
/// error of a read that stops is given back in, and the type of the reader.
struct Read {
    top: Ty,
    reader: Ty,
}

impl<'a> Lowering<'a> {
    /// The function that reads a value of `ty` from a reader of type `r`,
    /// which takes the reader and gives it back with the value, or with what
    /// was wrong. It calls the function of the reader's `Reader` for each
    /// shape of the value, and the part's own `Deserialize` for each part,
    /// and builds nothing between. A read that finds something wrong ends
    /// there, with the reader as it was left.
    fn deserialized(&mut self, ty: &Ty, r: &Ty) -> form::Function {
        let mut out = Out::new();
        let reader = self.scalars(r);
        let params = out.params(&reader);
        let c = Read {
            top: ty.clone(),
            reader: r.clone(),
        };
        let (reader_now, values) = self.read_shape(&mut out, &c, ty, params);
        let result = self.result_ty(ty);
        let ok = self.enum_value(&mut out, &result, "ok", values);
        out.blocks[out.current].end = form::End::Return([reader_now, ok].concat());
        let mut results = reader;
        results.extend(self.scalars(&result));
        form::Function {
            types: out.types,
            results,
            blocks: out.blocks,
            cold: false,
            name: String::new(),
        }
    }

    /// `Result[ok, ReadError]`.
    fn result_ty(&self, ok: &Ty) -> Ty {
        let defs = self.typed.defs;
        Ty::Enum(defs.result, vec![ok.clone(), self.read_error()])
    }

    fn read_error(&self) -> Ty {
        let at = (self.typed.defs.read_error).expect("a program that reads has the module");
        Ty::Struct(at, Vec::new())
    }

    fn problem_ty(&self) -> Ty {
        let at = (self.typed.defs.problem).expect("a program that reads has the module");
        Ty::Enum(at, Vec::new())
    }

    /// The values of the case `name` of the enum `ty`, holding `held`.
    fn enum_value(
        &mut self,
        out: &mut Out<'a>,
        ty: &Ty,
        name: &str,
        held: Vec<form::Value>,
    ) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let Ty::Enum(e, args) = ty else {
            unreachable!("an enum")
        };
        let k = defs.case_of(*e, name).expect("a case");
        let layout = self.layout(*e, args);
        let mut places: Vec<Option<form::Value>> = vec![None; layout.places.len()];
        for (v, &place) in held.iter().zip(&layout.cases[k]) {
            places[place] = Some(*v);
        }
        let case = out.constant(form::Constant::Int(k as u64), BYTE);
        let mut values = vec![case];
        for (place, ty) in places.into_iter().zip(&layout.places) {
            values.push(place.unwrap_or_else(|| out.zero(*ty)));
        }
        values
    }

    /// What the case `name` of the enum `ty` holds, from the values of the
    /// enum.
    fn held(&self, ty: &Ty, name: &str, values: &[form::Value]) -> Vec<form::Value> {
        let defs = self.typed.defs;
        let Ty::Enum(e, args) = ty else {
            unreachable!("an enum")
        };
        let k = defs.case_of(*e, name).expect("a case");
        let layout = self.layout(*e, args);
        layout.cases[k].iter().map(|&p| values[1 + p]).collect()
    }

    /// Calls the function `name` of the reader's `Reader`, which is given the
    /// reader and then `args`, and gives the reader and what the function
    /// gives, which is of type `gives`.
    fn reader_call(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        name: &str,
        reader: Vec<form::Value>,
        args: Vec<Vec<form::Value>>,
        gives: &Ty,
    ) -> (Vec<form::Value>, Vec<form::Value>) {
        let defs = self.typed.defs;
        let k = defs.reader.expect("a program that reads has the module");
        let at = crate::check::traits::trait_function(&defs.file.traits[k], name)
            .expect("a function of `Reader`");
        let (instance, _) = defs.chosen(k, at, vec![c.reader.clone()], Vec::new());
        let function = self.place(Body::Copy(instance));
        let mut types = self.scalars(&c.reader);
        types.extend(self.scalars(gives));
        let args = std::iter::once(reader).chain(args).flatten().collect();
        let mut all = out.emit(form::Op::Call { function, args }, types);
        let value = all.split_off(self.size(&c.reader));
        (all, value)
    }

    /// Ends the block being filled by giving back the reader with the error.
    fn give_error(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        reader: Vec<form::Value>,
        error: Vec<form::Value>,
    ) {
        let result = self.result_ty(&c.top);
        let err = self.enum_value(out, &result, "err", error);
        out.blocks[out.current].end = form::End::Return([reader, err].concat());
    }

    /// Ends the block being filled with the error the reader makes of the
    /// `Problem` that is the case `name` holding `held`.
    fn failure(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        reader: Vec<form::Value>,
        name: &str,
        held: Vec<form::Value>,
    ) {
        let problem = self.problem_ty();
        let problem = self.enum_value(out, &problem, name, held);
        let error = self.read_error();
        let (reader, error) = self.reader_call(out, c, "fail", reader, vec![problem], &error);
        self.give_error(out, c, reader, error);
    }

    /// Goes on in the block of the `ok` of what a function of the reader gave,
    /// which holds a value of type `ok`, and gives the value. The block of the
    /// `err` ends the read.
    fn proceed(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        ok: &Ty,
        reader: Vec<form::Value>,
        gave: Vec<form::Value>,
    ) -> Vec<form::Value> {
        let result = self.result_ty(ok);
        let arms = out.switch(gave[0], 2);
        out.current = arms[1];
        let error = self.held(&result, "err", &gave);
        self.give_error(out, c, reader, error);
        out.current = arms[0];
        self.held(&result, "ok", &gave)
    }

    /// Calls a function of the reader that gives `Result[ok, ReadError]`,
    /// and goes on with the `ok` of it.
    fn reader_step(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        name: &str,
        reader: Vec<form::Value>,
        args: Vec<Vec<form::Value>>,
        ok: &Ty,
    ) -> (Vec<form::Value>, Vec<form::Value>) {
        let result = self.result_ty(ok);
        let (reader, gave) = self.reader_call(out, c, name, reader, args, &result);
        let value = self.proceed(out, c, ok, reader.clone(), gave);
        (reader, value)
    }

    /// Reads a part, by the `Deserialize` its type is given or writes itself.
    fn read_part(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        ty: &Ty,
        reader: Vec<form::Value>,
    ) -> (Vec<form::Value>, Vec<form::Value>) {
        let defs = self.typed.defs;
        let k = defs
            .deserialize
            .expect("a program that reads has the module");
        let (instance, _) = defs.chosen(k, 0, vec![ty.clone()], vec![c.reader.clone()]);
        let function = self.place(Body::Copy(instance));
        let result = self.result_ty(ty);
        let mut types = self.scalars(&c.reader);
        types.extend(self.scalars(&result));
        let mut all = out.emit(
            form::Op::Call {
                function,
                args: reader,
            },
            types,
        );
        let gave = all.split_off(self.size(&c.reader));
        let value = self.proceed(out, c, ty, all.clone(), gave);
        (all, value)
    }

    /// The reader after a value of the type is read from it, and the value,
    /// by the shape of the type: a number as `signed` or `unsigned`, text as
    /// `text`, a struct as its name, the name and value of each field it
    /// holds, and a list as its elements for as long as the reader has more.
    fn read_shape(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        ty: &Ty,
        reader: Vec<form::Value>,
    ) -> (Vec<form::Value>, Vec<form::Value>) {
        let defs = self.typed.defs;
        match ty {
            Ty::Int(t) => {
                let signed = t.signed();
                let wide = Ty::Int(if signed { Int::I64 } else { Int::U64 });
                let name = if signed { "signed" } else { "unsigned" };
                let (reader, value) = self.reader_step(out, c, name, reader, Vec::new(), &wide);
                let bits = bits_of(*t);
                // A range that is not the whole of the size it is kept at holds
                // the numbers from its least to its greatest, and a number
                // outside them is not read, whatever the size holds.
                if *t != t.holder() {
                    let less = if signed {
                        form::Compare::LessS
                    } else {
                        form::Compare::LessU
                    };
                    let least = out.int(form::Bits::B64, t.min());
                    let greatest = out.int(form::Bits::B64, t.max());
                    let below = out.compare(less, value[0], least);
                    let arms = out.switch(below, 2);
                    out.current = arms[1];
                    let name = self.text(out, t.name().as_bytes());
                    self.failure(out, c, reader.clone(), "range", name);
                    out.current = arms[0];
                    let above = out.compare(less, greatest, value[0]);
                    let arms = out.switch(above, 2);
                    out.current = arms[1];
                    let name = self.text(out, t.name().as_bytes());
                    self.failure(out, c, reader.clone(), "range", name);
                    out.current = arms[0];
                    if bits == form::Bits::B64 {
                        return (reader, value);
                    }
                    let narrow = out.narrow(value[0], bits);
                    return (reader, vec![narrow]);
                }
                if bits == form::Bits::B64 {
                    return (reader, value);
                }
                let narrow = out.narrow(value[0], bits);
                let back = out.resize(signed, narrow, form::Bits::B64);
                let fits = out.compare(form::Compare::Equal, back, value[0]);
                let arms = out.switch(fits, 2);
                out.current = arms[0];
                let name = self.text(out, t.name().as_bytes());
                self.failure(out, c, reader.clone(), "range", name);
                out.current = arms[1];
                (reader, vec![narrow])
            }
            Ty::Text => self.reader_step(out, c, "text", reader, Vec::new(), ty),
            Ty::Nothing => self.reader_step(out, c, "nothing", reader, Vec::new(), ty),
            Ty::Tuple(of) => {
                let size = self.count(out, of.len());
                let (mut reader, _) =
                    self.reader_step(out, c, "tuple", reader, vec![size], &Ty::Nothing);
                let mut values = Vec::new();
                for t in of {
                    let (next, part) = self.read_part(out, c, t, reader);
                    reader = next;
                    values.extend(part);
                }
                let (reader, _) =
                    self.reader_step(out, c, "end_tuple", reader, Vec::new(), &Ty::Nothing);
                (reader, values)
            }
            Ty::List(of) => self.read_list(out, c, &of[0], reader),
            Ty::Struct(s, args) => self.read_struct(out, c, *s, args, reader),
            Ty::Enum(e, _) if *e == defs.boolean => {
                self.reader_step(out, c, "boolean", reader, Vec::new(), ty)
            }
            Ty::Enum(e, args) if *e == defs.option => {
                let (reader, present) =
                    self.reader_step(out, c, "present", reader, Vec::new(), &defs.bool_ty());
                let arms = out.switch(present[0], 2);
                out.current = arms[0];
                let none = self.enum_value(out, ty, "none", Vec::new());
                let mut ends = vec![(out.current, [reader.clone(), none].concat())];
                out.current = arms[1];
                let (after, inner) = self.read_part(out, c, &args[0], reader);
                let some = self.enum_value(out, ty, "some", inner);
                ends.push((out.current, [after, some].concat()));
                let mut types = self.scalars(&c.reader);
                types.extend(self.scalars(ty));
                let mut all = out.join(&types, ends);
                let value = all.split_off(self.size(&c.reader));
                (all, value)
            }
            Ty::Enum(e, args) => {
                let def = &defs.file.enums[*e];
                let of = self.text(out, crate::check::written_name(&def.name).as_bytes());
                let (reader, name) = self.reader_step(out, c, "case", reader, vec![of], &Ty::Text);
                let mut ends = Vec::new();
                for (case, held) in def.cases.iter().zip(&defs.cases[*e]) {
                    let literal = self.text(out, case.name.as_bytes());
                    let same = self.call_runtime(
                        out,
                        Runtime::Equal,
                        [name.clone(), literal].concat(),
                        vec![BYTE],
                    )[0];
                    let arms = out.switch(same, 2);
                    out.current = arms[1];
                    let (after, inside) = match held {
                        Some(t) => self.read_part(out, c, &t.subst(args), reader.clone()),
                        None => (reader.clone(), Vec::new()),
                    };
                    let (after, _) =
                        self.reader_step(out, c, "end_case", after, Vec::new(), &Ty::Nothing);
                    let value = self.enum_value(out, ty, &case.name, inside);
                    ends.push((out.current, [after, value].concat()));
                    out.current = arms[0];
                }
                self.failure(out, c, reader, "case", name);
                let mut types = self.scalars(&c.reader);
                types.extend(self.scalars(ty));
                let mut all = out.join(&types, ends);
                let value = all.split_off(self.size(&c.reader));
                (all, value)
            }
            _ => unreachable!("a type given `Deserialize`"),
        }
    }

    /// The reader after a list of `element` is read from it, and the list.
    fn read_list(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        element: &Ty,
        reader: Vec<form::Value>,
    ) -> (Vec<form::Value>, Vec<form::Value>) {
        let (reader, _) = self.reader_step(out, c, "sequence", reader, Vec::new(), &Ty::Nothing);
        let list = Ty::List(vec![element.clone()]);
        let kinds = self.scalars(&c.reader);
        let header = out.new_block();
        let params: Vec<form::Value> = (kinds.iter())
            .chain(&self.scalars(&list))
            .map(|t| out.fresh(*t))
            .collect();
        out.blocks[header].params = params.clone();
        let none = out.constant(form::Constant::Null, form::Type::Address);
        let zero = out.int(form::Bits::B64, 0);
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: [reader, vec![none, zero, zero]].concat(),
        });
        out.current = header;
        let (reader, rest) = params.split_at(kinds.len());
        let (reader, rest) = (reader.to_vec(), rest.to_vec());
        let (reader, more) = self.reader_step(out, c, "more", reader, Vec::new(), &defs_bool(self));
        let arms = out.switch(more[0], 2);
        out.current = arms[1];
        let (after, item) = self.read_part(out, c, element, reader.clone());
        let kind = Runtime::Push(self.scalars(element));
        let grown = self.call_runtime(
            out,
            kind,
            [rest.clone(), item].concat(),
            self.scalars(&list),
        );
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: [after, grown].concat(),
        });
        out.current = arms[0];
        (reader, rest)
    }

    /// The reader after a struct is read from it, and the struct. The fields
    /// come in any order, each at most once, and what is left out is its
    /// default, or `.none` for an `Option`, or an error that names it.
    fn read_struct(
        &mut self,
        out: &mut Out<'a>,
        c: &Read,
        s: usize,
        args: &[Ty],
        reader: Vec<form::Value>,
    ) -> (Vec<form::Value>, Vec<form::Value>) {
        let defs = self.typed.defs;
        let def = &defs.file.structs[s];
        let types: Vec<Ty> = defs.fields[s].iter().map(|t| t.subst(args)).collect();
        let name = self.text(out, crate::check::written_name(&def.name).as_bytes());
        let (reader, _) = self.reader_step(out, c, "structure", reader, vec![name], &Ty::Nothing);
        // The loop carries the reader, and for each field whether it was
        // read and what it was.
        let kinds = self.scalars(&c.reader);
        let mut carried = kinds.clone();
        for t in &types {
            carried.push(BYTE);
            carried.extend(self.scalars(t));
        }
        let header = out.new_block();
        let params: Vec<form::Value> = carried.iter().map(|t| out.fresh(*t)).collect();
        out.blocks[header].params = params.clone();
        let start: Vec<form::Value> = carried.iter().map(|t| out.zero(*t)).collect();
        let start = [reader, start[kinds.len()..].to_vec()].concat();
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: start,
        });
        out.current = header;
        let now = params[..kinds.len()].to_vec();
        let mut state: Vec<(form::Value, Vec<form::Value>)> = Vec::new();
        let mut at = kinds.len();
        for t in &types {
            let size = self.size(t);
            state.push((params[at], params[at + 1..at + 1 + size].to_vec()));
            at += 1 + size;
        }
        let option = Ty::Enum(defs.option, vec![Ty::Text]);
        let (reader, next) = self.reader_step(out, c, "field", now, Vec::new(), &option);
        let arms = out.switch(next[0], 2);
        let (some, none) = (
            defs.case_of(defs.option, "some").expect("a case"),
            defs.case_of(defs.option, "none").expect("a case"),
        );
        // A field the input holds.
        out.current = arms[some];
        let given = self.held(&option, "some", &next);
        for (i, field) in def.fields.iter().enumerate() {
            let literal = self.text(out, field.name.as_bytes());
            let same = self.call_runtime(
                out,
                Runtime::Equal,
                [given.clone(), literal.clone()].concat(),
                vec![BYTE],
            )[0];
            let found = out.switch(same, 2);
            out.current = found[1];
            let seen = out.switch(state[i].0, 2);
            out.current = seen[1];
            self.failure(out, c, reader.clone(), "twice", literal);
            out.current = seen[0];
            let (after, value) = self.read_part(out, c, &types[i], reader.clone());
            let one = out.int(form::Bits::B8, 1);
            let mut jump = after;
            for (j, (flag, held)) in state.iter().enumerate() {
                if j == i {
                    jump.push(one);
                    jump.extend(value.iter().copied());
                } else {
                    jump.push(*flag);
                    jump.extend(held.iter().copied());
                }
            }
            out.blocks[out.current].end = form::End::Jump(form::Jump {
                block: header,
                args: jump,
            });
            out.current = found[0];
        }
        let (after, _) =
            self.reader_step(out, c, "unknown", reader.clone(), vec![given], &Ty::Nothing);
        let mut jump = after;
        for (flag, held) in &state {
            jump.push(*flag);
            jump.extend(held.iter().copied());
        }
        out.blocks[out.current].end = form::End::Jump(form::Jump {
            block: header,
            args: jump,
        });
        // The input has no more fields.
        out.current = arms[none];
        let mut values = Vec::new();
        for (i, field) in def.fields.iter().enumerate() {
            let seen = out.switch(state[i].0, 2);
            let mut ends = vec![(seen[1], state[i].1.clone())];
            out.current = seen[0];
            let left_out = match &field.default {
                Some(default) => Some(self.expr(out, default, args).1),
                None if matches!(&types[i], Ty::Enum(e, _) if *e == defs.option) => {
                    Some(self.enum_value(out, &types[i], "none", Vec::new()))
                }
                None => None,
            };
            match left_out {
                Some(value) => ends.push((out.current, value)),
                None => {
                    let literal = self.text(out, field.name.as_bytes());
                    self.failure(out, c, reader.clone(), "missing", literal);
                }
            }
            values.extend(out.join(&self.scalars(&types[i]), ends));
        }
        (reader, values)
    }
}

fn defs_bool(l: &Lowering) -> Ty {
    l.typed.defs.bool_ty()
}
