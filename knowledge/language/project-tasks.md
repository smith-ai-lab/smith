---
type: specification
title: Project tasks
description: A public function in project.smith, run by name, whose parameters are its command-line flags and whose effects say what it may do.
status: planned
---

# Project tasks

A task is a public function in `project.smith`, run by name. `smith deploy --env staging` runs the task `deploy` with `env` set to `"staging"`.

```
pub fn deploy(env: str) -> Result[(), DeployError] ! fs, net, alloc {
  binary = try build(package, target: .linux_x64)
  upload(binary, to: env)
}
```

Every public function in `project.smith` is a command.

## Its parameters are its flags

A task's parameters are named flags. `env` is the flag `--env`. A flag is spelled exactly as its parameter is, so `batch_size` is `--batch_size`. No other spelling is taken: `--batch-size` names no parameter.

- A `bool` is a bare flag.
- A parameter with a default, or of type `Option`, may be left out.
- There are no positional arguments.

```
pub fn deploy(env: str, verbose: bool, region: str = "eu", note: Option[str]) -> Result[(), DeployError] ! fs, net, alloc
```

`smith deploy --env staging` leaves out the other three flags. `smith deploy --env staging --verbose --region us` gives `verbose` and `region`.

## Its effects say what it may do

A task declares its effects like any function. A task that runs another program needs the `exec` effect.

```
pub fn format_all() ! exec
```

## A task that fails

A `Result` error prints its message and exits non-zero. When `deploy` above gives an error, `smith deploy` prints the error's message and exits non-zero.

## The names of commands

Built-in command names are reserved, so a task named `build` is a compile error.

```
pub fn build() ! fs
```

## A project that has grown

A project that has grown looks like this:

```
serving/
  project.smith
  main.smith
  main_test.smith
  model/
    gpt.smith
    gpt_test.smith
  routes/
    health.smith
  generated/
    routes.smith
  vendor/
    toml/
      parse.smith
```

`project.smith` holds the package, its vendored folders, its build steps, and its tasks. A task that installs handlers and calls `main` is a [deployment](deployments.md). `model/gpt_test.smith` holds the tests of `model/gpt.smith`, as [Tests](tests.md) describes.

## The forms

| Written | Means |
| --- | --- |
| `pub fn name(...)` in `project.smith` | The command `smith name` |
| `--env staging` | The parameter `env`, given `"staging"` |
| `--batch_size 64` | The parameter `batch_size`, spelled as it is in the source |
| `verbose: bool` | A bare flag, `--verbose` |
| `region: str = "eu"` or `note: Option[str]` | A flag that may be left out |
| `! exec` | The task may run another program |
| A task named like a built-in command | A compile error |

Next: [Tests](tests.md)
