//! What a test needs to hold a library module to the text of a standard, which
//! is in `reference/rfc/`: the sentences of some sections that hold MUST,
//! MUST NOT, or SHALL, a ledger that says for each what holds it, and the
//! bytes that a rule of the standard's ABNF allows.
//!
//! A ledger is text, one line to a sentence, written `<section> | <first
//! words of the sentence> | test <name>` or `<section> | <first words> |
//! reason <why the module has nothing to do with it>`. The first words are
//! enough to be one sentence of that section only. A line that starts with `#`
//! or is blank is not a line of the ledger.

use std::path::PathBuf;

/// The text of a standard of `reference/rfc/`, such as `rfc9112.txt`.
pub fn read(file: &str) -> String {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("reference")
        .join("rfc")
        .join(file);
    std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("{} is read by the tests: {e}", path.display()))
}

/// A sentence that holds MUST, MUST NOT, or SHALL, and the section it is in.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Sentence {
    pub section: String,
    pub text: String,
}

const KEYWORDS: &[&str] = &["MUST NOT", "MUST", "SHALL NOT", "SHALL"];

/// Whether `section` is one of `wanted` or lies inside one of them.
fn selected(section: &str, wanted: &[&str]) -> bool {
    wanted
        .iter()
        .any(|w| section == *w || section.starts_with(&format!("{w}.")))
}

/// The number of a section if the line is the heading of one, as `3.2.` and
/// then two spaces and a title.
fn heading(line: &str) -> Option<&str> {
    let (number, title) = line.split_once("  ")?;
    let number = number.strip_suffix('.')?;
    let well_formed = !number.is_empty()
        && number
            .split('.')
            .all(|part| !part.is_empty() && part.bytes().all(|b| b.is_ascii_digit()));
    (well_formed && !title.trim().is_empty() && !title.starts_with(' ')).then_some(number)
}

/// A line the pages of a text repeat, which is no part of it.
fn page_furniture(line: &str) -> bool {
    line.trim_end().ends_with(']') && line.contains("[Page ")
        || line.starts_with("RFC ") && line.contains(" June ")
}

/// Cuts a paragraph at the end of each sentence: a full stop, a question mark,
/// or an exclamation mark, and then a space and the start of a sentence. A
/// full stop that follows `e.g.`, `i.e.`, or `al.` ends none.
fn cut(paragraph: &str) -> Vec<String> {
    let chars: Vec<char> = paragraph.chars().collect();
    let mut sentences = Vec::new();
    let mut start = 0;
    for i in 0..chars.len() {
        if !matches!(chars[i], '.' | '?' | '!') || chars.get(i + 1) != Some(&' ') {
            continue;
        }
        let before: String = chars[start..=i].iter().collect();
        if ["e.g.", "i.e.", "al.", "etc."].iter().any(|a| before.ends_with(a)) {
            continue;
        }
        let next = chars[i + 2..].iter().find(|c| **c != ' ');
        if next.is_some_and(|c| c.is_uppercase() || matches!(c, '"' | '(' | '[')) {
            sentences.push(chars[start..=i].iter().collect::<String>());
            start = i + 2;
        }
    }
    sentences.push(chars[start..].iter().collect());
    sentences
}

/// A part of a standard's text, in order: where a section begins (or none, where
/// what follows is no part of the numbered sections), a line of a paragraph, and
/// the end of a paragraph.
enum Part {
    Section(Option<String>),
    Line(String),
    End,
}

/// Every sentence of the sections `wanted` (and the sections inside them)
/// that holds one of the words of requirement, in the order of the text. A
/// sentence is its words with every run of spaces made one. The text is an
/// RFC as `www.rfc-editor.org` publishes it, or a page of HTML whose sections
/// are headings that begin with their number.
pub fn sentences(text: &str, wanted: &[&str]) -> Vec<Sentence> {
    let parts = if text.contains("<html") || text.trim_start().starts_with("<!") {
        html_parts(text)
    } else {
        rfc_parts(text)
    };
    let mut found = Vec::new();
    let mut section: Option<String> = None;
    let mut paragraph: Vec<String> = Vec::new();
    let mut flush = |section: &Option<String>, paragraph: &mut Vec<String>| {
        if let Some(section) = section.as_ref().filter(|s| selected(s, wanted)) {
            let text = paragraph.join(" ");
            for sentence in cut(&text) {
                let words: Vec<&str> = sentence.split_whitespace().collect();
                let has = |k: &str| {
                    let k: Vec<&str> = k.split(' ').collect();
                    words.windows(k.len()).any(|w| w == k.as_slice())
                };
                if KEYWORDS.iter().any(|k| has(k)) {
                    found.push(Sentence {
                        section: section.clone(),
                        text: words.join(" "),
                    });
                }
            }
        }
        paragraph.clear();
    };
    for part in parts {
        match part {
            Part::Section(next) => {
                flush(&section, &mut paragraph);
                section = next;
            }
            Part::End => flush(&section, &mut paragraph),
            Part::Line(line) => paragraph.push(line),
        }
    }
    flush(&section, &mut paragraph);
    found
}

