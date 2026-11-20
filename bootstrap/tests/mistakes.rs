//! Mistakes in the declarations of a program and in its calls: each is
//! reported once, at the place the generator wrote it, and the mistakes that
//! follow from one are not reported. Each property takes what it expects
//! from the generator that built the file.

use smith::diag::Pos;
use smith::lower;

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::{lines_added, pos_after, shifted, with_imports};

pub const ONCE: &[Test] = &[
    (
        "a_name_that_is_no_trait_is_said_to_be_a_type_or_to_be_unknown",
        a_name_that_is_no_trait_is_said_to_be_a_type_or_to_be_unknown,
    ),
    (
        "a_type_parameter_with_the_name_of_a_type_or_of_another_parameter_is_refused",
        a_type_parameter_with_the_name_of_a_type_or_of_another_parameter_is_refused,
    ),
    (
        "a_function_with_a_wrong_effect_asks_its_caller_for_the_effects_it_has_and_no_more",
        a_function_with_a_wrong_effect_asks_its_caller_for_the_effects_it_has_and_no_more,
    ),
    (
        "the_fix_for_a_missing_effect_writes_the_signature_with_its_defaults_as_they_were_written",
        the_fix_for_a_missing_effect_writes_the_signature_with_its_defaults_as_they_were_written,
    ),
    (
        "a_default_that_needs_an_effect_is_refused_at_what_needs_it",
        a_default_that_needs_an_effect_is_refused_at_what_needs_it,
    ),
    (
        "a_type_that_holds_itself_is_reported_once_and_its_uses_are_not_checked",
        a_type_that_holds_itself_is_reported_once_and_its_uses_are_not_checked,
    ),
    (
        "a_type_that_holds_itself_only_through_a_list_is_accepted",
        a_type_that_holds_itself_only_through_a_list_is_accepted,
    ),
    (
        "a_type_that_holds_itself_with_no_list_between_is_refused_once_where_it_closes_the_cycle",
        a_type_that_holds_itself_with_no_list_between_is_refused_once_where_it_closes_the_cycle,
    ),
    (
        "a_trait_a_type_that_holds_itself_through_a_list_would_be_given_is_refused_at_the_use",
        a_trait_a_type_that_holds_itself_through_a_list_would_be_given_is_refused_at_the_use,
    ),
    (
        "a_type_parameter_of_an_implementation_is_refused_unless_its_types_name_it",
        a_type_parameter_of_an_implementation_is_refused_unless_its_types_name_it,
    ),
    (
        "a_function_that_gives_a_value_may_not_end_empty_or_with_a_binding",
        a_function_that_gives_a_value_may_not_end_empty_or_with_a_binding,
    ),
    (
        "a_call_after_a_dot_is_refused_for_a_name_in_scope_and_for_one_that_is_no_trait",
        a_call_after_a_dot_is_refused_for_a_name_in_scope_and_for_one_that_is_no_trait,
    ),
    (
        "a_call_of_a_trait_no_implementation_fits_gives_that_error_and_none_in_its_lambda",
        a_call_of_a_trait_no_implementation_fits_gives_that_error_and_none_in_its_lambda,
    ),
    (
        "of_literals_that_no_other_argument_settles_only_the_first_is_reported",
        of_literals_that_no_other_argument_settles_only_the_first_is_reported,
    ),
    (
        "a_pattern_that_assigns_to_a_var_twice_is_refused_at_each_name_after_the_first",
        a_pattern_that_assigns_to_a_var_twice_is_refused_at_each_name_after_the_first,
    ),
];

/// A file being written, and the errors its mistakes must give, each with
/// what its message must mention.
#[derive(Debug, Default)]
struct File {
    source: String,
    expected: Vec<(Pos, String)>,
    /// What the fix of the one error that has one must say, if it has one.
    fix: Option<String>,
}

impl File {
    fn push(&mut self, text: &str) {
        self.source.push_str(text);
    }

