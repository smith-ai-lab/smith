//! The targets the compiler builds for. Each target turns the program into
//! the bytes of an executable.

mod arm64;
mod elf;
mod linux_arm64;
mod linux_x64;
mod macho;
mod macos_arm64;
mod macos_x64;
mod pe;
mod windows_arm64;
mod windows_x64;
mod x86_64;

use crate::ir::Program;
use crate::scalar;

pub use x86_64::Frame;

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
        }
    }

    /// Whether the executables for the target are built from the scalar form
    /// and not from the form `emit` takes.
    pub fn from_scalar(self) -> bool {
        match self {
            Target::LinuxX64 | Target::MacosX64 | Target::WindowsX64 => true,
            Target::MacosArm64 | Target::LinuxArm64 | Target::WindowsArm64 => false,
        }
    }

    /// The executable for the program, or `None` if the target is built
    /// from the scalar form, or the compiler cannot build for it yet.
    pub fn emit(self, program: &Program) -> Option<Vec<u8>> {
        match self {
            Target::MacosArm64 => Some(macos_arm64::emit(program)),
            Target::LinuxArm64 => Some(linux_arm64::emit(program)),
            Target::WindowsArm64 => Some(windows_arm64::emit(program)),
            Target::LinuxX64 | Target::MacosX64 | Target::WindowsX64 => None,
        }
    }

    /// The executable for the program in the scalar form, or `None` if the
    /// target is not built from it yet.
    pub fn emit_scalar(self, program: &scalar::Program) -> Option<Vec<u8>> {
        match self {
            Target::LinuxX64 => Some(linux_x64::emit(program)),
            Target::MacosX64 => Some(macos_x64::emit(program)),
            Target::WindowsX64 => Some(windows_x64::emit(program)),
            Target::MacosArm64 | Target::LinuxArm64 | Target::WindowsArm64 => None,
        }
    }

    /// What each function's frame holds in the executable for the program,
    /// or `None` if the target is not built from the scalar form.
    pub fn frames(self, program: &scalar::Program) -> Option<Vec<Frame>> {
        self.from_scalar().then(|| x86_64::frames(program))
    }
}