/// The parts of an RFC: a heading at the start of a line begins a section, an
/// appendix ends them, and a blank line ends a paragraph.
fn rfc_parts(rfc: &str) -> Vec<Part> {
    let mut parts = Vec::new();
    for line in rfc.trim_start_matches('\u{feff}').lines() {
        if page_furniture(line) {
            continue;
        }
        if line.starts_with("Appendix ") {
            parts.push(Part::Section(None));
        } else if let Some(number) = heading(line).filter(|_| !line.starts_with(' ')) {
            parts.push(Part::Section(Some(number.to_string())));
        } else if line.trim().is_empty() {
            parts.push(Part::End);
        } else {
            parts.push(Part::Line(line.trim().to_string()));
        }
    }
    parts
}

/// The parts of a page of HTML: the text of each paragraph, list item, and
/// definition, cut at each line break, and each heading of the second or third
/// level that begins with a number, such as `4.1 Notification`, as a section.
fn html_parts(html: &str) -> Vec<Part> {
    let mut parts = Vec::new();
    let mut text = String::new();
    let mut heading = false;
    let mut rest = html;
    while let Some(open) = rest.find('<') {
        text.push_str(&decode(&rest[..open]));
        let Some(close) = rest[open..].find('>') else {
            break;
        };
        let tag = rest[open + 1..open + close].trim().to_ascii_lowercase();
        rest = &rest[open + close + 1..];
        let name = tag
            .trim_start_matches('/')
            .split(|c: char| c.is_whitespace() || c == '/')
            .next()
            .unwrap_or("");
        // What a script or a style holds is no text of the page.
        if (name == "script" || name == "style") && !tag.starts_with('/') {
            let end = format!("</{name}");
            rest = rest.find(&end).map_or("", |i| &rest[i..]);
            continue;
        }
        let block = matches!(
            name,
            "p" | "br" | "li" | "dd" | "dt" | "dl" | "ul" | "ol" | "pre" | "div" | "table" | "tr"
                | "td" | "th" | "h1" | "h2" | "h3" | "h4" | "h5" | "h6"
        );
        if !block {
            continue;
        }
        let words: Vec<&str> = text.split_whitespace().collect();
        if heading {
            let number = words.first().copied().unwrap_or("");
            let numbered = !number.is_empty()
                && number
                    .split('.')
                    .all(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()));
            parts.push(Part::Section(numbered.then(|| number.to_string())));
        } else if !words.is_empty() {
            parts.push(Part::Line(words.join(" ")));
        }
        text.clear();
        heading = false;
        parts.push(Part::End);
        if matches!(name, "h2" | "h3") && !tag.starts_with('/') {
            heading = true;
        }
    }
    parts
}

/// The text of a piece of HTML with the character references it holds, which
/// are `&nbsp;`, `&lt;`, `&gt;`, `&quot;`, and `&amp;`, written out.
fn decode(piece: &str) -> String {
    piece
        .replace("&nbsp;", " ")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&amp;", "&")
}

/// What the ledger gives for each sentence of `wanted` sections of `rfc`: the
/// errors, none when every sentence is in a line that says what holds it. A
/// line that names a test names one of `tests`, and a line that names no
/// sentence, or more than one, is an error as well.
pub fn check_ledger(
    rfc: &str,
    wanted: &[&str],
    ledger: &str,
    tests: &[&str],
) -> Result<(), Vec<String>> {
    let sentences = sentences(rfc, wanted);
    let mut named = vec![false; sentences.len()];
    let mut errors = Vec::new();
    for line in ledger.lines().map(str::trim) {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let parts: Vec<&str> = line.splitn(3, '|').map(str::trim).collect();
        let [section, words, holds] = parts[..] else {
            errors.push(format!("not a line of the ledger: {line}"));
            continue;
        };
        let words = words.split_whitespace().collect::<Vec<_>>().join(" ");
        let at: Vec<usize> = (0..sentences.len())
            .filter(|i| sentences[*i].section == section && sentences[*i].text.starts_with(&words))
            .collect();
        match at[..] {
            [] => errors.push(format!("names no sentence: {line}")),
            [i] => {
                named[i] = true;
                if let Some(name) = holds.strip_prefix("test ") {
                    if !tests.contains(&name.trim()) {
                        errors.push(format!("names a test that does not exist: {line}"));
                    }
                } else if holds.strip_prefix("reason ").is_none_or(|r| r.trim().is_empty()) {
                    errors.push(format!("says neither a test nor a reason: {line}"));
                }
            }
            _ => errors.push(format!("names more than one sentence: {line}")),
        }
    }
    for (sentence, named) in sentences.iter().zip(&named) {
        if !named {
            errors.push(format!(
                "in no line of the ledger: {} | {}",
                sentence.section, sentence.text
            ));
        }
    }
    if errors.is_empty() { Ok(()) } else { Err(errors) }
}