    /// Writes `text`, which holds a mistake that is at its start.
    fn mistake(&mut self, text: &str, what: impl Into<String>) {
        self.expected.push((pos_after(&self.source), what.into()));
        self.push(text);
    }

    /// Checks that the compiler gives each error of the file, in order, at
    /// its place, mentioning what it must, and none if the file has no
    /// mistake.
    fn check(&self) {
        // The file starts with the `use` lines of what it writes of the
        // library, which the places of its mistakes come after.
        let source = with_imports(&self.source);
        let by = lines_added(&self.source, &source);
        let mut expected: Vec<(Pos, String)> = (self.expected.iter())
            .map(|(pos, what)| (shifted(*pos, by), what.clone()))
            .collect();
        expected.sort_by_key(|(pos, _)| *pos);
        let found = match lower(source.as_bytes()) {
            Ok(_) if expected.is_empty() => return,
            Ok(_) => panic!("expected errors for {source:?}"),
            Err(errors) => errors,
        };
        let places: Vec<Pos> = found.iter().map(|e| e.pos).collect();
        let wanted: Vec<Pos> = expected.iter().map(|(pos, _)| *pos).collect();
        assert_eq!(places, wanted, "{found:?}");
        for (e, (_, what)) in found.iter().zip(&expected) {
            assert!(e.message.contains(what.as_str()), "{e} lacks {what}");
        }
        if let Some(fix) = &self.fix {
            let fixes: Vec<&String> = found.iter().filter_map(|e| e.fix.as_ref()).collect();
            assert_eq!(fixes, [fix], "{found:?}");
        }
    }
}

/// The start of a file that defines the struct `Cat`, the enum `Shape`, and
/// the trait `Named`.
const PRELUDE: &str = "struct Cat {\n  name: str\n}\n\n\
enum Shape {\n  dot\n  line: str\n}\n\n\
trait Named[T] {\n  fn name_of(x: T) -> str\n}\n\n";

/// A file with mistakes in the name of a trait, in a bound and in `without`.
/// An implementation of a trait that does not exist is never reached, so it
/// is not one.
fn file_with_names_that_are_no_trait(g: &mut Gen) -> File {
    let mut file = File::default();
    file.push(PRELUDE);
    let mut calls = String::new();
    for i in 0..1 + g.below(3) {
        let name = *g.pick(&["Nope", "str", "Byte", "Int", "Cat", "Shape", "f32"]);
        let what = if name == "Nope" {
            "no trait named"
        } else {
            "is not a trait"
        };
        if g.one_in(2) {
            file.push(&format!("fn f{i}[T: "));
            file.mistake(name, what);
            file.push("](x: T) {}\n");
            calls.push_str(&format!("  f{i}(Cat(name: \"a\"))\n"));
        } else {
            file.push("without ");
            file.mistake(name, what);
            file.push(" for Cat\n");
        }
    }
    file.push(&format!("fn main() {{\n{calls}}}\n"));
    file
}

fn a_name_that_is_no_trait_is_said_to_be_a_type_or_to_be_unknown() {
    generate::check(120, 200, file_with_names_that_are_no_trait, File::check);
}

/// A file whose functions each have a type parameter named for a type, a
/// trait, or another parameter of the function, or named as no type is.
fn file_with_type_parameters(g: &mut Gen) -> File {
    let mut file = File::default();
    file.push(PRELUDE);
    let mut calls = String::new();
    for i in 0..1 + g.below(4) {
        file.push(&format!("fn f{i}["));
        // The first function holds a mistake, so the file has one.
        match if i == 0 { g.below(2) } else { g.below(3) } {
            0 => {
                let name = *g.pick(&["str", "Cat", "Shape", "Named"]);
                file.mistake(name, "is already the name of a type");
                file.push(&format!("](x: {name}) {{}}\n"));
            }
            1 => {
                file.push("T, ");
                file.mistake("T", "is already a type parameter");
                file.push("](x: T) {}\n");
            }
            _ => file.push("T](x: T) {}\n"),
        }
        calls.push_str(&format!("  f{i}(1)\n"));
    }
    file.push(&format!("fn main() {{\n{calls}}}\n"));
    file
}

