//! What the published text of an RFC holds, read from `reference/rfc/` so that
//! a test checks a module against the standard and not against a copy of it:
//! the tables of RFC 7541 and RFC 9113 and the examples of RFC 7541 appendix C.

use std::sync::OnceLock;

/// The text of `reference/rfc/<name>.txt`, with the lines a page break adds
/// taken out, as lines.
fn lines(name: &str) -> Vec<String> {
    let path = format!("{}/../reference/rfc/{name}.txt", env!("CARGO_MANIFEST_DIR"));
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{path}: {e}"));
    text.replace('\u{c}', "")
        .lines()
        .filter(|l| !l.starts_with("Peon & Ruellan") && !(l.starts_with("RFC 7541") && l.contains("HPACK")))
        .filter(|l| !(l.starts_with("Peon") || l.starts_with("RFC 9113") && l.contains("HTTP/2")))
        .map(str::to_string)
        .collect()
}

/// The lines from the one that starts with `from` to the one before the line
/// that starts with `to`.
fn between<'a>(all: &'a [String], from: &str, to: &str) -> &'a [String] {
    let start = all.iter().position(|l| l.starts_with(from)).unwrap_or_else(|| panic!("no `{from}`"));
    let end = all[start + 1..]
        .iter()
        .position(|l| l.starts_with(to))
        .unwrap_or_else(|| panic!("no `{to}` after `{from}`"));
    &all[start + 1..start + 1 + end]
}

/// The static table of RFC 7541 appendix A, from the index 1.
pub fn static_table() -> &'static [(String, String)] {
    static TABLE: OnceLock<Vec<(String, String)>> = OnceLock::new();
    TABLE.get_or_init(|| {
        let all = lines("rfc7541");
        let mut table = Vec::new();
        for l in between(&all, "Appendix A.", "Appendix B.") {
            let cells: Vec<&str> = l.trim().trim_matches('|').split('|').map(str::trim).collect();
            if cells.len() != 3 {
                continue;
            }
            let Ok(index) = cells[0].parse::<usize>() else { continue };
            assert_eq!(index, table.len() + 1, "{l}");
            table.push((cells[1].to_string(), cells[2].to_string()));
        }
        assert_eq!(table.len(), 61);
        table
    })
}

/// The Huffman code of RFC 7541 appendix B: the code and its length in bits,
/// for each byte and then for the end of a text.
pub fn huffman() -> &'static [(u32, u32)] {
    static CODE: OnceLock<Vec<(u32, u32)>> = OnceLock::new();
    CODE.get_or_init(|| {
        let all = lines("rfc7541");
        let mut code = Vec::new();
        for l in between(&all, "Appendix B.", "Appendix C.") {
            // The character the symbol is written with may be a bracket, so the number is the
            // first thing in brackets that is one.
            let Some(symbol) = l.match_indices('(').find_map(|(open, _)| {
                let close = l[open..].find(')')? + open;
                l[open + 1..close].trim().parse::<usize>().ok()
            }) else {
                continue;
            };
            let Some(bracket) = l.rfind('[') else { continue };
            let length: u32 = l[bracket + 1..].trim_end().trim_end_matches(']').trim().parse().expect("a length");
            let hex = l[..bracket].split_whitespace().last().expect("a code");
            assert_eq!(symbol, code.len(), "{l}");
            code.push((u32::from_str_radix(hex, 16).expect("hex"), length));
        }
        assert_eq!(code.len(), 257);
        code
    })
}

/// One block of appendix C, with what the appendix says of it.
pub struct Example {
    pub block: Vec<u8>,
    /// The header list the block decodes to.
    pub headers: Vec<(String, String)>,
    /// The dynamic table after it, newest first, with the size of each entry.
    pub table: Vec<(String, String, usize)>,
}

/// Header lists read by one decoder, one block after another, and the size of
/// its table.
pub struct Series {
    pub name: String,
    pub table: usize,
    pub examples: Vec<Example>,
}

fn field(text: &str) -> (String, String) {
    // A name that starts with `:` has the colon of the pseudo header in it.
    let at = text[1..].find(": ").map(|i| i + 1).unwrap_or_else(|| panic!("no `: ` in {text}"));
    (text[..at].to_string(), text[at + 2..].to_string())
}

#[derive(PartialEq, Clone, Copy)]
enum Part {
    Nothing,
    Hex,
    Table,
    Headers,
}

