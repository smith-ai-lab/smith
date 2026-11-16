# Smith

Smith is a programming language written mainly by models, for linear algebra with automatic differentiation, concurrent servers, and native applications.

- [knowledge/](knowledge/index.md) says what Smith is: the language, its grammar, and the compiler's design.
- [.agents/skills/](.agents/skills/) says how to work on it.
- [bootstrap/](bootstrap/) is the first compiler, written in Rust.

The compiler builds a small part of the language so far, and [knowledge/compiler.md](knowledge/compiler.md#bootstrap) says how it grows.

## Install

On macOS or Linux, one line installs the newest release as `~/.local/bin/smith`:

```
curl -fsSL https://raw.githubusercontent.com/smith-ai-lab/smith/main/install.sh | sh
```

The compiler is one file with no dependencies. Every [release](https://github.com/smith-ai-lab/smith/releases) holds it for six kinds of machine.

| Machine | File |
| --- | --- |
| macOS on Apple silicon | `smith-macos_arm64` |
| macOS on Intel | `smith-macos_x64` |
| Linux on x86-64 | `smith-linux_x64` |
| Linux on arm64 | `smith-linux_arm64` |
| Windows on x86-64 | `smith-windows_x64.exe` |
| Windows on arm64 | `smith-windows_arm64.exe` |

On Windows, in PowerShell:

```
curl.exe -L -o smith.exe https://github.com/smith-ai-lab/smith/releases/latest/download/smith-windows_x64.exe
.\smith.exe --version
```

Move the file to a folder on your path to run `smith` from anywhere. Each release also holds `SHA256SUMS`, the hash of every file in it.
