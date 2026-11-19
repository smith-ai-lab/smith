---
type: specification
title: DER and certificates
description: std.der, which reads the tagged values of DER as pieces of the bytes it is given, and std.x509, which reads a certificate into its names, its time, and its key, and checks nothing.
status: partial
built:
  - "`std.der`: `Value` and `Tag`, `value`, `children`, `whole`, `text`, `object`, and `bits`, with every `Slice` of a `Value` a piece of the bytes given and each way DER is not written right that the page lists an `.err`"
  - "not `std.x509`, `Certificate`, `certificate`, or `pem`"
---

# DER and certificates

The module `std.der` reads DER, the way of writing tagged values that certificates and keys are written in, as ITU-T X.690 gives it. The module `std.x509` reads one certificate with it, as RFC 5280 lays one out. Neither checks a signature or decides whom to trust: they turn bytes into values, and what trusts a certificate is the TLS of [`fetch`](fetch.md).

## A value of DER

```
struct Value {
  tag: Tag
  bytes: Slice[Byte]
}

enum Tag {
  boolean
  integer
  bits
  octets
  null
  object
  utf8
  sequence
  set
  printable
  ia5
  utc_time
  general_time
  context: (0..31, bool)
  other: Nat
}

fn value(bytes: Slice[Byte]) -> Result[(Value, Slice[Byte]), str]
fn children(of: Value) -> Result[List[Value], str] ! alloc
fn whole(of: Value) -> Result[Slice[Byte], str]
fn text(of: Value) -> Result[str, str]
fn bits(of: Value) -> Result[(Slice[Byte], Nat), str]
fn object(of: Value) -> Result[List[Nat], str] ! alloc
```

- `value` reads one value from the front of the bytes and gives it with the bytes after it. The bytes of a `Value` are a piece of what was given, so nothing is copied.
- `children` gives the values inside a `sequence`, a `set`, or a `context` value that holds others, and is `.err` for the bytes of one that its parts do not fill.
- `whole` gives the bytes of an `integer` with no sign, as a number of any size, most significant first, with a zero at the front taken off when more bytes follow it, and is `.err` for a number below zero. `text` gives the text of a `utf8`, `printable`, or `ia5` value, and is `.err` for bytes that are not valid UTF-8, or for a `printable` or `ia5` value with a byte of 128 or above. `object` gives the numbers of an object identifier, the first byte standing for the first two. `bits` gives the bytes of a `bits` value and how many bits at the end of its last byte are left over, which is `.err` above 7, when there is no byte to leave them in, and when a bit left over is not zero, as DER writes it.
- A tag is `context` for the class of that name, with its number and whether it is built of parts, and `other` holds the byte that began the value for every class and kind that has no case of its own. A tag of more than one byte is `.err`.
- DER has one way to write each value, and what is not written that way is `.err`: a length that could be shorter, a length with no end, an `integer` with bytes at the front it does not need, a `boolean` that is neither 0 nor 255, and bytes left inside a value that its parts do not use.

## A certificate

```
struct Certificate {
  signed: Slice[Byte]
  algorithm: List[Nat]
  signature: Slice[Byte]
  serial: Slice[Byte]
  issuer: Slice[Byte]
  subject: Slice[Byte]
  not_before: u64
  not_after: u64
  key_algorithm: List[Nat]
  key: Slice[Byte]
  names: List[str]
  authority: Option[bool]
  path_length: Option[Nat]
}

fn certificate(bytes: Slice[Byte]) -> Result[Certificate, str] ! alloc
fn pem(text: str) -> Result[List[List[Byte]], str] ! alloc
```

- `certificate` reads one certificate of version 3. `signed` is the part the signature is over, as the bytes it was, and `issuer` and `subject` are the bytes of each name, as two names are the same when their bytes are.
- `not_before` and `not_after` are seconds since 1970, as the clock of [`time`](../language/effect-declarations.md) gives.
- `names` are the DNS names of the extension for other names, each as written, a `*` at the front among them. `authority` and `path_length` are the extension that says a certificate may sign others.
- An extension marked critical that this module does not read makes the certificate `.err`, as RFC 5280 asks, so a rule this module does not know is never passed over in silence.
- `pem` gives the bytes of each certificate in a text of the kind that begins `-----BEGIN CERTIFICATE-----`, read with [`std.base64`](base64.md).

## The forms

| Written | Gives |
| --- | --- |
| `value(bytes)` | The first value of the bytes, and the bytes after it |
| `children(v)`, `whole(v)`, `text(v)`, `object(v)`, `bits(v)` | What a value holds, read as its kind |
| `certificate(bytes)` | The parts of one certificate, with nothing checked |
| `pem(text)` | The bytes of each certificate in the text |
