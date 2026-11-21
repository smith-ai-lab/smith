//! The file of JavaScript that loads a module and supplies the functions it
//! imports. The compiler knows no host: this is what a host with a JavaScript
//! entry needs, and it supplies each import the module has from the list of
//! them, and no other. It runs under `node`, which is the host of the tests.

/// The JavaScript that loads the module beside it, which has these imports,
/// named by the effect and the operation.
pub fn script(imports: &[(&str, &str)]) -> String {
    let mut out = String::from(
        r#"// Written by smith. It loads the module beside it and runs its `main`.
"use strict";
const fs = require("fs");
let memory;
const imports = {
"#,
    );
    for (effect, operation) in imports {
        out.push_str(&format!("  {effect}: {{\n"));
        out.push_str(&operation_script(effect, operation));
        out.push_str("  },\n");
    }
    out.push_str(
        r#"};
const bytes = fs.readFileSync(__filename.replace(/\.js$/, ".wasm"));
const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes), imports);
memory = instance.exports.memory;
// A program that ends before `main` does, as one does that the host refuses it
// memory, leaves its status, and the module traps.
try {
  instance.exports.main();
  process.exitCode = 0;
} catch (error) {
  const status = instance.exports.status();
  if (status < 0) throw error;
  process.exitCode = status;
}
"#,
    );
    out
}

/// The function that supplies an operation of an effect.
fn operation_script(effect: &str, operation: &str) -> String {
    match (effect, operation) {
        // Writes `length` bytes at `address` to standard output, when `to` is
        // 1, and to standard error, when it is 2.
        ("console", "print") => r#"    print(to, address, length) {
      const text = new Uint8Array(memory.buffer, address, length);
      let written = 0;
      while (written < length) {
        try {
          written += fs.writeSync(to, text, written, length - written);
        } catch (error) {
          if (error.code !== "EAGAIN") throw error;
        }
      }
    },
"#
        .to_string(),
        // Fills `length` bytes at `address` with bytes nobody can predict, in
        // pieces of at most 65536, which is the most `getRandomValues` takes,
        // and says whether it did: 0 when it did, and 1 when the host refused.
        ("entropy", "fill") => r#"    fill(address, length) {
      try {
        for (let at = 0; at < length; at += 65536) {
          const piece = Math.min(65536, length - at);
          globalThis.crypto.getRandomValues(new Uint8Array(memory.buffer, address + at, piece));
        }
        return 0;
      } catch (error) {
        return 1;
      }
    },
"#
        .to_string(),
        _ => panic!("no host supplies the operation {operation} of {effect}"),
    }
}
