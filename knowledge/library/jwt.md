---
type: specification
title: JSON Web Tokens
description: std.jwt, with verify, which gives the claims of a token only when its signature, its time, its issuer, and its audience are all right.
status: planned
---

# JSON Web Tokens

The module `std.jwt` reads a JSON Web Token, the signed text of RFC 7519 that says who a caller is. It has one way in, `verify`, which gives the claims of a token only when everything about it is right. No function gives the claims of a token that was not checked.

```
use std.jwt (Claims, JwtError, Key, verify)

fn who(token: str, key: Key) -> Result[Claims, JwtError] ! time, alloc {
  verify(token, key, issuer: "https://login.example.com", audience: "https://notes.example.com")
}
```

`who` gives `.ok` of the claims when the token was signed with the key, names that issuer and that audience, and is inside its time. It gives `.err` otherwise, and the error says which of these failed.

```
fn verify(token: str, key: Key, issuer: str, audience: str) -> Result[Claims, JwtError] ! time, alloc

struct Claims {
  subject: str
  scopes: List[str]
  expires: u64
}

enum Key {
  hmac: Secret[Slice[Byte]]
}

enum JwtError {
  malformed
  algorithm
  signature
  expired
  early
  issuer
  audience
}
```

- `verify` checks in this order, and stops at the first that fails: the text is three parts of [base64 for an address](base64.md) with a header and claims that are JSON; the algorithm the header names is the one the key is for; the signature is that of the first two parts under the key; the time now, from [`time`](../language/effect-declarations.md), is before `exp` and not before `nbf`; `iss` is the issuer; `aud` is or holds the audience.
- A token whose header names the algorithm `none`, or any algorithm the key is not for, is `.err(.algorithm)`. The key says the algorithm and the token never does.
- `Claims` holds what a server acts on: `sub`, the scopes of `scope` cut at each space, and `exp`. Its fields are read and it is made only by `verify`.
- `Key` has one case today, `hmac`, for the algorithm HS256, which signs with [`hmac_sha256`](sha2.md). The algorithms that sign with a key of two halves, RS256 and ES256, are cases it has not yet.

## The forms

| Written | Gives |
| --- | --- |
| `verify(token, key, issuer: i, audience: a)` | `.ok` of the claims of a token that is right in every way, or which way it is not |
