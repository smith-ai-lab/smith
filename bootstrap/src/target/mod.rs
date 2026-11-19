//! The targets the compiler builds for. Each target turns the program into
//! the bytes of an executable.

pub mod alloc;
mod arm64;
mod branch;
mod elf;
mod frame;
mod indexed;
mod linux_arm64;
mod linux_x64;
mod macho;
mod macos_arm64;
mod macos_x64;
pub mod moves;
mod pe;
mod wasm;
mod wasm_code;
mod wasm_host;
mod wasm_module;
mod wasm_structure;
mod windows_arm64;
mod windows_x64;
mod x86_64;

/// Whether the blocks of a function are written as loops and branches by a
/// target that writes a module: every jump back goes to a block that dominates
/// the jump.
pub use wasm_structure::is_structured;

use crate::scalar::{Place, Program, Type};

/// What a part of the code of an executable is: a function of the program, by
/// its place, or a piece the compiler adds, by its name in angle brackets.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Named {
    Function(usize),
    Piece(&'static str),
}

/// A run of the code of a function that was built from one place in the source:
/// where it is, how many bytes it takes, and the place.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Run {
    pub address: u64,
    pub size: u64,
    pub place: Place,
}

/// A part of the code of an executable: where it is, as the address the
/// executable asks to be loaded at, or for a module the place of the byte in
/// the file, how many bytes it takes, and how many bytes of frame it takes of
/// the stack when it runs. The parts of an executable are in the order of their
/// addresses and cover its code, with no gap and no overlap. `at` is the runs
/// of a function that has places, which cover it the same way.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Part {
    pub named: Named,
    pub address: u64,
    pub size: u64,
    pub frame: u32,
    pub at: Vec<Run>,
}

/// What a target lays out for a program: the code, the data it reads each under
/// its label, and the parts of the code.
type Laid<'a, A> = (A, Vec<(usize, &'a [u8])>, Vec<Part>);

/// The runs of the code from `from` to `to`, given where the code of each
/// instruction that has a new place begins. A run is the code of the
/// instructions that have one place: a line of the source, or the piece of the
/// runtime or the function that no line wrote, as the code before the first
/// instruction is, which is the function's own, named `piece`. The code of an instruction whose
/// place says nothing, as a constant that a pass makes, is part of the run before
/// it. A function whose code is all of one place, which is its own, as the
/// runtime's is, has no runs.
fn runs(marks: &[(usize, Place)], from: usize, to: usize, piece: Place) -> Vec<Run> {
    let first = marks.partition_point(|(at, _)| *at < from);
    let last = marks.partition_point(|(at, _)| *at < to);
    let marks = &marks[first..last];
    let mut runs: Vec<(usize, Place)> = Vec::new();
    for &(at, place) in marks {
        if place.is_none() || runs.last().is_some_and(|(_, p)| *p == place) {
            continue;
        }
        if runs.last().is_some_and(|(start, _)| *start == at) {
            runs.pop();
        }
        runs.push((at, place));
    }
    if runs.is_empty() {
        return Vec::new();
    }
    // The code before the first instruction, which no line wrote, is the
    // function's own, and a function with no name has it in its first run.
    if runs[0].0 > from && !piece.is_none() {
        runs.insert(0, (from, piece));
        // The code before the first instruction is one run with that of the
        // instructions that are the function's own.
        if runs.get(1).is_some_and(|(_, p)| *p == piece) {
            runs.remove(1);
        }
    } else {
        runs[0].0 = from;
    }
    // One run that is the function's own says nothing that its line does not.
    if runs.len() == 1 && runs[0].1 == piece {
        return Vec::new();
    }
    (0..runs.len())
        .map(|k| {
            let end = runs.get(k + 1).map_or(to, |(at, _)| *at);
            Run {
                address: runs[k].0 as u64,
                size: (end - runs[k].0) as u64,
                place: runs[k].1,
            }
        })
        .collect()
}

