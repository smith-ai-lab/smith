---
type: convention
title: The SDK is chosen through the environment
description: On macOS smith sets CLANG_NO_DEFAULT_CONFIG and SDKROOT for the caller that has not, and never adds a flag.
tags: [sdk, macos]
---
# The SDK is chosen through the environment

**The SDK is chosen through the environment, never a flag.** Homebrew's clang reads a default config
(`/opt/homebrew/etc/clang/*.cfg`) that forces `-isysroot` to the Command Line Tools SDK, and that wins over
`SDKROOT`, though `xcrun` and `metal` use Xcode's. So on macOS smith sets, for each the caller has not set,
`CLANG_NO_DEFAULT_CONFIG=1` and `SDKROOT=$(xcrun --show-sdk-path)`. The caller's own `SDKROOT`, `-isysroot` and
`--config` win.

**The answer is remembered** (`runtime/sdk_cache.c`, #639). `xcrun --show-sdk-path` is a process of 5-11 ms and 35 million
instructions, as much as the whole start of smith, so the real `/usr/bin/xcrun`'s answer is kept in `<data>/smith/sdk`
(the key, the path, and the device, inode and modification time of the SDK folder). It is used while `DEVELOPER_DIR`
and the target of `xcode-select`'s link are the same and the folder is the same one, so a new Xcode, a switch or a new
SDK behind the same name asks again; a stub `xcrun` on PATH is always asked.
