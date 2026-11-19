//! A message names an item of a module as the module wrote it, whatever the
//! module is called and whether the entry lists the item or not, since the
//! error says which file it is in.

use smith::lower_project;

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[(
    "a_message_names_an_item_of_a_module_as_the_module_wrote_it",
    a_message_names_an_item_of_a_module_as_the_module_wrote_it,
)];

const ITEMS: &[&str] = &["Tag", "Box", "Shape", "Kind"];
const MODULES: &[&str] = &["m", "shapes", "stuff_2"];

#[derive(Clone, Debug)]
struct Case {
    item: &'static str,
    module: &'static str,
    enumeration: bool,
    lists_item: bool,
}

fn case(g: &mut Gen) -> Case {
    Case {
        item: g.pick(ITEMS),
        module: g.pick(MODULES),
        enumeration: g.one_in(2),
        lists_item: g.one_in(2),
    }
}

fn a_message_names_an_item_of_a_module_as_the_module_wrote_it() {
    generate::check(4402, 40, case, |c| {
        let (item, module) = (c.item, c.module);
        let shape = if c.enumeration {
            format!("pub enum {item} {{\n  first\n  second\n}}\n\npub fn make() -> {item} {{ .first }}\n")
        } else {
            format!("pub struct {item} {{\n  pub size: Nat\n}}\n\npub fn make() -> {item} {{ {item}(size: 1) }}\n")
        };
        let listed = if c.lists_item { format!("make, {item}") } else { "make".to_string() };
        let main = format!(
            "use std.console (console, print)\nuse {module} ({listed})\n\nfn main() ! console, alloc {{\n  n: Nat = make()\n  print(\"{{n}}\")\n}}\n"
        );
        let errors = match lower_project(main.as_bytes(), &[(module, shape.as_str())]) {
            Ok(_) => panic!("builds, for\n{main}\n{shape}"),
            Err(errors) => errors,
        };
        assert!(!errors.is_empty());
        for e in errors {
            let text = format!("{} {}", e.message, e.fix.unwrap_or_default());
            assert!(!text.contains('@'), "{text:?} for\n{main}\n{shape}");
        }
    });
}