/// The parts of the code of a machine code executable, as places in the code,
/// from where each function begins, how large its frame is, where the code
/// ends, and where the code of each instruction that has a new place begins.
/// What comes before the first function is the start of the executable.
fn parts(
    offsets: &[usize],
    frames: &[u32],
    end: usize,
    marks: &[(usize, Place)],
    pieces: &[Place],
) -> Vec<Part> {
    let first = offsets.first().copied().unwrap_or(end);
    let mut parts = vec![Part {
        named: Named::Piece("<start>"),
        address: 0,
        size: first as u64,
        frame: 0,
        at: Vec::new(),
    }];
    for (k, (&at, &frame)) in offsets.iter().zip(frames).enumerate() {
        let to = offsets.get(k + 1).copied().unwrap_or(end);
        parts.push(Part {
            named: Named::Function(k),
            address: at as u64,
            size: (to - at) as u64,
            frame,
            at: runs(marks, at, to, pieces[k]),
        });
    }
    parts
}

/// The parts with each address that was a place in the code moved to where the
/// code is loaded.
fn shifted(mut parts: Vec<Part>, base: u64) -> Vec<Part> {
    for part in &mut parts {
        part.address += base;
        for run in &mut part.at {
            run.address += base;
        }
    }
    parts
}

/// The map of an executable, one `fn` line for each part of its code: the
/// address, the size, the frame, and the name, each word after a space. After a
/// function's line come its `at` lines, one for each run of its code: the
/// address, the size, the file, the line, and the column, and the name of the
/// function the place is written in.
pub fn map(names: &[String], parts: &[Part]) -> String {
    let mut text = String::new();
    for part in parts {
        let name = match &part.named {
            Named::Piece(piece) => piece.to_string(),
            Named::Function(k) => match names[*k].as_str() {
                "" => format!("<function{k}>"),
                name => name.to_string(),
            },
        };
        text.push_str(&format!(
            "fn {:#x} {} {} {name}\n",
            part.address, part.size, part.frame
        ));
        for run in &part.at {
            let place = run.place;
            // Code that no line of the source wrote has a dash for its place.
            let at = match place.is_source() {
                true => format!(
                    "{}:{}:{}",
                    crate::scalar::names::name(place.file),
                    place.line,
                    place.col
                ),
                false => "-".to_string(),
            };
            text.push_str(&format!(
                "at {:#x} {} {at} {}\n",
                run.address,
                run.size,
                crate::scalar::names::name(place.function),
            ));
        }
    }
    text
}

/// Declares the `Target` enum along with `Target::ALL`, its list of cases,
/// and each case's name, so that a case cannot be added without joining
/// the list.
macro_rules! targets {
    ($($case:ident => $name:literal,)*) => {
        /// A target, named as its case in the `Target` enum.
        #[derive(Clone, Copy, Debug, PartialEq, Eq)]
        pub enum Target {
            $($case,)*
        }

        impl Target {
            /// Every case of the enum, in the order they are declared.
            pub const ALL: &[Target] = &[$(Target::$case,)*];

            pub fn name(self) -> &'static str {
                match self {
                    $(Target::$case => $name,)*
                }
            }
        }
    };
}

targets! {
    LinuxX64 => "linux_x64",
    MacosArm64 => "macos_arm64",
    LinuxArm64 => "linux_arm64",
    MacosX64 => "macos_x64",
    WindowsX64 => "windows_x64",
    WindowsArm64 => "windows_arm64",
    Wasm => "wasm",
}

impl Target {
    pub fn from_name(name: &str) -> Option<Target> {
        Target::ALL.iter().copied().find(|t| t.name() == name)
    }

