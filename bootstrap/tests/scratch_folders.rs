//! Tests of where tests keep their files.

use crate::{Test, scratch};

pub const ONCE: &[Test] = &[
    (
        "a_folder_an_earlier_run_with_the_same_process_id_left_is_empty_when_given",
        a_folder_an_earlier_run_with_the_same_process_id_left_is_empty_when_given,
    ),
    (
        "a_path_is_one_nothing_is_at",
        a_path_is_one_nothing_is_at,
    ),
];

fn a_folder_an_earlier_run_with_the_same_process_id_left_is_empty_when_given() {
    let base = scratch::path("scratch-test");
    let process = 4242;
    let left = base.join(format!("smith-tests-{process}"));
    std::fs::create_dir_all(left.join("a/b")).unwrap();
    std::fs::write(left.join("a/b/file"), "left behind").unwrap();
    std::fs::write(left.join("hello.smith"), "left behind").unwrap();
    let given = scratch::prepare(&base, process);
    assert_eq!(given, left);
    assert_eq!(std::fs::read_dir(&given).unwrap().count(), 0);
}

fn a_path_is_one_nothing_is_at() {
    let paths: Vec<_> = (0..20).map(|_| scratch::path("same-label")).collect();
    for (i, path) in paths.iter().enumerate() {
        assert!(!path.exists());
        assert!(!paths[..i].contains(path));
    }
}