fn a_type_parameter_with_the_name_of_a_type_or_of_another_parameter_is_refused() {
    generate::check(121, 200, file_with_type_parameters, File::check);
}

/// A file whose functions list effects, some of which are not effects, are
/// not supported yet, or are listed twice, and whose `main` lists none and
/// calls each. Each mistake in a list is reported, and `main` is told of each
/// effect that is one a function has, once.
fn file_with_calls_of_functions_with_effects(g: &mut Gen) -> File {
    loop {
        let file = calls_of_functions_with_effects(g);
        if !file.expected.is_empty() {
            return file;
        }
    }
}

fn calls_of_functions_with_effects(g: &mut Gen) -> File {
    let mut file = File::default();
    let mut calls = Vec::new();
    let mut asked: Vec<&str> = Vec::new();
    for i in 0..1 + g.below(4) {
        let built = *g.pick(&["console", "alloc", "none"]);
        let mut listed: Vec<&str> = Vec::new();
        let mut first = true;
        file.push(&format!("fn f{i}()"));
        for _ in 0..1 + g.below(3) {
            file.push(if first { " ! " } else { ", " });
            first = false;
            let effect = match g.below(4) {
                0 | 1 if built != "none" => built,
                0 => "consol",
                1 => "fs",
                2 => "net",
                _ => "consol",
            };
            if listed.contains(&effect) {
                file.mistake(effect, format!("`{effect}` effect is already listed"));
            } else if effect == "consol" {
                file.mistake(effect, "no effect named `consol`");
            } else if effect == "fs" || effect == "net" {
                file.mistake(effect, format!("the `{effect}` effect is not supported yet"));
            } else {
                file.push(effect);
            }
            listed.push(effect);
        }
        file.push(" {}\n");
        calls.push((i, listed.iter().find(|e| ["console", "alloc"].contains(e)).copied()));
    }
    file.push("fn main() {\n");
    for (i, effect) in calls {
        file.push("  ");
        if let Some(effect) = effect
            && !asked.contains(&effect)
        {
            asked.push(effect);
            file.mistake(
                &format!("f{i}()\n"),
                format!("`f{i}` needs the `{effect}` effect"),
            );
        } else {
            file.push(&format!("f{i}()\n"));
        }
    }
    file.push("}\n");
    file
}

fn a_function_with_a_wrong_effect_asks_its_caller_for_the_effects_it_has_and_no_more() {
    generate::check(
        122,
        200,
        file_with_calls_of_functions_with_effects,
        File::check,
    );
}

/// A piece of the text of a default, as it is written when an error writes
/// the function again: the escapes it needs, a character that is not ASCII,
/// and an expression in braces.
const DEFAULT_PIECES: &[&str] = &[
    "a", " ", "é", "\\n", "\\t", "\\\"", "\\\\", "\\{", "\\}", "{1}", "{22}",
];

/// A file whose function `greet`, with a default text for its parameter,
/// needs the `console` effect and lists none, and whose default may hold an
/// expression, which needs the `alloc` effect that a default carries none of.
fn file_with_a_default_text(g: &mut Gen) -> File {
    let mut file = File::default();
    let mut default = String::from("\"");
    let mut interpolated = false;
    for _ in 0..1 + g.below(6) {
        let piece = *g.pick(DEFAULT_PIECES);
        interpolated |= piece.starts_with('{');
        default.push_str(piece);
    }
    default.push('"');
    file.push("fn greet(who: str = ");
    if interpolated {
        file.mistake(&default, "needs the `alloc` effect");
    } else {
        file.push(&default);
    }
    file.push(") {\n  ");
    file.mistake("print", "needs the `console` effect");
    file.push("(\"x\")\n}\n\nfn main() ! console {\n  greet()\n}\n");
    file.fix = Some(format!("write `fn greet(who: str = {default}) ! console`"));
    file
}

