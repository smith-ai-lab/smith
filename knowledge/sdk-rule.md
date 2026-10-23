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
