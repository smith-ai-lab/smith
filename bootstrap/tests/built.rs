//! What a page of the language says is built, which `.github/check-status.sh`
//! checks, so it is tested here without GitHub.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "a_partial_page_passes_with_a_list_of_what_is_built",
        a_partial_page_passes_with_a_list_of_what_is_built,
    ),
    (
        "a_partial_page_fails_with_no_list_or_a_line_of_text_in_its_place",
        a_partial_page_fails_with_no_list_or_a_line_of_text_in_its_place,
    ),
];

fn script() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../.github/check-status.sh")
}

/// Whether the script passes a folder holding one page with this front matter.
fn passes(front: &str) -> bool {
    static COUNT: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir().join(format!(
        "smith-built-{}-{}",
        std::process::id(),
        COUNT.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("page.md"), format!("---\n{front}---\n\n# Page\n")).unwrap();
    let status = Command::new("sh")
        .arg(script())
        .arg(&dir)
        .output()
        .unwrap()
        .status;
    std::fs::remove_dir_all(&dir).unwrap();
    status.success()
}

fn a_partial_page_passes_with_a_list_of_what_is_built() {
    assert!(passes("status: partial\nbuilt:\n  - \"one\"\n"));
    assert!(passes(
        "status: partial\nbuilt:\n  - \"one\"\n  - \"two\"\ntype: specification\n"
    ));
}

fn a_partial_page_fails_with_no_list_or_a_line_of_text_in_its_place() {
    assert!(!passes("status: partial\n"));
    assert!(!passes("status: partial\nbuilt:\n"));
    assert!(!passes("status: partial\nbuilt: \"one; two\"\n"));
    assert!(!passes("status: built\nbuilt: \"one\"\n"));
}