    /// The target of the machine the compiler is running on, if it is one.
    pub fn host() -> Option<Target> {
        if cfg!(all(target_os = "linux", target_arch = "x86_64")) {
            Some(Target::LinuxX64)
        } else if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
            Some(Target::MacosArm64)
        } else if cfg!(all(target_os = "linux", target_arch = "aarch64")) {
            Some(Target::LinuxArm64)
        } else if cfg!(all(target_os = "macos", target_arch = "x86_64")) {
            Some(Target::MacosX64)
        } else if cfg!(all(target_os = "windows", target_arch = "x86_64")) {
            Some(Target::WindowsX64)
        } else if cfg!(all(target_os = "windows", target_arch = "aarch64")) {
            Some(Target::WindowsArm64)
        } else {
            None
        }
    }

    /// The machine the compiler is running on, as an error names it.
    pub fn host_description() -> String {
        format!("{} on {}", std::env::consts::OS, std::env::consts::ARCH)
    }

    /// The name of the executable built from an entry file whose name,
    /// without `.smith`, is `stem`.
    pub fn executable_name(self, stem: &str) -> String {
        match self {
            Target::LinuxX64 | Target::MacosArm64 | Target::LinuxArm64 | Target::MacosX64 => {
                stem.to_string()
            }
            Target::WindowsX64 | Target::WindowsArm64 => format!("{stem}.exe"),
            Target::Wasm => format!("{stem}.wasm"),
        }
    }

    /// The executable for the program. A program with a vector is refused by a
    /// target that does not build them, with one message that names the first
    /// function that has one.
    pub fn emit(self, program: &Program) -> Vec<u8> {
        self.emit_parts(program).0
    }

    /// The executable for the program, and the parts of its code, which the
    /// map of the executable is made of.
    pub fn emit_parts(self, program: &Program) -> (Vec<u8>, Vec<Part>) {
        if !self.builds_vectors()
            && let Some(function) = (program.functions.iter())
                .position(|f| f.types.iter().any(|ty| matches!(ty, Type::Vector(_))))
        {
            panic!(
                "{} does not build vectors yet: function {function} has one",
                self.name()
            );
        }
        match self {
            Target::LinuxX64 => linux_x64::emit(program),
            Target::MacosX64 => macos_x64::emit(program),
            Target::WindowsX64 => windows_x64::emit(program),
            Target::MacosArm64 => macos_arm64::emit(program),
            Target::LinuxArm64 => linux_arm64::emit(program),
            Target::WindowsArm64 => windows_arm64::emit(program),
            Target::Wasm => wasm::emit(program),
        }
    }

    /// Whether the target builds a function that has a float.
    pub fn builds_floats(self) -> bool {
        match self {
            Target::LinuxX64
            | Target::MacosX64
            | Target::WindowsX64
            | Target::MacosArm64
            | Target::LinuxArm64
            | Target::WindowsArm64 => true,
            Target::Wasm => false,
        }
    }

    /// Whether the target builds a function that has a vector. A target that
    /// does not stops, with one message that names the function.
    pub fn builds_vectors(self) -> bool {
        match self {
            Target::LinuxX64
            | Target::MacosX64
            | Target::WindowsX64
            | Target::MacosArm64
            | Target::LinuxArm64
            | Target::WindowsArm64 => true,
            Target::Wasm => false,
        }
    }

    /// Whether what `emit` gives is a program for a chip, which the system
    /// runs, and not a module that a host runs.
    pub fn is_machine_code(self) -> bool {
        match self {
            Target::LinuxX64
            | Target::MacosArm64
            | Target::LinuxArm64
            | Target::MacosX64
            | Target::WindowsX64
            | Target::WindowsArm64 => true,
            Target::Wasm => false,
        }
    }

    /// The files that are written beside the executable, each by the
    /// extension its name takes in place of the executable's, and what it holds.
    /// A module is loaded by a file of JavaScript, which supplies what the
    /// module imports.
    pub fn beside(self, program: &Program) -> Vec<(&'static str, Vec<u8>)> {
        match self {
            Target::LinuxX64
            | Target::MacosArm64
            | Target::LinuxArm64
            | Target::MacosX64
            | Target::WindowsX64
            | Target::WindowsArm64 => Vec::new(),
            Target::Wasm => vec![(
                "js",
                wasm_host::script(&wasm::imports(program)).into_bytes(),
            )],
        }
    }
}