fn the_fix_for_a_missing_effect_writes_the_signature_with_its_defaults_as_they_were_written() {
    generate::check(123, 200, file_with_a_default_text, File::check);
}

/// A file whose `main` calls functions whose defaults each hold one thing that needs the
/// `alloc` effect, which a default carries none of: a list with elements, or
/// a call of `push`. Each is refused where the thing is written, and a
/// default that needs nothing is not.
fn file_with_defaults_that_need_an_effect(g: &mut Gen) -> File {
    let mut file = File::default();
    let mut calls = String::new();
    for i in 0..1 + g.below(4) {
        calls.push_str(&format!("  f{i}()\n"));
        file.push(&format!("fn f{i}(a: List[Byte] = "));
        match g.below(4) {
            0 => file.push("[]"),
            1 => {
                file.mistake("[", "a list that holds elements");
                for n in 0..1 + g.below(3) {
                    file.push(if n == 0 { "1" } else { ", 2" });
                }
                file.push("]");
            }
            2 => {
                file.mistake("push", "`push`");
                file.push("([], 3)");
            }
            _ => {
                file.mistake("[", "a list that holds elements");
                file.push("7]");
            }
        }
        file.push(") {}\n\n");
    }
    file.push(&format!("fn main() {{\n{calls}}}\n"));
    file
}

fn a_default_that_needs_an_effect_is_refused_at_what_needs_it() {
    generate::check(283, 200, file_with_defaults_that_need_an_effect, File::check);
}

/// A file that defines a struct or an enum that holds itself, directly or
/// through another, with no `List` between, and then uses them wrongly. A type
/// that holds itself so is not supported yet, which is the only error the file
/// gives: what the type would be is not known, so a use of it is not held to
/// it. The error is where the type that `main` uses first is written inside
/// what it holds.
fn file_with_a_type_that_holds_itself(g: &mut Gen) -> File {
    let mut file = File::default();
    let shape = g.below(4);
    let types: &[&str] = match shape {
        0 => &["E"],
        1 => &["S"],
        _ => &["E", "P"],
    };
    let first = *g.pick(types);
    // Writes a name, as the mistake if it is the one that closes the cycle.
    let name = |file: &mut File, name: &str| {
        if name == first {
            file.mistake(name, "holds itself");
        } else {
            file.push(name);
        }
    };
    match shape {
        0 => {
            file.push("enum E {\n  a: ");
            name(&mut file, "E");
            file.push("\n  b: str\n}\n");
        }
        1 => {
            file.push("struct S {\n  next: ");
            name(&mut file, "S");
            file.push("\n}\n");
        }
        2 => {
            file.push("enum E {\n  a: ");
            name(&mut file, "P");
            file.push("\n  b: str\n}\nstruct P {\n  e: ");
            name(&mut file, "E");
            file.push("\n}\n");
        }
        _ => {
            file.push("struct P {\n  e: ");
            name(&mut file, "E");
            file.push("\n}\nenum E {\n  a: ");
            name(&mut file, "P");
            file.push("\n  b: str\n}\n");
        }
    }
    file.push("fn main() {\n");
    for i in 0..1 + g.below(4) {
        let ty = if i == 0 { first } else { *g.pick(types) };
        let value = if ty == "E" && g.one_in(2) {
            ".b(1)"
        } else {
            "5"
        };
        file.push(&format!("  v{i}: {ty} = {value}\n"));
    }
    file.push("}\n");
    file
}

fn a_type_that_holds_itself_is_reported_once_and_its_uses_are_not_checked() {
    generate::check(124, 200, file_with_a_type_that_holds_itself, File::check);
}

