//! The lexer in `compiler/lexer.smith` as a module under `node`, beside the
//! same program as an executable of this machine: the time each takes for a
//! byte, which prints the same count of tokens, the size of the module and of
//! the executable, and the time and the instructions of building the module.
//! The build of the same program for `linux_x64` is printed beside it. The instructions are from `valgrind --tool=callgrind`, when it is installed.

use std::path::Path;
use std::process::Command;

use crate::build::{instructions, per_byte, size};
use crate::{Host, lex_for};

/// The size of the input, in bytes, and how many times it is lexed in a run,
/// which is long enough for the engine to have optimized the module.
const SIZE: usize = 1 << 18;
const ROUNDS: usize = 40;

pub fn run(smith: &Path, work: &Path, lexer_source: &str) {
    if Command::new("node").arg("--version").output().is_err() {
        println!("wasm skipped: node is not installed");
        return;
    }
    for (who, host) in [("native", Host::Native), ("node", Host::Node)] {
        let (bytes, tokens, time, _) = lex_for(smith, work, lexer_source, SIZE, host, Some(ROUNDS));
        let file = match host {
            Host::Native => "lexer-whole",
            Host::Node => "lexer-wasm-whole.wasm",
        };
        println!(
            "wasm {who} tokens={tokens} size_bytes={} ns_per_byte={}",
            size(&work.join(file)),
            per_byte(time, bytes)
        );
    }
    // The build of each, from the program the runs above left.
    for target in ["linux_x64", "wasm"] {
        let source = work.join(match target {
            "wasm" => "lexer-wasm-whole.smith",
            _ => "lexer-whole.smith",
        });
        let start = std::time::Instant::now();
        let built = std::process::Command::new(smith)
            .args(["build", "--target", target])
            .arg(&source)
            .current_dir(work)
            .status()
            .map(|s| s.success())
            .unwrap_or(false);
        assert!(built, "smith build --target {target} failed");
        let took = start.elapsed();
        let ir = instructions(
            &[
                smith.as_os_str(),
                "build".as_ref(),
                "--target".as_ref(),
                target.as_ref(),
                source.as_os_str(),
            ],
            None,
            work,
        );
        println!(
            "wasm build {target} total_ms={:.2} instructions={}",
            took.as_secs_f64() * 1000.0,
            ir.map_or("unknown".to_string(), |n| n.to_string())
        );
    }
}
