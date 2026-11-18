//! A copy of an executable, made so that running it cannot be refused as
//! busy.
//!
//! The tests run in one process, on many threads, and a thread that starts a
//! program forks the whole process. A copy that the process writes is open
//! for writing in it, so a fork on another thread gives the child that open
//! file, and the child keeps it until it runs its own program. Running the
//! copy in that moment fails with "Text file busy", though the copy was closed
//! long before. Another process writes a copy that this one never holds open,
//! so no fork of this process can hand it on.
//!
//! The same holds for a file the test builds itself, such as an executable
//! the compiler gave back as bytes, or a script. `write` has `install`, a
//! process of its own, make the file from a copy of the bytes that is never
//! run, so no test writes in its own process a file that it then runs.

use std::path::Path;
use std::process::Command;

/// Copies the executable at `from` to `to`, and gives a file that can be run
/// at once.
#[cfg(unix)]
pub fn copy(from: &str, to: &Path) {
    let copied = crate::clock::output_within(Command::new("cp").arg(from).arg(to), true).unwrap();
    assert!(copied.status.success(), "{}", crate::cli::stderr(&copied));
}

/// Only Unix refuses to run a file that a process has open for writing, so
/// elsewhere the copy is made in this process.
#[cfg(not(unix))]
pub fn copy(from: &str, to: &Path) {
    std::fs::copy(from, to).unwrap();
}

/// Writes `bytes` to `to` as a file that can be run at once. The bytes go
/// first to a file beside it that is never run, and `install` makes `to`
/// from that, in a process of its own.
#[cfg(unix)]
pub fn write(to: &Path, bytes: &[u8]) {
    let mut data = to.as_os_str().to_owned();
    data.push(".data");
    let data = std::path::PathBuf::from(data);
    std::fs::write(&data, bytes).unwrap();
    let made = crate::clock::output_within(
        Command::new("install").args(["-m", "755"]).arg(&data).arg(to),
        true,
    )
    .unwrap();
    std::fs::remove_file(&data).unwrap();
    assert!(made.status.success(), "{}", crate::cli::stderr(&made));
}

#[cfg(not(unix))]
pub fn write(to: &Path, bytes: &[u8]) {
    std::fs::write(to, bytes).unwrap();
}