/// The bytes that the rule `name` of the ABNF in `rfc` allows, for a rule that
/// is an alternative of single characters, `%x` values and ranges, core rules,
/// and other rules of the same kind, as `tchar`, `obs-text`, and `field-vchar`
/// are. A repetition or a group round one element is that element.
pub fn abnf_bytes(rfc: &str, name: &str) -> [bool; 256] {
    let mut set = [false; 256];
    let text = rule(rfc, name).unwrap_or_else(|| panic!("the text has no rule `{name}`"));
    for alternative in text.split('/') {
        let element = alternative
            .trim()
            .trim_start_matches(|c: char| c == '*' || c.is_ascii_digit())
            .trim_start_matches('(')
            .trim_end_matches(')')
            .trim();
        for b in element_bytes(rfc, element) {
            set[b as usize] = true;
        }
    }
    set
}

/// The text of a rule, after its `=`, with the lines set further in that
/// carry on, as the collected grammar of an appendix writes it. The first rule of that name
/// is it.
fn rule(rfc: &str, name: &str) -> Option<String> {
    let mut lines = rfc.lines();
    let mut text = String::new();
    while let Some(line) = lines.next() {
        let Some(rest) = line.trim_start().strip_prefix(name) else {
            continue;
        };
        let Some(rest) = rest.trim_start().strip_prefix('=') else {
            continue;
        };
        if !line.starts_with(' ') || rest.starts_with('/') {
            continue;
        }
        text.push_str(without_comment(rest));
        // The lines after it that are set further in are the rest of it.
        let indent = |l: &str| l.len() - l.trim_start().len();
        for next in lines.by_ref() {
            if next.trim().is_empty() || indent(next) <= indent(line) {
                break;
            }
            text.push(' ');
            text.push_str(without_comment(next).trim());
        }
        // A rule that only says where it is defined, in another standard, is not it.
        if text.trim().starts_with('<') {
            text.clear();
            continue;
        }
        return Some(text);
    }
    None
}

/// A line of a rule without its comment, which is from a semicolon on.
fn without_comment(line: &str) -> &str {
    line.split(';').next().unwrap_or("")
}

fn element_bytes(rfc: &str, element: &str) -> Vec<u8> {
    if let Some(quoted) = element.strip_prefix('"').and_then(|e| e.strip_suffix('"')) {
        assert_eq!(quoted.len(), 1, "a rule of single characters: {element}");
        return vec![quoted.as_bytes()[0]];
    }
    if let Some(hex) = element.strip_prefix("%x") {
        let value = |h: &str| u8::from_str_radix(h, 16).unwrap();
        return match hex.split_once('-') {
            Some((a, b)) => (value(a)..=value(b)).collect(),
            None => vec![value(hex)],
        };
    }
    match core(element) {
        Some(ranges) => ranges.iter().flat_map(|(a, b)| *a..=*b).collect(),
        None => {
            let set = abnf_bytes(rfc, element);
            (0..=255).filter(|b| set[*b as usize]).collect()
        }
    }
}

// The core rules of RFC 5234, Appendix B.1, which a standard that uses ABNF
// does not repeat, each as the ranges of the bytes it allows. A quoted string
// of ABNF holds either case of a letter, so `HEXDIG` holds `a` to `f`.

/// `ALPHA = %x41-5A / %x61-7A`
pub const ALPHA: &[(u8, u8)] = &[(0x41, 0x5a), (0x61, 0x7a)];
/// `CTL = %x00-1F / %x7F`
pub const CTL: &[(u8, u8)] = &[(0x00, 0x1f), (0x7f, 0x7f)];
/// `DIGIT = %x30-39`
pub const DIGIT: &[(u8, u8)] = &[(0x30, 0x39)];
/// `DQUOTE = %x22`
pub const DQUOTE: &[(u8, u8)] = &[(0x22, 0x22)];
/// `HEXDIG = DIGIT / "A" / "B" / "C" / "D" / "E" / "F"`
pub const HEXDIG: &[(u8, u8)] = &[(0x30, 0x39), (0x41, 0x46), (0x61, 0x66)];
/// `HTAB = %x09`
pub const HTAB: &[(u8, u8)] = &[(0x09, 0x09)];
/// `SP = %x20`
pub const SP: &[(u8, u8)] = &[(0x20, 0x20)];
/// `VCHAR = %x21-7E`
pub const VCHAR: &[(u8, u8)] = &[(0x21, 0x7e)];
/// `WSP = SP / HTAB`
pub const WSP: &[(u8, u8)] = &[(0x20, 0x20), (0x09, 0x09)];

/// The ranges of a core rule, named as the rule is.
fn core(name: &str) -> Option<&'static [(u8, u8)]> {
    Some(match name {
        "ALPHA" => ALPHA,
        "CTL" => CTL,
        "DIGIT" => DIGIT,
        "DQUOTE" => DQUOTE,
        "HEXDIG" => HEXDIG,
        "HTAB" => HTAB,
        "SP" => SP,
        "VCHAR" => VCHAR,
        "WSP" => WSP,
        _ => return None,
    })
}

/// Whether `b` is in one of the ranges of a core rule.
pub fn in_core(ranges: &[(u8, u8)], b: u8) -> bool {
    ranges.iter().any(|(a, z)| (*a..=*z).contains(&b))
}
