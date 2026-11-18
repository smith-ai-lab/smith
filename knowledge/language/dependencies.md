---
type: specification
title: Dependencies
description: Outside code is vendored into the project as ordinary modules, listed with its source and hash in project.smith, and checked by the build without the network.
status: planned
---

# Dependencies

Outside code is vendored: it is copied into the project as ordinary modules and imported like any other, as in `use vendor.toml.parse (parse)`. There is no registry and there are no version ranges.

`project.smith` records where each vendored folder came from and its content hash.

```
use std.project (Package, Vendored)

package = Package(
  name: "site",
  vendored: [
    Vendored(path: "vendor/toml", source: "https://example.com/toml", hash: "b3:9f2c"),
    Vendored(path: "vendor/sqlite", source: "https://example.com/sqlite", hash: "b3:41ae"),
  ],
)
```

`site` has two vendored folders. Each has its path in the project, the place it came from, and its hash.

## Adding one

`smith vendor add <url>` fetches the folder, hashes it, and adds its entry. It creates `project.smith` when there is none, as [Projects](projects.md) says.

```
smith vendor add https://example.com/toml
```

`smith vendor update <path>` fetches one folder again and updates its hash.

```
smith vendor update vendor/toml
```

`smith vendor add` shows a dependency's own vendored list, and writes entries only for what is approved.

## Every dependency is listed

Every dependency is listed by the project, including dependencies of dependencies. If `vendor/sqlite` imports from `vendor/toml`, both are in the list above.

## The build checks the hash

The build checks each folder of the list above against its hash and never touches the network.

## Effects stay in the signature

A vendored function's effects are in its signature like any other, and a caller must declare them to call it. A parser with no effects is pure, whatever it contains.

```
use vendor.toml.parse (parse)
use vendor.say (shout)

fn config(text: str) -> Config {
  parse(text)
}

fn announce() {
  shout("ready")
}
```

`config` is pure because `parse` has no effects. `announce` is refused if `shout` is `fn shout(text: str) ! console`, since `announce` does not declare `console`: "`shout` needs the `console` effect, which `announce` does not declare".

## Nothing runs on its own

Nothing runs because a dependency is present. Its code runs only when the project calls it.

```
use vendor.toml.parse (parse)

fn main() ! console {
  print("hello")
}
```

`vendor/toml` is in the project, and `main` never calls `parse`, so none of it runs.

## The forms

| Written | Means |
| --- | --- |
| `vendored: [Vendored(path:, source:, hash:)]` | A folder copied into the project, with where it came from and its hash |
| `use vendor.toml.parse (parse)` | A vendored module, imported like any other |
| `smith vendor add <url>` | Fetches, hashes, and adds an entry, for what is approved |
| `smith vendor update <path>` | Fetches one folder again and updates its hash |

Next: [Build steps](build-steps.md)
