//! A model of DER, which shares nothing with `std/der.smith`, a builder that
//! writes values as DER, and the program that prints what `value` and `children`
//! give for bytes.

use smith::{lower, scalar};

use crate::generate::Gen;

// The model of DER.

/// A value as the model reads it: its tag as the library names it, where its
/// contents are, and where the next value begins.
pub struct Read {
    pub tag: String,
    /// Where the tag is, which is not where the contents begin.
    pub from: usize,
    pub start: usize,
    pub end: usize,
    pub container: bool,
}

/// What DER says of the length that begins at `at`, and where the contents
/// begin.
pub fn model_length(b: &[u8], at: usize) -> Result<(u64, usize), ()> {
    let first = *b.get(at).ok_or(())?;
    if first < 128 {
        return Ok((u64::from(first), at + 1));
    }
    if first == 128 {
        return Err(());
    }
    let count = usize::from(first - 128);
    if count > 8 || at + 1 + count > b.len() || b[at + 1] == 0 {
        return Err(());
    }
    let n = b[at + 1..at + 1 + count]
        .iter()
        .fold(0u64, |n, byte| (n << 8) | u64::from(*byte));
    if n < 128 {
        return Err(());
    }
    Ok((n, at + 1 + count))
}

pub fn model_read(b: &[u8]) -> Result<Read, ()> {
    let id = *b.first().ok_or(())?;
    let number = id & 31;
    let constructed = id & 32 != 0;
    let class = id >> 6;
    if number == 31 {
        return Err(());
    }
    let (tag, container) = if class == 2 {
        (format!("context_{number}_{constructed}"), constructed)
    } else if class != 0 {
        (format!("other_{id}"), false)
    } else if number == 16 || number == 17 {
        if !constructed {
            return Err(());
        }
        (if number == 16 { "sequence" } else { "set" }.to_string(), true)
    } else if constructed {
        (format!("other_{id}"), false)
    } else {
        let name = match number {
            1 => "boolean",
            2 => "integer",
            3 => "bits",
            4 => "octets",
            5 => "null",
            6 => "object",
            12 => "utf8",
            19 => "printable",
            22 => "ia5",
            23 => "utc_time",
            24 => "general_time",
            _ => "",
        };
        if name.is_empty() {
            (format!("other_{id}"), false)
        } else {
            (name.to_string(), false)
        }
    };
    let (len, start) = model_length(b, 1)?;
    if len > (b.len() - start) as u64 {
        return Err(());
    }
    let end = start + len as usize;
    let c = &b[start..end];
    let ok = match tag.as_str() {
        "boolean" => c.len() == 1 && (c[0] == 0 || c[0] == 255),
        "integer" => {
            !c.is_empty()
                && !(c.len() > 1 && ((c[0] == 0 && c[1] < 128) || (c[0] == 255 && c[1] >= 128)))
        }
        "null" => c.is_empty(),
        "object" => !c.is_empty(),
        _ => true,
    };
    if !ok {
        return Err(());
    }
    Ok(Read { tag, from: 0, start, end, container })
}

/// The parts of the contents of a value that holds parts.
pub fn model_children(c: &[u8]) -> Result<Vec<Read>, ()> {
    let mut at = 0;
    let mut parts = Vec::new();
    while at < c.len() {
        let mut part = model_read(&c[at..])?;
        part.from += at;
        part.start += at;
        part.end += at;
        at = part.end;
        parts.push(part);
    }
    Ok(parts)
}

/// The line the program prints for the bytes, written from the model.
pub fn model_line(b: &[u8]) -> String {
    let Ok(v) = model_read(b) else {
        return "err".to_string();
    };
    let mut line = format!("ok {} {} {}", v.tag, v.end - v.start, b.len() - v.end);
    if v.container {
        match model_children(&b[v.start..v.end]) {
            Err(()) => line += " kids-err",
            Ok(parts) => {
                let items: Vec<String> = parts
                    .iter()
                    .map(|p| {
                        let mut item = format!("{}:{}", p.tag, p.end - p.start);
                        if p.container {
                            let inside = &b[v.start..v.end][p.start..p.end];
                            match model_children(inside) {
                                Ok(kids) => item += &format!("/{}", kids.len()),
                                Err(()) => item += "/x",
                            }
                        }
                        item
                    })
                    .collect();
                line += &format!(" [{}]", items.join(" "));
            }
        }
    }
    line
}

// The builder of DER.

pub fn length(n: usize) -> Vec<u8> {
    if n < 128 {
        vec![n as u8]
    } else {
        let bytes: Vec<u8> = n.to_be_bytes().into_iter().skip_while(|b| *b == 0).collect();
        let mut out = vec![128 + bytes.len() as u8];
        out.extend(bytes);
        out
    }
}

pub fn tlv(id: u8, contents: &[u8]) -> Vec<u8> {
    let mut out = vec![id];
    out.extend(length(contents.len()));
    out.extend(contents);
    out
}

/// A value of DER that the generator builds, and the bytes of it.
#[derive(Clone, Debug)]
pub enum Node {
    Leaf(u8, Vec<u8>),
    Parts(u8, Vec<Node>),
}

impl Node {
    pub fn encode(&self) -> Vec<u8> {
        match self {
            Node::Leaf(id, c) => tlv(*id, c),
            Node::Parts(id, parts) => {
                let c: Vec<u8> = parts.iter().flat_map(Node::encode).collect();
                tlv(*id, &c)
            }
        }
    }
}

pub fn bytes(g: &mut Gen, most: usize) -> Vec<u8> {
    let n = g.below(most + 1);
    (0..n).map(|_| g.below(256) as u8).collect()
}