/// The names of the types of a cycle, which are none that the library has.
const CYCLE: [&str; 4] = ["Ta", "Tb", "Tc", "Td"];

/// How a type is written inside the type before it, with `X` for it.
const HELD: [&str; 3] = ["X", "Option[X]", "(X, str)"];
const THROUGH: [&str; 3] = ["List[X]", "List[List[X]]", "Option[List[X]]"];

/// Writes the type `X`, as `form` says, with `name` written by `write`.
fn written(file: &mut File, form: &str, name: &str, mistake: Option<&str>) {
    let (before, after) = form.split_once('X').expect("a form names the type");
    file.push(before);
    match mistake {
        Some(what) => file.mistake(name, what),
        None => file.push(name),
    }
    file.push(after);
}

/// A cycle of structs and enums, each holding the next and the last holding
/// the first, as the first holds nothing else. `links` says for each how the
/// next is held, in place or through a `List`. When `mistake` says what, the
/// name of the first type, which the walk begins at when `main` uses it, is
/// written, where the last holds it, as the mistake.
fn cycle_of_types(g: &mut Gen, links: &[bool], mistake: Option<&str>, first_enum: bool) -> File {
    let mut file = File::default();
    let n = links.len();
    let mut kinds: Vec<bool> = (0..n).map(|_| g.one_in(2)).collect();
    if first_enum {
        kinds[0] = false;
    }
    let start = 0;
    for (i, is_struct) in kinds.iter().enumerate() {
        let next = (i + 1) % n;
        let form = match links[i] {
            true => *g.pick(&THROUGH),
            false => *g.pick(&HELD),
        };
        let closes = next == start && !links[i];
        let what = mistake.filter(|_| closes);
        if *is_struct {
            file.push(&format!("struct {} {{\n  next: ", CYCLE[i]));
        } else {
            file.push(&format!("enum {} {{\n  b: str\n  a: ", CYCLE[i]));
        }
        written(&mut file, form, CYCLE[next], what);
        file.push("\n}\n");
    }
    file
}

/// A file with a cycle of types, at least one of which holds the next through
/// a `List`, and a `main` that may hold a list of the first.
fn file_with_a_cycle_through_a_list(g: &mut Gen) -> File {
    let n = 1 + g.below(4);
    let mut links: Vec<bool> = (0..n).map(|_| g.one_in(2)).collect();
    let k = g.below(n);
    links[k] = true;
    let mut file = cycle_of_types(g, &links, None, false);
    if g.one_in(2) {
        file.push("fn main() {\n  v: List[Ta] = []\n}\n");
    } else {
        file.push("fn main() {}\n");
    }
    file
}

fn a_type_that_holds_itself_only_through_a_list_is_accepted() {
    generate::check(5870, 60, file_with_a_cycle_through_a_list, File::check);
}

/// A file with a cycle of types none of which holds the next through a
/// `List`, and a `main` that uses the first type the walk reaches, so the
/// error is given once, where the last of the cycle holds that type.
fn file_with_a_cycle_in_place(g: &mut Gen) -> File {
    let n = 1 + g.below(4);
    let links = vec![false; n];
    let mut file = cycle_of_types(g, &links, Some("holds itself"), false);
    file.push("fn main() {\n  v: Ta = 5\n}\n");
    file
}

fn a_type_that_holds_itself_with_no_list_between_is_refused_once_where_it_closes_the_cycle() {
    generate::check(5871, 60, file_with_a_cycle_in_place, |file: &File| {
        file.check();
        // The error says what is built.
        let found = lower(with_imports(&file.source).as_bytes()).expect_err("a refusal");
        assert!(found.len() == 1 && found[0].message.contains("`List`"), "{found:?}");
        assert!(found[0].unbuilt, "{found:?}");
    });
}

