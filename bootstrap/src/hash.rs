//! The hasher of the maps and sets of the compiler whose keys are numbers and
//! names it made itself, which the standard library's SipHash hashes slowly to
//! keep a key that an attacker chose from slowing a server. Nothing here is
//! one: a program's own names are hashed by it, and what it costs is the time
//! of its own build. It is the multiply and rotate that `rustc` hashes with.
//!
//! A map that is read in the order of its keys is not read in the order this
//! gives, and neither was it in the one the standard library's gave: what is
//! reported or built is in an order that a list or a sort gives.

use std::collections::{HashMap, HashSet};
use std::hash::{BuildHasherDefault, Hasher};

/// The hasher.
#[derive(Default, Clone, Copy)]
pub(crate) struct Fast(u64);

const SEED: u64 = 0x51_7c_c1_b7_27_22_0a_95;

impl Fast {
    fn add(&mut self, word: u64) {
        self.0 = (self.0.rotate_left(5) ^ word).wrapping_mul(SEED);
    }
}

impl Hasher for Fast {
    fn write(&mut self, bytes: &[u8]) {
        let mut chunks = bytes.chunks_exact(8);
        for chunk in &mut chunks {
            self.add(u64::from_le_bytes(chunk.try_into().unwrap()));
        }
        let rest = chunks.remainder();
        if !rest.is_empty() {
            let mut last = [0; 8];
            last[..rest.len()].copy_from_slice(rest);
            self.add(u64::from_le_bytes(last));
        }
    }

    fn write_u8(&mut self, n: u8) {
        self.add(u64::from(n));
    }

    fn write_u32(&mut self, n: u32) {
        self.add(u64::from(n));
    }

    fn write_u64(&mut self, n: u64) {
        self.add(n);
    }

    fn write_usize(&mut self, n: usize) {
        self.add(n as u64);
    }

    /// The bits the multiplication mixes best are the high ones, and a table
    /// picks its slot by the low ones.
    fn finish(&self) -> u64 {
        self.0.rotate_left(26)
    }
}

pub(crate) type Map<K, V> = HashMap<K, V, BuildHasherDefault<Fast>>;
pub(crate) type Set<K> = HashSet<K, BuildHasherDefault<Fast>>;
