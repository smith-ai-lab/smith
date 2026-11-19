//! Where the instructions of a Smith program go: by function and by line of
//! the source. The program is built with `smith build --map`, run under
//! `valgrind --tool=callgrind --dump-instr=yes`, and each address that the
//! run counted is put in the run of the map that holds it. What the program
//! does beyond the work that is measured is taken away by the same program
//! that does not do it, function by function and line by line, so what is
//! printed is the cost of the work alone.

use std::collections::HashMap;
use std::path::Path;
use std::process::{Command, Stdio};

/// How many functions, and how many lines, are printed.
const SHOWN: usize = 12;

/// The runs of a map: `(address, size, place, function)` for each `at` line,
/// in the order of the addresses.
type Runs = Vec<(u64, u64, String, String)>;

/// The functions of a map: `(address, size, name)` for each `fn` line.
type Functions = Vec<(u64, u64, String)>;

fn hex(word: &str) -> Option<u64> {
    u64::from_str_radix(word.strip_prefix("0x")?, 16).ok()
}

/// The `fn` and the `at` lines of a map.
fn read_map(path: &Path) -> Option<(Functions, Runs)> {
    let text = std::fs::read_to_string(path).ok()?;
    let (mut functions, mut runs) = (Vec::new(), Vec::new());
    for line in text.lines() {
        let words: Vec<&str> = line.split(' ').collect();
        match words.as_slice() {
            ["fn", address, size, _frame, name] => {
                functions.push((hex(address)?, size.parse().ok()?, name.to_string()));
            }
            ["at", address, size, place, function] => {
                let run = (
                    hex(address)?,
                    size.parse().ok()?,
                    place.to_string(),
                    function.to_string(),
                );
                runs.push(run);
            }
            _ => {}
        }
    }
    Some((functions, runs))
}

/// The instructions that the run counted at each address, from the file
/// `callgrind` wrote with `--dump-instr=yes`: the cost of each instruction by
/// itself, which is not the cost that a line after `calls=` gives for the call.
fn counted(path: &Path) -> Option<HashMap<u64, u64>> {
    let text = std::fs::read_to_string(path).ok()?;
    let mut at = 0u64;
    let mut call = false;
    let mut counts: HashMap<u64, u64> = HashMap::new();
    for line in text.lines() {
        if line.starts_with("calls=") {
            call = true;
            continue;
        }
        if !line.starts_with(|c: char| c.is_ascii_digit() || "+-*".contains(c)) {
            continue;
        }
        let words: Vec<&str> = line.split_whitespace().collect();
        let [position, _line, cost] = words.as_slice() else {
            continue;
        };
        at = match *position {
            "*" => at,
            p if p.starts_with("0x") => hex(p)?,
            p if p.starts_with('+') => at.wrapping_add(p[1..].parse().ok()?),
            p if p.starts_with('-') => at.wrapping_sub(p[1..].parse().ok()?),
            _ => continue,
        };
        if std::mem::take(&mut call) {
            continue;
        }
        *counts.entry(at).or_default() += cost.parse::<u64>().ok()?;
    }
    Some(counts)
}

/// What each function of the source, and each line of it, cost: the function
/// an instruction was written in and the line, where a run that no line wrote
/// is the piece of the runtime or the function that holds it. A name that has
/// the program's own in it has `program` instead, so that two programs that
/// differ in their names are put in the same place.
fn costs(
    stem: &str,
    (functions, runs): &(Functions, Runs),
    counts: &HashMap<u64, u64>,
) -> (HashMap<String, u64>, HashMap<String, u64>) {
    let (mut by_function, mut by_line) = (HashMap::new(), HashMap::new());
    for (&address, &count) in counts {
        let found = runs.partition_point(|r| r.0 <= address);
        let held = found
            .checked_sub(1)
            .map(|k| &runs[k])
            .filter(|r| address < r.0 + r.1);
        let (place, function) = match held {
            Some((_, _, place, function)) => (place.clone(), function.clone()),
            None => {
                let k = functions.partition_point(|f| f.0 <= address);
                let name = k
                    .checked_sub(1)
                    .map(|k| &functions[k])
                    .filter(|f| address < f.0 + f.1)
                    .map_or("<outside>".to_string(), |f| f.2.clone());
                ("-".to_string(), name)
            }
        };
        let line = match place.rsplit_once(':') {
            Some((line, _column)) if place != "-" => line.to_string(),
            _ => format!("- {function}"),
        };
        *by_function
            .entry(function.replace(stem, "program"))
            .or_default() += count;
        *by_line.entry(line.replace(stem, "program")).or_default() += count;
    }
    (by_function, by_line)
}

/// Builds the program with a map and runs it under callgrind, and gives what
/// each function and each line of it cost. `None` when `valgrind` is not
/// installed or a step fails.
fn profiled(
    smith: &Path,
    work: &Path,
    stem: &str,
) -> Option<(HashMap<String, u64>, HashMap<String, u64>)> {
    let status = Command::new(smith)
        .args(["build", "--map"])
        .arg(work.join(format!("{stem}.smith")))
        .current_dir(work)
        .stderr(Stdio::inherit())
        .status()
        .ok()?;
    assert!(status.success(), "smith build --map {stem} failed");
    let out = work.join(format!("{stem}.cg"));
    let status = Command::new("valgrind")
        .args(["--tool=callgrind", "--dump-instr=yes"])
        .arg(format!("--callgrind-out-file={}", out.display()))
        .arg(work.join(stem))
        .current_dir(work)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .ok()?;
    if !status.success() {
        return None;
    }
    let map = read_map(&work.join(format!("{stem}.map")))?;
    Some(costs(stem, &map, &counted(&out)?))
}

/// Prints the instructions a byte of the work that `whole` does and `base`
/// does not, by function and by line, for the program with the stem `whole`
/// and the one with the stem `base`, built as `reps` rounds of the work. The
/// biggest `SHOWN` of each are printed, with their share of all.
pub fn print(
    smith: &Path,
    work: &Path,
    label: &str,
    (whole, base): (&str, &str),
    reps: usize,
    bytes: usize,
) {
    let (Some(whole_costs), Some(base_costs)) =
        (profiled(smith, work, whole), profiled(smith, work, base))
    else {
        return;
    };
    let per = (reps * bytes) as f64;
    for (kind, whole, base) in [
        ("by_function", whole_costs.0, base_costs.0),
        ("by_line", whole_costs.1, base_costs.1),
    ] {
        let mut net: Vec<(String, u64)> = whole
            .into_iter()
            .map(|(key, n)| {
                let less = base.get(&key).copied().unwrap_or(0);
                (key, n.saturating_sub(less))
            })
            .filter(|(_, n)| *n > 0)
            .collect();
        net.sort_by(|a, b| b.1.cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
        let total: u64 = net.iter().map(|(_, n)| n).sum();
        println!(
            "{label} {kind} total instructions_per_byte={:.2}",
            total as f64 / per
        );
        for (key, n) in net.iter().take(SHOWN) {
            println!(
                "{label} {kind} {key} instructions_per_byte={:.2} share={:.1}%",
                *n as f64 / per,
                100.0 * *n as f64 / total.max(1) as f64
            );
        }
    }
}