/// A file that asks `Eq` or `Fmt` of a struct or an enum that reaches itself
/// through a `List`, once, in a function that takes two of them.
fn file_with_a_trait_of_a_type_that_holds_itself(g: &mut Gen) -> (File, &'static str) {
    let n = 1 + g.below(3);
    let links = vec![true; n];
    let mut file = cycle_of_types(g, &links, None, true);
    let (use_of, trait_name) = if g.one_in(2) {
        ("a == b", "Eq")
    } else {
        ("\"{a}\"", "Fmt")
    };
    let (head, tail) = use_of.split_once('a').expect("a use names a");
    file.push("fn f(a: Ta, b: Ta) -> ");
    file.push(if trait_name == "Eq" { "bool" } else { "str" });
    file.push(if trait_name == "Eq" { " {\n  " } else { " ! alloc {\n  " });
    file.push(head);
    file.mistake("a", "Ta` contains itself");
    file.push(tail);
    file.push("\n}\nfn main() ! alloc {\n  v: Ta = .b(\"x\")\n  w: Ta = .b(\"y\")\n  r = f(v, w)\n}\n");
    (file, trait_name)
}

fn a_trait_a_type_that_holds_itself_through_a_list_would_be_given_is_refused_at_the_use() {
    generate::check(
        5872,
        60,
        file_with_a_trait_of_a_type_that_holds_itself,
        |(file, trait_name): &(File, &str)| {
            file.check();
            let found = lower(with_imports(&file.source).as_bytes()).expect_err("a refusal");
            let named = format!("given no `{trait_name}`");
            assert!(found.len() == 1 && found[0].message.contains(&named), "{found:?}");
        },
    );
}

/// A file with an implementation that has a type parameter, and a call that
/// uses it. The parameter must be named by the types the implementation is
/// for, and when it is not, the implementation is not one that calls choose,
/// so the call has none to use.
fn file_with_an_implementation_with_a_type_parameter(g: &mut Gen) -> File {
    let mut file = File::default();
    file.push("struct Cat {\n  name: str\n}\n\nstruct Box[A] {\n  item: A\n}\n\n");
    file.push("trait Named[X] {\n  fn name_of(x: X) -> str\n}\n\n");
    // The types it is for, a value of them, and whether they name `T`.
    let (types, value, named) = *g.pick(&[
        ("T", "Cat(name: \"a\")", true),
        ("Box[T]", "Box(item: Cat(name: \"a\"))", true),
        ("Cat", "Cat(name: \"a\")", false),
        ("Box[Cat]", "Box(item: Cat(name: \"a\"))", false),
    ]);
    file.push("impl[");
    if named {
        file.push("T");
    } else {
        file.mistake("T", "type parameter of an implementation");
    }
    file.push(&format!(
        "] Named[{types}] {{\n  fn name_of(x: {types}) -> str {{ \"a\" }}\n}}\n\n"
    ));
    file.push("fn main() ! console {\n  print(");
    if !named {
        file.mistake("name_of", "no implementation of `Named`");
    } else {
        file.push("name_of");
    }
    file.push(&format!("({value}))\n}}\n"));
    file
}

fn a_type_parameter_of_an_implementation_is_refused_unless_its_types_name_it() {
    generate::check(
        125,
        50,
        file_with_an_implementation_with_a_type_parameter,
        File::check,
    );
}

