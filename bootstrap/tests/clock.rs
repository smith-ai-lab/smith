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

use std::time::Duration;

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

/// How long a test, or a command a test starts, may take on the wall before it
/// is taken to never end. It is far above what one that ends takes, even on a
/// machine that is busy, as the limit counts only the time a test spends
/// computing, and a busy machine stretches the wall.
pub const HANG: Duration = Duration::from_secs(10);

/// Runs the command and gives what it did, as `Command::output` does, except
/// that a command still running after `HANG` is killed, and the test
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
    use std::process::Stdio;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::Instant;
    static COUNT: AtomicUsize = AtomicUsize::new(0);
    let name = format!(
        "smith-out-{}-{}",
        std::process::id(),
        COUNT.fetch_add(1, Ordering::Relaxed)
    );
    let (out, err) = (
        std::env::temp_dir().join(format!("{name}.out")),
        std::env::temp_dir().join(format!("{name}.err")),
    );
    let begin = Instant::now();
    command
        .stdin(Stdio::null())
        .stdout(std::fs::File::create(&out).unwrap());
    if with_stderr {
        command.stderr(std::fs::File::create(&err).unwrap());
    } else {
        command.stderr(Stdio::null());
    }
    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(e) => {
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
        if begin.elapsed() > HANG {
            child.kill().unwrap();
            child.wait().unwrap();
            break None;
        }
        std::thread::sleep(Duration::from_millis(1));
    };
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
    if with_stderr {
        std::fs::remove_file(&err).unwrap();
    }
    Ok(output.unwrap_or_else(|| {
        panic!(
            "the program never ends: it ran for more than {} ms",
            HANG.as_millis()
        )
    }))
}
