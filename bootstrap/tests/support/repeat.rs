//! The lexer in `compiler/lexer.smith` repeated under other function names, so
//! that a build of it can be made 10 and 100 times as large and what each stage
//! spends can be compared across the sizes. `measure/` includes this file, so
//! the program it times is the program the test counts.

/// The source of the lexer without its `main`, as the file holds it.
pub fn lexer(file: &str) -> &str {
    let at = file.find("\nfn main()").expect("lexer.smith has a main");
    &file[..at + 1]
}

/// The functions of `lexer`, each with its name and its text.
fn functions(lexer: &str) -> (String, Vec<(String, String)>) {
    let lines: Vec<&str> = lexer.lines().collect();
    let (mut rest, mut fns) = (String::new(), Vec::new());
    let mut i = 0;
    while i < lines.len() {
        let Some(after) = lines[i].strip_prefix("fn ") else {
            rest.push_str(lines[i]);
            rest.push('\n');
            i += 1;
            continue;
        };
        let name: String = after
            .chars()
            .take_while(|c| c.is_alphanumeric() || *c == '_')
            .collect();
        let mut end = i;
        if !lines[i].ends_with('}') || lines[i].ends_with("{ }") {
            while lines[end] != "}" {
                end += 1;
            }
        }
        fns.push((name, lines[i..=end].join("\n")));
        i = end + 1;
    }
    (rest, fns)
}

/// `text` with every name in `names` followed by `_<copy>`, but not inside a
/// comment, a string, or a character.
fn renamed(text: &str, names: &[&str], copy: usize) -> String {
    let chars: Vec<char> = text.chars().collect();
    let mut out = String::new();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c == '/' && chars.get(i + 1) == Some(&'/') {
            while i < chars.len() && chars[i] != '\n' {
                out.push(chars[i]);
                i += 1;
            }
        } else if c == '"' || c == '\'' {
            out.push(c);
            i += 1;
            while i < chars.len() && chars[i] != c {
                if chars[i] == '\\' {
                    out.push(chars[i]);
                    i += 1;
                }
                out.push(chars[i]);
                i += 1;
            }
            if i < chars.len() {
                out.push(c);
                i += 1;
            }
        } else if c.is_alphabetic() || c == '_' {
            let start = i;
            while i < chars.len() && (chars[i].is_alphanumeric() || chars[i] == '_') {
                i += 1;
            }
            let word: String = chars[start..i].iter().collect();
            out.push_str(&word);
            let after_dot = start > 0 && chars[start - 1] == '.';
            if !after_dot && names.contains(&word.as_str()) {
                out.push_str(&format!("_{copy}"));
            }
        } else {
            out.push(c);
            i += 1;
        }
    }
    out
}

/// The names in `names` that `text` mentions.
fn mentions<'a>(text: &str, names: &[&'a str]) -> Vec<&'a str> {
    let words: Vec<&str> = text
        .split(|c: char| !(c.is_alphanumeric() || c == '_'))
        .collect();
    names
        .iter()
        .copied()
        .filter(|n| words.contains(n))
        .collect()
}

/// A program of `copies` lexers: the types and the `use` once, and for each
/// copy the functions that `root` reaches, under their names and `_` and the
/// number of the copy, and a `main` that prints, for each copy, the number
/// that `count` gives, so that every copy is reached. `count` is the text of a
/// function body that gives a `Nat`, and may call `root`.
pub fn repeated(lexer: &str, copies: usize, root: &str, count: &str) -> String {
    let (rest, fns) = functions(lexer);
    let all: Vec<&str> = fns.iter().map(|(n, _)| n.as_str()).collect();
    let mut reached = vec![root];
    let mut at = 0;
    while at < reached.len() {
        let text = &fns
            .iter()
            .find(|(n, _)| n == reached[at])
            .expect("a function")
            .1;
        for name in mentions(text, &all) {
            if !reached.contains(&name) {
                reached.push(name);
            }
        }
        at += 1;
    }
    let kept: Vec<&(String, String)> = fns
        .iter()
        .filter(|(n, _)| reached.contains(&n.as_str()))
        .collect();
    let names: Vec<&str> = kept.iter().map(|(n, _)| n.as_str()).collect();
    let mut out = rest;
    for copy in 0..copies {
        for (_, text) in &kept {
            out.push('\n');
            out.push_str(&renamed(text, &names, copy));
            out.push('\n');
        }
        let body = renamed(count, &names, copy);
        out.push_str(&format!(
            "\nfn count_{copy}() -> Nat ! alloc {{\n  {body}\n}}\n"
        ));
    }
    out.push_str("\nfn main() ! console, alloc {\n");
    for copy in 0..copies {
        out.push_str(&format!("  print(\"{{count_{copy}()}}\")\n"));
    }
    out.push_str("}\n");
    used_imports_only(&out)
}

/// `text` with the names of its `use` lines that nothing else in it writes
/// taken away, as a program that is cut down imports what it still uses.
fn used_imports_only(text: &str) -> String {
    let lines: Vec<&str> = text.split_inclusive('\n').collect();
    let rest: String = (lines.iter())
        .filter(|l| !l.starts_with("use "))
        .copied()
        .collect();
    let words: Vec<&str> = rest
        .split(|c: char| !(c.is_alphanumeric() || c == '_'))
        .collect();
    let mut out = String::new();
    for line in lines {
        let Some(import) = line.trim_end().strip_prefix("use ") else {
            out.push_str(line);
            continue;
        };
        let (module, names) = import.split_once(" (").expect("a use of names");
        let kept: Vec<&str> = (names.trim_end_matches(')').split(", "))
            .filter(|n| words.contains(n))
            .collect();
        if !kept.is_empty() {
            out.push_str(&format!("use {module} ({})\n", kept.join(", ")));
        }
    }
    out
}