/// A file whose functions give a value, each ending in a value, in nothing at
/// all, or in a binding, which gives nothing. A function that does not end in
/// a value is refused where it ends.
fn file_with_functions_that_end_in_nothing(g: &mut Gen) -> File {
    let mut file = File::default();
    file.push(PRELUDE);
    let mut calls = String::new();
    for i in 0..1 + g.below(4) {
        let (result, value) = *g.pick(&[
            ("str", "\"a\""),
            ("Byte", "1"),
            ("Int", "1"),
            ("Cat", "Cat(name: \"a\")"),
            ("Shape", "Shape.dot"),
        ]);
        file.push(&format!("fn f{i}() -> {result} {{"));
        match g.below(4) {
            0 => {
                file.push(" ");
                file.push(value);
                file.push(" }\n");
            }
            1 => {
                file.mistake("}", format!("`f{i}` returns `{result}`, and its body is empty"));
                file.push("\n");
            }
            2 => {
                file.push("\n  ");
                file.mistake("x", format!("`f{i}` returns `{result}`, and its body ends with"));
                file.push(&format!(" = {value}\n}}\n"));
            }
            _ => {
                file.push("\n  var ");
                file.mistake("y", format!("`f{i}` returns `{result}`, and its body ends with"));
                file.push(&format!(" = {value}\n}}\n"));
            }
        }
        calls.push_str(&format!("  f{i}()\n"));
    }
    file.push(&format!("fn main() {{\n{calls}  done = 0\n}}\n"));
    file
}

fn a_function_that_gives_a_value_may_not_end_empty_or_with_a_binding() {
    generate::check(126, 200, file_with_functions_that_end_in_nothing, File::check);
}

/// A file whose `main` binds some names and calls a function after a dot of
/// a name. If the name is one `main` has bound, the call is of what a field
/// gives, which is not supported yet. If not, the name should be a trait, and
/// there is none.
fn file_with_calls_after_a_dot(g: &mut Gen) -> File {
    let mut file = File::default();
    file.push("fn main() {\n");
    let names = ["cat", "dog", "owl"];
    let bound = &names[..1 + g.below(3)];
    for name in bound {
        file.push(&format!("  {name} = \"a\"\n"));
    }
    for _ in 0..1 + g.below(4) {
        file.push("  ");
        let name = *g.pick(&["cat", "dog", "owl", "bird", "tit"]);
        if bound.contains(&name) {
            file.push(&format!("{name}."));
            file.mistake("call", "calling what a field gives");
        } else {
            file.mistake(name, format!("no trait named `{name}`"));
            file.push(".call");
        }
        file.push("()\n");
    }
    file.push("  done = 0\n}\n");
    file
}

fn a_call_after_a_dot_is_refused_for_a_name_in_scope_and_for_one_that_is_no_trait() {
    generate::check(127, 200, file_with_calls_after_a_dot, File::check);
}

/// A number too large for any whole number type.
const HUGE: &str = "99999999999999999999";

/// A file with a trait whose function takes a lambda, implemented for one
/// type, and calls of it. A call that no implementation fits is told so, once,
/// whether the type is that of another struct or of a number that settles on
/// a type with none. The types of what its lambda is given and must give are
/// not known, so a mistake that depends on them is not reported.
fn file_with_calls_of_a_trait_with_a_lambda(g: &mut Gen) -> File {
    let mut file = File::default();
    file.push("struct Box {\n  n: Byte\n}\n\nstruct Other {\n  n: Byte\n}\n\n");
    file.push("trait Mapper[C, A, out R] {\n  fn apply(c: C, a: A, f: fn(A) -> R) -> R\n}\n\n");
    file.push("impl Mapper[Box, Byte, str] {\n");
    file.push("  fn apply(c: Box, a: Byte, f: fn(Byte) -> str) -> str { f(a) }\n}\n\n");
    file.push("fn main() {\n  box = Box(n: 3)\n  other = Other(n: 4)\n  small: Byte = 5\n");
    for _ in 0..1 + g.below(4) {
        file.push("  ");
        // The call with `box` and a number settles on `i64`, which no
        // implementation is for.
        let (callee, number) = *g.pick(&[("box", "small"), ("box", "5"), ("other", "small")]);
        let fits = (callee, number) == ("box", "small");
        let body = match (fits, callee) {
            (true, _) => "\"a\"",
            (_, "box") => *g.pick(&["\"a\"", "x", "5", "x + \"a\""]),
            _ => *g.pick(&["\"a\"", "x", "5", HUGE, "x + \"a\""]),
        };
        if fits {
            file.push("apply");
        } else {
            file.mistake("apply", "no implementation of `Mapper`");
        }
        file.push(&format!("({callee}, {number}) fn(x) {{ {body} }}\n"));
    }
    file.push("  done = 0\n}\n");
    file
}

