//! The targets the compiler builds for. Each target turns the program into
//! the bytes of an executable.

pub mod alloc;
mod arm64;
mod elf;
mod frame;
mod linux_arm64;
mod linux_x64;
mod macho;
mod macos_arm64;
mod macos_x64;
pub mod moves;
mod pe;
mod windows_arm64;
mod windows_x64;
mod x86_64;

use crate::scalar::Program;

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

    /// The executable for the program.
    pub fn emit(self, program: &Program) -> Vec<u8> {
        match self {
            Target::LinuxX64 => linux_x64::emit(program),
            Target::MacosX64 => macos_x64::emit(program),
            Target::WindowsX64 => windows_x64::emit(program),
            Target::MacosArm64 => macos_arm64::emit(program),
            Target::LinuxArm64 => linux_arm64::emit(program),
            Target::WindowsArm64 => windows_arm64::emit(program),
        }
    }
}
