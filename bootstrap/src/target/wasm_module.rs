//! The writer of a WebAssembly module: its sections, in the order the format
//! asks for them.

use super::wasm_code::{Kind, unsigned};

/// What a function takes and gives.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Signature {
    pub params: Vec<Kind>,
    pub results: Vec<Kind>,
}

/// A function the module defines.
pub struct Body {
    /// The type of each local after the parameters, in runs.
    pub locals: Vec<(u32, Kind)>,
    pub code: Vec<u8>,
}

/// What an export names.
#[derive(Clone, Copy)]
pub enum Export {
    Function(u32),
    Memory,
}

/// A global of 32 bits that the module may change.
pub struct Global {
    pub initial: i32,
}

#[derive(Default)]
pub struct Module {
    signatures: Vec<Signature>,
    /// The functions the module asks its host for: the name it is asked for as
    /// the effect and its operation, and its signature.
    imports: Vec<(String, String, u32)>,
    functions: Vec<u32>,
    bodies: Vec<Body>,
    /// How many 64 KB pages the memory starts with.
    pub pages: u32,
    pub globals: Vec<Global>,
    exports: Vec<(String, Export)>,
    /// What is in memory when the module starts: the address and the bytes.
    pub data: Vec<(u32, Vec<u8>)>,
}

impl Module {
    fn signature(&mut self, signature: Signature) -> u32 {
        let place = self.signatures.iter().position(|s| *s == signature);
        place.unwrap_or_else(|| {
            self.signatures.push(signature);
            self.signatures.len() - 1
        }) as u32
    }

    /// Asks the host for a function, and gives its number.
    pub fn import(&mut self, effect: &str, operation: &str, signature: Signature) -> u32 {
        assert!(self.functions.is_empty(), "imports come before functions");
        let signature = self.signature(signature);
        self.imports
            .push((effect.to_string(), operation.to_string(), signature));
        self.imports.len() as u32 - 1
    }

    /// Defines a function, and gives its number, which follows the imports.
    pub fn function(&mut self, signature: Signature, body: Body) -> u32 {
        let signature = self.signature(signature);
        self.functions.push(signature);
        self.bodies.push(body);
        (self.imports.len() + self.functions.len()) as u32 - 1
    }

    pub fn export(&mut self, name: &str, what: Export) {
        self.exports.push((name.to_string(), what));
    }

    /// The bytes of the module.
    pub fn finish(&self) -> Vec<u8> {
        let mut out = b"\0asm\x01\0\0\0".to_vec();
        section(&mut out, 1, self.signatures.len(), |s| {
            for signature in &self.signatures {
                s.push(0x60);
                kinds(s, &signature.params);
                kinds(s, &signature.results);
            }
        });
        section(&mut out, 2, self.imports.len(), |s| {
            for (effect, operation, signature) in &self.imports {
                name(s, effect);
                name(s, operation);
                s.push(0x00);
                unsigned(s, u64::from(*signature));
            }
        });
        section(&mut out, 3, self.functions.len(), |s| {
            for signature in &self.functions {
                unsigned(s, u64::from(*signature));
            }
        });
        section(&mut out, 5, 1, |s| {
            // No greatest size: the module asks the host for pages as it needs them.
            s.push(0x00);
            unsigned(s, u64::from(self.pages));
        });
        section(&mut out, 6, self.globals.len(), |s| {
            for global in &self.globals {
                s.extend([Kind::I32.byte(), 0x01, super::wasm_code::I32_CONST]);
                super::wasm_code::signed(s, i64::from(global.initial));
                s.push(super::wasm_code::END);
            }
        });
        section(&mut out, 7, self.exports.len(), |s| {
            for (exported, what) in &self.exports {
                name(s, exported);
                match what {
                    Export::Function(f) => {
                        s.push(0x00);
                        unsigned(s, u64::from(*f));
                    }
                    Export::Memory => {
                        s.push(0x02);
                        s.push(0);
                    }
                }
            }
        });
        section(&mut out, 10, self.bodies.len(), |s| {
            for body in &self.bodies {
                let mut b = Vec::new();
                unsigned(&mut b, body.locals.len() as u64);
                for (count, kind) in &body.locals {
                    unsigned(&mut b, u64::from(*count));
                    b.push(kind.byte());
                }
                b.extend(&body.code);
                unsigned(s, b.len() as u64);
                s.extend(b);
            }
        });
        section(&mut out, 11, self.data.len(), |s| {
            for (at, bytes) in &self.data {
                s.push(0x00);
                s.push(super::wasm_code::I32_CONST);
                super::wasm_code::signed(s, i64::from(*at));
                s.push(super::wasm_code::END);
                unsigned(s, bytes.len() as u64);
                s.extend(bytes);
            }
        });
        out
    }
}

fn kinds(out: &mut Vec<u8>, kinds: &[Kind]) {
    unsigned(out, kinds.len() as u64);
    out.extend(kinds.iter().map(|k| k.byte()));
}

fn name(out: &mut Vec<u8>, text: &str) {
    unsigned(out, text.len() as u64);
    out.extend(text.as_bytes());
}

/// Writes a section of `count` items, which `items` writes, unless it has none.
fn section(out: &mut Vec<u8>, id: u8, count: usize, items: impl FnOnce(&mut Vec<u8>)) {
    if count == 0 {
        return;
    }
    let mut body = Vec::new();
    unsigned(&mut body, count as u64);
    items(&mut body);
    out.push(id);
    unsigned(out, body.len() as u64);
    out.extend(body);
}
