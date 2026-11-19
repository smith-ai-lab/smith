---
type: specification
title: HTTP/2 frames and headers
description: std.http2, which reads and writes the frames of HTTP/2 and the compressed headers of HPACK as bytes, with the limits the standard asks a reader to keep.
status: planned
---

# HTTP/2 frames and headers

The module `std.http2` turns bytes into the frames of HTTP/2, as RFC 9113 writes them, and reads and writes the compressed headers of HPACK, as RFC 7541 does. Like [`std.http1`](http1.md) it is given bytes and gives values, carries no effect but `alloc`, and knows nothing of sockets or of which stream is open: a handler of [`http`](http.md) holds that.

## Frames

```
enum Frame {
  data: Data
  headers: Headers
  priority: Nat
  reset: Reset
  settings: List[(Nat, Nat)]
  settings_ack
  push_promise: Promise
  ping: Ping
  go_away: GoAway
  window_update: Window
  continuation: Continuation
  unknown: Nat
}

fn frame(bytes: Slice[Byte], largest: Nat = 16384) -> Read[Frame] ! alloc
fn write_frame(to: take List[Byte], frame: Frame) -> List[Byte] ! alloc
```

- `frame` reads one frame and gives it with how many bytes it took, in the [`Read`](http1.md#reading) of `std.http1`. A frame's bytes, as the data of `data`, are a piece of what was given.
- Each case holds what its frame holds: its stream, its flags as `bool`s by name, and its fields. A stream is a `0..2147483648`, as the standard keeps the highest bit back.
- A frame of a kind the standard does not name is `unknown` with its kind, and is passed over, as the standard says.
- A frame is refused where RFC 9113 says the connection is in error: one longer than `largest`, a kind whose size is fixed that has another size, a frame on stream 0 that must be on a stream or the other way round, and padding longer than the frame.
- For every frame `f` this module writes, reading what it wrote gives `f`.

## Headers

```
struct Decoder
struct Encoder

fn decoder(table: Nat = 4096) -> Decoder
fn decode(d: take Decoder, block: Slice[Byte], largest: Nat = 65536) -> (Decoder, Result[List[(str, str)], str]) ! alloc

fn encoder(table: Nat = 4096) -> Encoder
fn encode(e: take Encoder, to: take List[Byte], headers: List[(str, str)]) -> (Encoder, List[Byte]) ! alloc
```

- A `Decoder` and an `Encoder` hold the table of headers that HPACK builds up over a connection, so each is [taken](../language/take.md) and given back, and one is kept for each direction of each connection.
- `decode` reads one block of headers. It gives `.err` where RFC 7541 says the block is wrong: an index that names no header, a number that does not end, a text in the short code that is not right, or a change of the table's size that is not at the start or is above what was agreed.
- `largest` is the most bytes the headers of one block may come to once read. A block that would come to more is `.err`, so a few bytes cannot be made into a great many.
- A name or value that is not UTF-8 is `.err`, as a `str` holds it.
- `encode` never writes a header whose name is `authorization`, `cookie`, or `set-cookie` into the table, so its value is not learned from the size of what follows.
- For every list of headers, decoding what was encoded gives the list, with a decoder and an encoder that began together.

## The forms

| Written | Gives |
| --- | --- |
| `frame(bytes)` | One frame and how many bytes it took, or that more are needed, or why it is refused |
| `write_frame(to, frame)` | What was given, with the frame added |
| `decode(d, block)` | The headers of a block, and the decoder with its table as the block left it |
| `encode(e, to, headers)` | The block added to what was given, and the encoder with its table as it now is |