/// A number written as DER writes an integer.
pub fn integer(g: &mut Gen) -> Vec<u8> {
    let n = 1 + g.below(6);
    let mut c: Vec<u8> = (0..n).map(|_| g.below(256) as u8).collect();
    while c.len() > 1 && ((c[0] == 0 && c[1] < 128) || (c[0] == 255 && c[1] >= 128)) {
        c.remove(0);
    }
    c
}

/// An object identifier, in the one way DER writes it.
pub fn object(g: &mut Gen) -> Vec<u8> {
    let mut c = vec![(g.below(3) * 40 + g.below(40)) as u8];
    for _ in 0..g.below(5) {
        let n = g.below(1 << 20);
        let mut groups = vec![(n & 127) as u8];
        let mut rest = n >> 7;
        while rest > 0 {
            groups.push(128 | (rest & 127) as u8);
            rest >>= 7;
        }
        groups.reverse();
        c.extend(groups);
    }
    c
}

pub fn leaf(g: &mut Gen) -> Node {
    match g.below(13) {
        0 => Node::Leaf(1, vec![if g.one_in(2) { 255 } else { 0 }]),
        1 => Node::Leaf(2, integer(g)),
        2 => Node::Leaf(3, {
            let mut c = vec![g.below(8) as u8];
            c.extend(bytes(g, 20));
            c
        }),
        3 => Node::Leaf(4, bytes(g, 200)),
        4 => Node::Leaf(5, vec![]),
        5 => Node::Leaf(6, object(g)),
        6 => Node::Leaf(12, text(g, 30)),
        7 => Node::Leaf(19, text(g, 30)),
        8 => Node::Leaf(22, text(g, 30)),
        9 => Node::Leaf(23, b"240229235959Z".to_vec()),
        10 => Node::Leaf(24, b"20240229235959Z".to_vec()),
        // A context value that holds bytes, an application value, and a
        // universal value that the library gives as another one.
        11 => Node::Leaf(0x80 | g.below(31) as u8, bytes(g, 40)),
        _ => Node::Leaf(*g.pick(&[0x41, 0xc3, 0x0a, 0x1e]), bytes(g, 40)),
    }
}

pub fn text(g: &mut Gen, most: usize) -> Vec<u8> {
    let n = g.below(most + 1);
    (0..n).map(|_| 32 + g.below(95) as u8).collect()
}

pub fn node(g: &mut Gen, depth: usize) -> Node {
    if depth == 0 || g.below(3) == 0 {
        return leaf(g);
    }
    let id = *g.pick(&[0x30, 0x31, 0xa0, 0xa1, 0xbe]);
    let parts = (0..g.below(5)).map(|_| node(g, depth - 1)).collect();
    Node::Parts(id, parts)
}

// The programs.

/// Source that prints, for each input, what `value` and `children` give.
pub fn values_program(inputs: &[Vec<u8>]) -> String {
    let mut source = String::from(
        "use std.console (console, print)
use std.der (Tag, Value, children, value)

fn name(t: Tag) -> str ! alloc {
  match t {
    .boolean { \"boolean\" }
    .integer { \"integer\" }
    .bits { \"bits\" }
    .octets { \"octets\" }
    .null { \"null\" }
    .object { \"object\" }
    .utf8 { \"utf8\" }
    .sequence { \"sequence\" }
    .set { \"set\" }
    .printable { \"printable\" }
    .ia5 { \"ia5\" }
    .utc_time { \"utc_time\" }
    .general_time { \"general_time\" }
    .context((n, built)) { \"context_{n}_{built}\" }
    .other(id) { \"other_{id}\" }
  }
}

fn holds(t: Tag) -> bool {
  match t {
    .sequence { true }
    .set { true }
    .context((n, built)) { built }
    _ { false }
  }
}

fn count(v: Value) -> str ! alloc {
  match children(v) {
    .ok(cs) { \"{cs.size}\" }
    .err(e) { \"x\" }
  }
}

fn show(b: Slice[Byte]) ! console, alloc {
  match value(b) {
    .err(e) { print(\"err\") }
    .ok((v, rest)) {
      head = \"ok {name(v.tag)} {v.bytes.size} {rest.size}\"
      if holds(v.tag) {
        match children(v) {
          .err(e) { print(\"{head} kids-err\") }
          .ok(cs) {
            var items = \"\"
            loop i in 0..cs.size {
              c = cs[i]
              sub = if holds(c.tag) { \"/{count(c)}\" } else { \"\" }
              sep = if i == 0 { \"\" } else { \" \" }
              items = \"{items}{sep}{name(c.tag)}:{c.bytes.size}{sub}\"
            }
            print(\"{head} [{items}]\")
          }
        }
      } else {
        print(head)
      }
    }
  }
}

",
    );
    for (i, input) in inputs.iter().enumerate() {
        source += &format!("fn input{i}() -> Slice[Byte] {{ {} }}\n\n", literal(input));
    }
    source += "fn main() ! console, alloc {\n";
    for i in 0..inputs.len() {
        source += &format!("  show(input{i}())\n");
    }
    source + "}\n"
}

/// An array of whole numbers written out.
pub fn literal(b: &[u8]) -> String {
    let items: Vec<String> = b.iter().map(u8::to_string).collect();
    format!("[{}]", items.join(", "))
}

pub fn run(source: &str) -> Vec<String> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    let out = scalar::run(&program).stdout;
    String::from_utf8(out).unwrap().lines().map(str::to_string).collect()
}
