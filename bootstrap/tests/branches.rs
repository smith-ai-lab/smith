//! What the targets write for a branch: a comparison that a choice reads and
//! nothing else is one compare and one branch, and a push that has room in a
//! loop takes few instructions. Both are read from the bytes of the
//! executables.

use smith::scalar::{self, Bits, Builder, Compare, End, Op, Type};
use smith::target::Target;
use smith::{compile, lower};

use crate::Test;
use crate::aarch64;
use crate::machine_code::{self, Kind, Machine};

pub const ONCE: &[Test] = &[
    (
        "a_comparison_that_only_a_branch_reads_is_one_compare_and_one_branch",
        a_comparison_that_only_a_branch_reads_is_one_compare_and_one_branch,
    ),
    (
        "a_choice_of_a_block_more_than_1_mb_away_goes_there",
        a_choice_of_a_block_more_than_1_mb_away_goes_there,
    ),
    (
        "a_push_that_has_room_is_20_instructions_or_fewer_in_a_loop_on_x86_64",
        a_push_that_has_room_is_20_instructions_or_fewer_in_a_loop_on_x86_64,
    ),
];

/// Programs that print nothing, so that their code is all the executable
/// holds, and that compare values the compiler cannot know.
const QUIET: &[&str] = &[
    "fn main() ! alloc {\n  var xs: List[u64] = []\n  loop i in 0..1000 { xs = push(xs, i) }\n}\n",
    "fn main() ! alloc {
  var xs: List[u64] = []
  loop i in 0..100 { xs = push(xs, i) }
  var best: u64 = 0
  var at: u64 = 0
  loop j in 0..xs.size {
    if xs[j] > best {
      best = xs[j]
      at = j
    } else if xs[j] == best {
      at = j
    }
  }
  var ys: List[u64] = []
  loop k in 0..xs.size { if xs[k] < best { ys = push(ys, at) } }
}
",
];

/// How many comparisons the program makes, and how many of them only the choice
/// that ends their block reads, which are the ones a target need not make a
/// value of.
fn comparisons(program: &scalar::Program) -> (usize, usize) {
    let (mut all, mut branched) = (0, 0);
    for f in &program.functions {
        let mut reads = vec![0usize; f.types.len()];
        for block in &f.blocks {
            let operands = block.instructions.iter().flat_map(|i| i.op.operands());
            for v in operands.chain(block.end.operands()) {
                reads[v.0] += 1;
            }
        }
        for block in &f.blocks {
            all += block
                .instructions
                .iter()
                .filter(|i| matches!(i.op, Op::Compare { .. }))
                .count();
            if let (End::Switch { on, jumps }, Some(last)) = (&block.end, block.instructions.last())
                && jumps.len() == 2
                && reads[on.0] == 1
                && last.results == [*on]
                && matches!(last.op, Op::Compare { .. })
            {
                branched += 1;
            }
        }
    }
    (all, branched)
}

fn a_comparison_that_only_a_branch_reads_is_one_compare_and_one_branch() {
    for source in QUIET {
        let program = scalar::passes(&lower(source.as_bytes()).unwrap());
        let (all, branched) = comparisons(&program);
        assert!(branched > 0, "{source}");
        for target in Target::ALL {
            let bytes = compile(source.as_bytes(), *target).unwrap();
            let Some((_, found)) = machine_code::instructions(&bytes) else {
                continue;
            };
            let made = found.iter().filter(|i| i.kind == Kind::SetCondition).count();
            assert_eq!(
                made,
                all - branched,
                "a comparison that a branch only tests is made a value on {target:?}: {source}"
            );
        }
    }
}

fn a_push_that_has_room_is_20_instructions_or_fewer_in_a_loop_on_x86_64() {
    // A bound that no other number in the program is, so that the
    // instruction that holds it is the loop's own test.
    const PUSHES: i64 = 1_000_003;
    let source = format!(
        "fn main() ! console, alloc {{\n  var xs: List[u64] = []\n  loop i in 0..{PUSHES} {{ xs = push(xs, i) }}\n  print(\"{{xs.size}}\")\n}}\n"
    );
    let mut read = 0;
    for target in Target::ALL {
        let bytes = compile(source.as_bytes(), *target).unwrap();
        let Some((Machine::X86_64, found)) = machine_code::instructions(&bytes) else {
            continue;
        };
        read += 1;
        let round = machine_code::shortest_round(&found, PUSHES);
        assert!(round <= 20, "a push that has room takes {round} instructions");
    }
    assert!(read > 0, "no target is an ELF file for x86-64");
}

/// A function whose first block chooses between a long block and the block
/// after it, which asks for memory, so that the branch to the last block is longer than the 1 MB a
/// conditional branch of arm64 reaches. It ends with the status of the block
/// the choice went to.
fn a_choice_of_a_block_more_than_1_mb_away_goes_there() {
    let long = Type::Int(Bits::B64);
    {
        let mut b = Builder::new();
        let (padding, _) = b.block(&[]);
        let (end, _) = b.block(&[]);
        let seven = b.int(Bits::B64, 7);
        let said = b.int(Bits::B64, 7);
        let same = b.compare(Compare::Equal, seven, said);
        b.switch(same, vec![(padding, vec![]), (end, vec![])]);
        b.at(padding);
        let mut v = b.emit(Op::Constant(smith::scalar::Constant::Int(1)), &[long])[0];
        for _ in 0..60_000 {
            let n = b.int(Bits::B64, 0x1234_5678_9abc_def0);
            v = b.binary(smith::scalar::Binary::Add, v, n);
        }
        let status = b.int(Bits::B8, 4);
        b.exit(status);
        b.ret(vec![]);
        b.at(end);
        // A request for memory branches back to the code that ends the
        // program when it is refused, which is more than 1 MB behind.
        let eight = b.int(Bits::B64, 8);
        b.allocate(eight);
        let status = b.int(Bits::B8, 3);
        b.exit(status);
        b.ret(vec![]);
        let program = scalar::Program {
            data: Vec::new(),
            functions: vec![b.finish(&[])],
            main: 0,
        };
        assert_eq!(scalar::check(&program), Ok(()));
        let said = scalar::run(&program);
        assert_eq!(said.status, 3);
        let mut ran = 0;
        for target in Target::ALL {
            let bytes = target.emit(&program);
            if aarch64::is_executable(&bytes) {
                ran += 1;
                assert!(bytes.len() > 1 << 20, "the choice is closer than 1 MB");
                assert_eq!(aarch64::run(&bytes).status, said.status);
            }
        }
        assert!(ran > 0, "no target is built for arm64");
    }
}
