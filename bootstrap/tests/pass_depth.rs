//! No pass takes more than its share for a program that is deep. The program
//! is a type of enums, each of which holds the next in a case, read from and
//! written to JSON, and it is built at a depth and at twice the depth. Each
//! level adds a block to a function that threading goes through, so the
//! function's size grows with the depth, and a pass that looks again at what it
//! has looked at, once for each block, grows with the square of it.
//!
//! What is compared does not depend on the machine, as a time does: how large
//! the program that a pass leaves is, as its instructions, parameters, and
//! what each jump gives, and how many times the pass asks to allocate, which
//! a walk that is made again for each block does too.

use smith::scalar::{self, Function, Program};

use crate::Test;
use crate::counting::{Allocated, allocated};

pub const ONCE: &[Test] = &[(
    "no_pass_does_more_for_a_program_twice_as_deep_than_in_proportion_to_what_it_is_given",
    no_pass_does_more_for_a_program_twice_as_deep_than_in_proportion_to_what_it_is_given,
)];

/// The depths, the second twice the first.
const DEPTHS: [usize; 2] = [12, 24];

/// How much more than in proportion a pass may do or leave, in hundredths.
/// Twice the depth is a little more than twice the program, which is not
/// counted against a pass.
const SLACK: u64 = 150;

/// A program of `depth` enums that the program reads and writes as JSON.
fn program(depth: usize) -> String {
    let mut types = String::new();
    for d in 0..depth {
        let held = if d + 1 < depth { format!("c0: E{}\n  c1", d + 1) } else { "c0\n  c1".to_string() };
        types.push_str(&format!("enum E{d} {{\n  {held}\n}}\n\n"));
    }
    let mut value: String = (0..depth - 1).map(|d| format!("E{d}.c0(")).collect();
    value.push_str(&format!("E{}.c0", depth - 1));
    value.push_str(&")".repeat(depth - 1));
    let key = format!("{}\"c0\"{}", "{\"c0\":".repeat(depth - 1), "}".repeat(depth - 1));
    let text: Vec<String> = key.bytes().map(|b| b.to_string()).collect();
    format!(
        "use std.console (console, print)
use std.deserialize (ReadError)
use std.json (Json, from_json_as, to_json_as)
use std.list (freeze)
use std.utf8 (utf8)

fn s(bytes: take List[Byte]) -> str {{
  match utf8(freeze(bytes)) {{
    .ok(t) {{ t }}
    .err(e) {{ \"\" }}
  }}
}}

{types}fn main() ! console, alloc {{
  v: E0 = {value}
  k = Json(cases: .key, names: .written)
  r: Result[E0, ReadError] = from_json_as(s([{}]), k)
  match r {{
    .ok(w) {{
      match to_json_as(w, k) {{
        .ok(u) {{ print(\"ok\") }}
        .err(c) {{ print(\"clash\") }}
      }}
    }}
    .err(e) {{ print(\"bad\") }}
  }}
}}
",
        text.join(", ")
    )
}

/// The size of the program: what the passes have to look at, as the sum over its
/// functions of their blocks, parameters, instructions, and what each jump gives.
fn size(program: &Program) -> u64 {
    let of = |f: &Function| {
        let blocks = f.blocks.len();
        let params: usize = f.blocks.iter().map(|b| b.params.len()).sum();
        let instructions: usize = f.blocks.iter().map(|b| b.instructions.len()).sum();
        let args: usize = (f.blocks.iter())
            .map(|b| b.end.jumps().iter().map(|j| j.args.len()).sum::<usize>())
            .sum();
        (blocks + params + instructions + args) as u64
    };
    program.functions.iter().map(of).sum()
}

/// For each pass, how large the program is when it is given
/// to the pass and what it allocates.
fn passes(source: &str) -> Vec<(&'static str, u64, u64, Allocated)> {
    let mut program = smith::lower(source.as_bytes()).expect("the program builds");
    let mut seen = Vec::new();
    macro_rules! pass {
        ($name:literal, $pass:path) => {{
            let given = size(&program);
            let (made, did) = allocated(|| $pass(program));
            program = made;
            seen.push(($name, given, size(&program), did));
        }};
    }
    pass!("inline", scalar::inline);
    pass!("trim", scalar::trim);
    pass!("thread", scalar::thread);
    pass!("fold", scalar::fold);
    pass!("common", scalar::common);
    pass!("remove", scalar::remove);
    seen
}

fn no_pass_does_more_for_a_program_twice_as_deep_than_in_proportion_to_what_it_is_given() {
    let [few, many] = DEPTHS.map(|d| passes(&program(d)));
    for (a, b) in few.iter().zip(&many) {
        let name = a.0;
        let (given_a, given_b) = (a.1, b.1);
        let depths = DEPTHS;
        // Each grows as what the pass is given does, and no more.
        let counts = [
            ("leaves", a.2, b.2),
            ("asks to allocate", a.3.calls, b.3.calls),
        ];
        for (what, at_few, at_many) in counts {
            assert!(
                at_many * given_a * 100 <= SLACK * at_few * given_b,
                "{name} {what} {at_few} with {given_a} given at depth {}, and {at_many} with {given_b} given at depth {}",
                depths[0],
                depths[1]
            );
        }
    }
}
