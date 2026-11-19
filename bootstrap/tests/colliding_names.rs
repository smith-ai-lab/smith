//! A module that is not the entry, or that the entry lists only some names of,
//! writes its own names with the module after them, and a name that is also a
//! case, a field, a parameter, or a local of the module is that name where the
//! module defines it, whatever the item of that name is called.

use smith::lower_project;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[(
    "a_name_that_is_also_a_case_a_field_a_parameter_or_a_local_means_each_where_it_is_defined",
    a_name_that_is_also_a_case_a_field_a_parameter_or_a_local_means_each_where_it_is_defined,
)];

/// The names a function of the module may have, which each role takes too.
const NAMES: &[&str] = &["object", "value", "name", "kind", "label"];

/// What the module writes with the name of its function, and how its names
/// reach `main`.
#[derive(Clone, Debug)]
struct Case {
    name: &'static str,
    field: bool,
    case: bool,
    param: bool,
    local: bool,
    /// A parameter and a result named in `requires` and `ensures`.
    contract: bool,
    /// Whether `main` lists the functions of the module itself, or those of a
    /// module that lists them, which is not the entry.
    through: bool,
    /// Whether the function of that name is listed too.
    lists_function: bool,
}

fn case(g: &mut Gen) -> Case {
    Case {
        name: g.pick(NAMES),
        field: g.one_in(2),
        case: g.one_in(2),
        param: g.one_in(2),
        local: g.one_in(2),
        contract: g.one_in(2),
        through: g.one_in(2),
        lists_function: g.one_in(2),
    }
}

fn a_name_that_is_also_a_case_a_field_a_parameter_or_a_local_means_each_where_it_is_defined() {
    generate::check(4401, 80, case, |c| {
        let n = c.name;
        let mut shapes = String::new();
        if c.field {
            shapes.push_str(&format!("struct Box {{\n  {n}: Nat\n}}\n\n"));
        }
        if c.case {
            shapes.push_str(&format!("enum Tag {{\n  {n}\n  rest: Nat\n}}\n\n"));
        }
        shapes.push_str(&format!("pub fn {n}() -> Nat {{ 3 }}\n"));
        // What each role gives, which is a number of its own.
        let mut roles: Vec<(&str, u64)> = Vec::new();
        if c.field {
            shapes.push_str(&format!(
                "\npub fn field() -> Nat {{\n  b = Box({n}: 7)\n  b.{n}\n}}\n"
            ));
            roles.push(("field", 7));
        }
        if c.case {
            shapes.push_str(&format!(
                "\npub fn case() -> Nat {{\n  t: Tag = .{n}\n  match t {{\n    .{n} {{ 1 }}\n    .rest(k) {{ k }}\n  }}\n}}\n"
            ));
            roles.push(("case", 1));
        }
        if c.param {
            shapes.push_str(&format!(
                "\nfn echo({n}: Nat) -> Nat {{ {n} }}\n\npub fn param() -> Nat {{ echo({n}: 9) }}\n"
            ));
            roles.push(("param", 9));
        }
        if c.local {
            shapes.push_str(&format!("\npub fn local() -> Nat {{\n  {n} = 11\n  {n}\n}}\n"));
            roles.push(("local", 11));
        }
        if c.contract {
            shapes.push_str(&format!(
                "\npub fn res() -> Nat {{ 5 }}\n\nfn guard({n}: 0..10) -> res: 0..10\n  requires {n} > 0\n  ensures res > 0\n{{\n  {n}\n}}\n\npub fn contract() -> Nat {{ guard({n}: 3) }}\n"
            ));
            roles.push(("contract", 3));
        }
        let mut names: Vec<&str> = roles.iter().map(|(r, _)| *r).collect();
        if c.lists_function {
            names.push(n);
        }
        // `main` calls each of them and prints what it gives.
        let mut called: Vec<(String, u64)> = roles.iter().map(|(r, v)| (r.to_string(), *v)).collect();
        if c.lists_function {
            called.push((n.to_string(), 3));
        }
        let (uses, modules): (String, Vec<(&str, &str)>);
        let relay;
        if c.through {
            // A module that is not the entry lists the names, and gives them on.
            let mut text = format!("use shapes ({})\n\n", names.join(", "));
            for (name, _) in &called {
                text.push_str(&format!("pub fn via_{name}() -> Nat {{ {name}() }}\n"));
            }
            relay = text;
            uses = format!(
                "use relay ({})\n",
                called.iter().map(|(name, _)| format!("via_{name}")).collect::<Vec<_>>().join(", ")
            );
            modules = vec![("shapes", shapes.as_str()), ("relay", relay.as_str())];
        } else {
            relay = String::new();
            uses = format!("use shapes ({})\n", names.join(", "));
            modules = vec![("shapes", shapes.as_str()), ("relay", relay.as_str())];
        }
        if called.is_empty() {
            return;
        }
        let mut main = format!("use std.console (console, print)\n{uses}\nfn main() ! console, alloc {{\n");
        for (name, _) in &called {
            let callee = if c.through { format!("via_{name}") } else { name.clone() };
            main.push_str(&format!("  print(\"{{{callee}()}}\")\n"));
        }
        main.push_str("}\n");
        let program = lower_project(main.as_bytes(), &modules)
            .unwrap_or_else(|e| panic!("{e:?} for\n{main}\nshapes\n{shapes}\nrelay\n{relay}"));
        let expected: String = called.iter().map(|(_, v)| format!("{v}\n")).collect();
        assert_eq!(
            String::from_utf8(scalar::run(&program).stdout).unwrap(),
            expected,
            "for\n{main}\nshapes\n{shapes}\nrelay\n{relay}"
        );
    });
}
