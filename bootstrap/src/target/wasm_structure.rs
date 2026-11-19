//! How the blocks of a function of the scalar form become WebAssembly's own
//! `loop`, `block`, `if`, and `br`, as Ramsey describes in "Beyond Relooper".
//!
//! Each block is written once, inside the block that dominates it. A block
//! that more than one jump reaches, other than by a jump back, is the end of a
//! WebAssembly `block` that encloses the code of everything that jumps to it,
//! so those jumps are `br`. A block that is jumped to from after it is the
//! start of a `loop`, and those jumps are `br` to it. A block that one jump
//! reaches is written where the jump is. That covers every function whose
//! jumps back go only to blocks that dominate them, as the jumps of a loop do,
//! which are all that the passes leave. For any other function there is no
//! such writing, and `analyze` says so.

use crate::scalar::Function;

/// What the writing of a function needs to know of its blocks.
pub struct Shape {
    /// The place of each block in reverse postorder, and `usize::MAX` for a
    /// block that nothing reaches.
    pub order: Vec<usize>,
    /// Whether a jump back goes to the block, which is then the start of a
    /// loop.
    pub header: Vec<bool>,
    /// How many jumps that are not jumps back go to each block.
    pub entries: Vec<usize>,
    /// For each block, the blocks it immediately dominates that more than one
    /// jump reaches, by their order, which is where each is written.
    pub merges: Vec<Vec<usize>>,
}

impl Shape {
    /// Whether the jump from `from` to `to` goes back to a block that is
    /// being written, which is one the jump's own block is inside.
    pub fn back(&self, from: usize, to: usize) -> bool {
        self.order[to] <= self.order[from]
    }

    /// Whether the block is written at the end of a `block` of its own.
    pub fn merge(&self, block: usize) -> bool {
        self.entries[block] > 1
    }
}

/// The shape of a function, or `None` when a jump goes back to a block that
/// does not dominate it, so that the blocks are not written as loops.
pub fn analyze(f: &Function) -> Option<Shape> {
    let n = f.blocks.len();
    let successors: Vec<Vec<usize>> = f.blocks.iter().map(|b| b.end.successors()).collect();

    // The blocks each before every block it reaches except by a jump back.
    let mut seen = vec![false; n];
    let mut post = Vec::new();
    seen[0] = true;
    let mut stack = vec![(0, 0)];
    while let Some((b, next)) = stack.last_mut() {
        let b = *b;
        if let Some(&to) = successors[b].get(*next) {
            *next += 1;
            if !seen[to] {
                seen[to] = true;
                stack.push((to, 0));
            }
        } else {
            post.push(b);
            stack.pop();
        }
    }
    post.reverse();
    let mut order = vec![usize::MAX; n];
    for (place, &b) in post.iter().enumerate() {
        order[b] = place;
    }

    // The block that dominates each, as Cooper, Harvey, and Kennedy find it.
    let mut into: Vec<Vec<usize>> = vec![Vec::new(); n];
    for &b in &post {
        for &to in &successors[b] {
            into[to].push(b);
        }
    }
    let mut idom = vec![usize::MAX; n];
    idom[0] = 0;
    let meet = |idom: &[usize], mut a: usize, mut b: usize| {
        while a != b {
            while order[a] > order[b] {
                a = idom[a];
            }
            while order[b] > order[a] {
                b = idom[b];
            }
        }
        a
    };
    let mut changed = true;
    while changed {
        changed = false;
        for &b in &post[1..] {
            let mut done = into[b].iter().copied().filter(|&p| idom[p] != usize::MAX);
            let first = done.next().expect("a block that is reached has a way in");
            let new = done.fold(first, |a, p| meet(&idom, a, p));
            if idom[b] != new {
                idom[b] = new;
                changed = true;
            }
        }
    }

    // The places of the blocks in a walk of the tree of dominators, so that a
    // block dominates another when its walk contains the other's start.
    let mut children: Vec<Vec<usize>> = vec![Vec::new(); n];
    for &b in &post[1..] {
        children[idom[b]].push(b);
    }
    let mut start = vec![0; n];
    let mut end = vec![0; n];
    let mut clock = 0;
    let mut walk = vec![(0, 0)];
    start[0] = 0;
    while let Some((b, next)) = walk.last_mut() {
        let b = *b;
        if let Some(&c) = children[b].get(*next) {
            *next += 1;
            clock += 1;
            start[c] = clock;
            walk.push((c, 0));
        } else {
            end[b] = clock;
            walk.pop();
        }
    }
    let dominates = |a: usize, b: usize| start[a] <= start[b] && start[b] <= end[a];

    let mut header = vec![false; n];
    let mut entries = vec![0; n];
    for &b in &post {
        for &to in &successors[b] {
            if order[to] <= order[b] {
                if !dominates(to, b) {
                    return None;
                }
                header[to] = true;
            } else {
                entries[to] += 1;
            }
        }
    }
    let mut merges: Vec<Vec<usize>> = vec![Vec::new(); n];
    for &b in &post[1..] {
        if entries[b] > 1 {
            merges[idom[b]].push(b);
        }
    }
    for list in &mut merges {
        list.sort_by_key(|&b| order[b]);
    }
    Some(Shape {
        order,
        header,
        entries,
        merges,
    })
}

/// Whether the function's blocks are written as WebAssembly's own loops and
/// branches.
pub fn is_structured(f: &Function) -> bool {
    analyze(f).is_some()
}