fn a_call_of_a_trait_no_implementation_fits_gives_that_error_and_none_in_its_lambda() {
    generate::check(128, 200, file_with_calls_of_a_trait_with_a_lambda, File::check);
}

/// A file with a function of type parameters, one for each of its parameters,
/// called with numbers too large for any type. No other argument says what
/// the type of each is, so the number takes `i64`, which it does not fit, and
/// only the first of them is told so, as the rest would repeat it.
fn file_with_literals_that_nothing_settles(g: &mut Gen) -> File {
    let mut file = File::default();
    let count = 2 + g.below(3);
    let generics: Vec<String> = (0..count).map(|i| format!("A{i}")).collect();
    let params: Vec<String> = (0..count).map(|i| format!("a{i}: A{i}")).collect();
    file.push(&format!("fn f[{}]({}) {{}}\n\n", generics.join(", "), params.join(", ")));
    file.push("fn main() {\n  f(");
    for i in 0..count {
        if i > 0 {
            file.push(", ");
        }
        if i == 0 {
            file.mistake(HUGE, "does not fit `i64`");
        } else {
            file.push(HUGE);
        }
    }
    file.push(")\n}\n");
    file
}

fn of_literals_that_no_other_argument_settles_only_the_first_is_reported() {
    generate::check(129, 20, file_with_literals_that_nothing_settles, File::check);
}

/// Writes a pattern of `vars` at some depth, and the tuple it matches to
/// `value`. Each name that an earlier part of the pattern named is a mistake.
fn write_pattern(
    g: &mut Gen,
    file: &mut File,
    value: &mut String,
    vars: usize,
    named: &mut Vec<usize>,
    depth: usize,
) {
    if depth == 0 || g.below(3) == 0 {
        let i = g.below(vars);
        let name = format!("v{i}");
        if named.contains(&i) {
            file.mistake(&name, format!("names `{name}` twice"));
        } else {
            file.push(&name);
        }
        named.push(i);
        value.push_str(&format!("{}", g.below(100)));
        return;
    }
    file.push("(");
    value.push('(');
    for k in 0..2 + g.below(2) {
        if k > 0 {
            file.push(", ");
            value.push_str(", ");
        }
        write_pattern(g, file, value, vars, named, depth - 1);
    }
    file.push(")");
    value.push(')');
}

/// A file whose `main` assigns a pattern to some `var`s, where the pattern
/// names a `var` more than once, as deep in the pattern as it likes.
fn file_with_patterns_that_assign_twice(g: &mut Gen) -> File {
    let mut file = File::default();
    let vars = 1 + g.below(3);
    file.push("fn main() ! console, alloc {\n");
    for i in 0..vars {
        file.push(&format!("  var v{i} = {i}\n"));
    }
    file.push("  ");
    let (mut value, mut named) = (String::new(), Vec::new());
    // A bare name is no pattern, so the outside is a tuple.
    file.push("(");
    value.push('(');
    for k in 0..2 + g.below(2) {
        if k > 0 {
            file.push(", ");
            value.push_str(", ");
        }
        let depth = g.below(3);
        write_pattern(g, &mut file, &mut value, vars, &mut named, depth);
    }
    file.push(")");
    value.push(')');
    file.push(&format!(" = {value}\n"));
    for i in 0..vars {
        file.push(&format!("  print(\"{{v{i}}}\")\n"));
    }
    file.push("}\n");
    file
}

fn a_pattern_that_assigns_to_a_var_twice_is_refused_at_each_name_after_the_first() {
    generate::check(130, 100, file_with_patterns_that_assign_twice, File::check);
}
