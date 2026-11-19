//! Where tests keep the files they make.
//!
//! A run makes one folder under the system's temporary folder, named by the
//! process id, empties it if an earlier run with the same process id left it,
//! and removes it when the run ends. Every file and folder a test makes is
//! inside it, at a path that nothing was at when it was given, so no test
//! sees what another run or another test left, and the temporary folder is as
//! it was before the run.

use std::path::{Path, PathBuf};
use std::sync::OnceLock;
use std::sync::atomic::{AtomicUsize, Ordering};

/// The folder for a run in `base`, made empty whatever an earlier run left.
pub fn prepare(base: &Path, process: u32) -> PathBuf {
    let root = base.join(format!("smith-tests-{process}"));
    let _ = std::fs::remove_dir_all(&root);
    std::fs::create_dir_all(&root).unwrap();
    root
}

fn root() -> &'static Path {
    static ROOT: OnceLock<PathBuf> = OnceLock::new();
    ROOT.get_or_init(|| prepare(&std::env::temp_dir(), std::process::id()))
}

/// A path, named after `label`, that nothing is at. The caller makes it.
pub fn path(label: &str) -> PathBuf {
    static COUNT: AtomicUsize = AtomicUsize::new(0);
    root().join(format!("{label}-{}", COUNT.fetch_add(1, Ordering::Relaxed)))
}

/// The temporary folder of a program a test starts, inside the folder of the
/// run, so that what a script makes with `mktemp` and leaves goes with it.
pub fn for_programs() -> &'static Path {
    static PROGRAMS: OnceLock<PathBuf> = OnceLock::new();
    PROGRAMS.get_or_init(|| {
        let dir = root().join("programs");
        std::fs::create_dir_all(&dir).unwrap();
        dir
    })
}

/// Removes everything the run made. Called when the run ends.
pub fn remove() {
    let _ = std::fs::remove_dir_all(root());
}