/// The examples of appendix C: the sections 2, which are one header each, and
/// then the series of sections 3 to 6, each of three blocks.
pub fn appendix_c() -> &'static [Series] {
    static ALL: OnceLock<Vec<Series>> = OnceLock::new();
    ALL.get_or_init(|| {
        let all = lines("rfc7541");
        let start = all.iter().position(|l| l.starts_with("Appendix C.")).unwrap();
        let mut series: Vec<Series> = Vec::new();
        // The size of the table of each of sections 3 to 6, from the text before its examples.
        let mut sizes = std::collections::HashMap::new();
        let mut group = 0usize;
        let mut part = Part::Nothing;
        let mut open = false;
        let mut intro = String::new();
        for l in &all[start..] {
            if let Some(rest) = l.strip_prefix("C.") {
                let numbers: Vec<&str> = rest.split(". ").next().unwrap().split('.').collect();
                if numbers.len() == 1 {
                    group = numbers[0].parse().unwrap();
                    intro.clear();
                    open = false;
                    part = Part::Nothing;
                    continue;
                }
                let (g, n): (usize, usize) = (numbers[0].parse().unwrap(), numbers[1].parse().unwrap());
                if g < 2 {
                    open = false;
                    continue;
                }
                group = g;
                if n == 1 && g != 2 {
                    let size = intro
                        .split("value of ")
                        .nth(1)
                        .and_then(|t| t.split_whitespace().next()?.parse::<usize>().ok())
                        .unwrap_or(4096);
                    sizes.insert(g, size);
                    series.push(Series {
                        name: format!("C.{g}"),
                        table: size,
                        examples: Vec::new(),
                    });
                }
                if g == 2 {
                    series.push(Series { name: format!("C.2.{n}"), table: 4096, examples: Vec::new() });
                }
                series.last_mut().unwrap().examples.push(Example {
                    block: Vec::new(),
                    headers: Vec::new(),
                    table: Vec::new(),
                });
                open = true;
                part = Part::Nothing;
                continue;
            }
            if !l.starts_with(' ') && !l.is_empty() {
                open = false;
                continue;
            }
            if !open {
                if group >= 3 {
                    intro.push_str(l);
                    intro.push(' ');
                }
                continue;
            }
            let t = l.trim();
            let example = series.last_mut().unwrap().examples.last_mut().unwrap();
            match t {
                "Header list to encode:" | "Decoding process:" => part = Part::Nothing,
                "Hex dump of encoded data:" => part = Part::Hex,
                "Dynamic Table (after decoding):" => part = Part::Table,
                "Decoded header list:" => part = Part::Headers,
                "" => {}
                _ => match part {
                    Part::Hex if t.contains('|') => {
                        let hex: String = t.split('|').next().unwrap().split_whitespace().collect();
                        example.block.extend((0..hex.len()).step_by(2).map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap()));
                    }
                    Part::Table if t.starts_with('[') => {
                        let size: usize = t[t.find("s =").unwrap() + 3..t.find(')').unwrap()].trim().parse().unwrap();
                        let (name, value) = field(t[t.find(") ").unwrap() + 2..].trim());
                        example.table.push((name, value, size));
                    }
                    // What a long entry of the table or of a list is wrapped to.
                    Part::Table if !t.starts_with("Table size") => {
                        let last = example.table.last_mut().unwrap();
                        last.1.push(' ');
                        last.1.push_str(t);
                    }
                    Part::Headers if l.starts_with("   ") && !l.starts_with("    ") => {
                        example.headers.push(field(t));
                    }
                    Part::Headers => {
                        let last = example.headers.last_mut().unwrap();
                        last.1.push(' ');
                        last.1.push_str(t);
                    }
                    _ => {}
                },
            }
        }
        let _ = sizes;
        assert_eq!(series.len(), 4 + 4, "four single examples and four series");
        series
    })
}

/// The numbers appendix C.1 encodes with a prefix of 5 bits.
pub fn integers() -> Vec<usize> {
    let all = lines("rfc7541");
    all.iter()
        .filter(|l| l.starts_with("C.1.") && l.contains("Using a 5-Bit Prefix"))
        .map(|l| l.split("Encoding ").nth(1).unwrap().split_whitespace().next().unwrap().parse().unwrap())
        .collect()
}

/// A name and a number each a definition of RFC 9113 gives in the form
/// `NAME (0xNN)`, between the headings.
fn named(from: &str, to: &str, prefix: &str) -> Vec<(String, u32)> {
    let all = lines("rfc9113");
    between(&all, from, to)
        .iter()
        .filter_map(|l| {
            let t = l.strip_prefix("   ")?;
            if t.starts_with(' ') || !t.starts_with(prefix) {
                return None;
            }
            let (name, rest) = t.split_once(" (0x")?;
            Some((name.to_string(), u32::from_str_radix(rest.split("):").next()?, 16).ok()?))
        })
        .collect()
}

/// The frame types of RFC 9113 section 6, with the number of each, as each is
/// named where the section defines it: `The PING frame (type=0x06)`.
pub fn frame_types() -> Vec<(String, u8)> {
    let all = lines("rfc9113");
    let mut found = Vec::new();
    for l in between(&all, "6.  Frame Definitions", "7.  Error Codes") {
        let Some(at) = l.find("(type=0x") else { continue };
        let number = u8::from_str_radix(&l[at + 8..at + 10], 16).unwrap();
        let words: Vec<&str> = l[..at].split_whitespace().collect();
        let name = words.iter().rev().nth(1).copied().filter(|_| {
            words.last().is_some_and(|w| w.starts_with("frame"))
        });
        found.push((name.expect("a frame named before its type").to_string(), number));
    }
    found
}

/// The settings of RFC 9113 section 6.5.2, with the number of each.
pub fn settings() -> Vec<(String, u32)> {
    named("6.5.2.  Defined Settings", "6.5.3.", "SETTINGS_")
}

/// The error codes of RFC 9113 section 7, with the number of each.
pub fn error_codes() -> Vec<(String, u32)> {
    named("7.  Error Codes", "8.  Expressing HTTP Semantics", "")
}
