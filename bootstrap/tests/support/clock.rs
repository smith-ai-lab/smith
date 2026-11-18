//! How long a test spent computing: the processor time of the thread that
//! ran it. A busy machine makes a test wait for the processor, and the
//! clock on the wall then runs on while the test does not, so the wall
//! clock gives a quick test a slow verdict. Processor time does not run
//! while the thread waits, so the verdict of a test is the same alone and
//! beside every other test.
//!
//! The time of a program a test starts, or of a thread it starts, is not the
//! time of its own thread. A test that waits on either has not spent that
//! time computing, so a test starts no thread, and starts a program only
//! through `output_within`, which gives the program the limit of a test on
//! the wall and fails the test when the program has not ended by then.
//!
//! The run takes a test to hang only when nothing has ended for the whole of
//! the limit: not the test, and not a program it started through
//! `output_within`. A program that ends is progress, so a test that starts
//! many programs, each of which ends in time, never hangs however long
//! together they take.

use std::cell::RefCell;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

/// When a test last made progress: it began, or a program it started began or
/// ended.
pub type Progress = Arc<Mutex<Instant>>;

thread_local! {
    static WATCHED: RefCell<Option<Progress>> = const { RefCell::new(None) };
}

/// Makes the calling thread's progress the one the run watches, until it is
/// called again.
pub fn watch(progress: Option<Progress>) {
    WATCHED.with(|w| *w.borrow_mut() = progress);
}

fn progressed() {
    WATCHED.with(|w| {
        if let Some(progress) = &*w.borrow() {
            *progress.lock().unwrap() = Instant::now();
        }
    });
}

/// The processor time the calling thread has used so far. Where the
/// machine gives none, the time since the first call, which is all the
/// limit needs there, as it is enforced where this is the processor time.
#[cfg(unix)]
pub fn thread_time() -> Duration {
    use std::ffi::{c_int, c_long};

    #[repr(C)]
    struct Timespec {
        seconds: c_long,
        nanoseconds: c_long,
    }
    #[cfg(target_os = "linux")]
    const THREAD_CPUTIME: c_int = 3;
    #[cfg(target_os = "macos")]
    const THREAD_CPUTIME: c_int = 16;
    unsafe extern "C" {
        fn clock_gettime(clock: c_int, into: *mut Timespec) -> c_int;
    }
    let mut now = Timespec {
        seconds: 0,
        nanoseconds: 0,
    };
    // SAFETY: `now` is a `timespec`, which is what the call fills in.
    let status = unsafe { clock_gettime(THREAD_CPUTIME, &mut now) };
    assert_eq!(status, 0, "the machine gives no processor time");
    Duration::new(now.seconds as u64, now.nanoseconds as u32)
}

#[cfg(not(unix))]
pub fn thread_time() -> Duration {
    use std::sync::OnceLock;
    use std::time::Instant;
    static FIRST: OnceLock<Instant> = OnceLock::new();
    FIRST.get_or_init(Instant::now).elapsed()
}

/// How long a test may go on the wall without anything ending, and how long a
/// command a test starts may take, before it is taken to never end. It is far above what one that ends takes, even on a
/// machine that is busy, as the limit counts only the time a test spends
/// computing, and a busy machine stretches the wall.
const HANG: Duration = Duration::from_secs(10);

static HANG_MILLISECONDS: AtomicU64 = AtomicU64::new(HANG.as_millis() as u64);

/// The limit, which the run's own tests make small with `--hang`.
pub fn hang() -> Duration {
    Duration::from_millis(HANG_MILLISECONDS.load(Ordering::Relaxed))
}

pub fn set_hang(limit: Duration) {
    HANG_MILLISECONDS.store(limit.as_millis() as u64, Ordering::Relaxed);
}

/// Runs the command and gives what it did, as `Command::output` does, except
/// that a command still running after the limit is killed, and the test
/// fails saying that the program never ends. So a program that does not end is
/// a failing test, and not a run that does not end. What the command writes
/// to standard error is given only when `with_stderr` is, and is dropped
/// otherwise, as a file for it costs a run on a machine where files are slow.
///
/// What the command writes goes to files and not to pipes, so that the test
/// reads nothing while it waits and needs no thread to do it.
pub fn output_within(
    command: &mut std::process::Command,
    with_stderr: bool,
) -> std::io::Result<std::process::Output> {
    run(command, None, with_stderr)
}

/// `output_within` for a command that reads its standard input: the command
/// is given `input` as a file to read, so that the test writes to no pipe and
/// needs no thread, and a command that stops reading early blocks nothing.
pub fn output_within_given(
    command: &mut std::process::Command,
    input: &[u8],
    with_stderr: bool,
) -> std::io::Result<std::process::Output> {
    run(command, Some(input), with_stderr)
}

fn run(
    command: &mut std::process::Command,
    input: Option<&[u8]>,
    with_stderr: bool,
) -> std::io::Result<std::process::Output> {
    use std::process::Stdio;
    use std::sync::atomic::{AtomicUsize, Ordering};
    static COUNT: AtomicUsize = AtomicUsize::new(0);
    let name = format!(
        "smith-out-{}-{}",
        std::process::id(),
        COUNT.fetch_add(1, Ordering::Relaxed)
    );
    let (out, err, inp) = (
        std::env::temp_dir().join(format!("{name}.out")),
        std::env::temp_dir().join(format!("{name}.err")),
        std::env::temp_dir().join(format!("{name}.in")),
    );
    let begin = Instant::now();
    progressed();
    match input {
        Some(bytes) => {
            std::fs::write(&inp, bytes).unwrap();
            command.stdin(std::fs::File::open(&inp).unwrap());
        }
        None => {
            command.stdin(Stdio::null());
        }
    }
    command.stdout(std::fs::File::create(&out).unwrap());
    if with_stderr {
        command.stderr(std::fs::File::create(&err).unwrap());
    } else {
        command.stderr(Stdio::null());
    }
    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(e) => {
            if input.is_some() {
                std::fs::remove_file(&inp).unwrap();
            }
            std::fs::remove_file(&out).unwrap();
            if with_stderr {
                std::fs::remove_file(&err).unwrap();
            }
            return Err(e);
        }
    };
    let status = loop {
        if let Some(status) = child.try_wait().unwrap() {
            break Some(status);
        }
        if begin.elapsed() > hang() {
            child.kill().unwrap();
            child.wait().unwrap();
            break None;
        }
        std::thread::sleep(Duration::from_millis(1));
    };
    progressed();
    let output = status.map(|status| std::process::Output {
        status,
        stdout: std::fs::read(&out).unwrap(),
        stderr: if with_stderr {
            std::fs::read(&err).unwrap()
        } else {
            Vec::new()
        },
    });
    std::fs::remove_file(&out).unwrap();
    if input.is_some() {
        std::fs::remove_file(&inp).unwrap();
    }
    if with_stderr {
        std::fs::remove_file(&err).unwrap();
    }
    Ok(output.unwrap_or_else(|| {
        panic!(
            "the program never ends: it ran for more than {} ms",
            hang().as_millis()
        )
    }))
}
