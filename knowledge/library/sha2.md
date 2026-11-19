---
type: specification
title: SHA-256 and HMAC
description: std.sha2, with sha256, which gives the 32 bytes that stand for any bytes, and hmac_sha256, which signs bytes with a key that is a secret.
status: planned
---

# SHA-256 and HMAC

The module `std.sha2` holds `sha256`, the function of FIPS 180-4 that gives 32 bytes for any bytes, and `hmac_sha256`, the function of RFC 2104 that signs bytes with a key. Both are written in Smith, ask for no memory, and carry no effect.

```
fn sha256(bytes: Slice[Byte]) -> [32]Byte
fn hmac_sha256(key: Secret[Slice[Byte]], bytes: Slice[Byte]) -> Secret[[32]Byte]
```

```
use std.sha2 (sha256)

fn demo() -> [32]Byte { sha256("abc".bytes) }
```

`demo()` is the 32 bytes that begin `ba 78 16 bf` and end `f2 00 15 ad`, as the standard gives for `abc`.

`sha256` is for bytes anyone may see, and takes a time that depends on how many bytes there are and on nothing else.

`hmac_sha256` takes its key as a [`Secret`](secret.md) and gives a `Secret`, so neither the key nor the signature can steer a branch. A signature is compared with one that came from outside by looking at every byte, as `same` on that page does, and not with `==`, which a `Secret` has not. The bytes that are signed are not secret.

## The forms

| Written | Gives |
| --- | --- |
| `sha256(bytes)` | The 32 bytes of SHA-256 for the bytes |
| `hmac_sha256(key, bytes)` | The 32 bytes of HMAC with SHA-256, as a `Secret` |
