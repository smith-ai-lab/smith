---
type: specification
title: HTTP/2 frames and headers
description: std.http2, which reads and writes the frames of HTTP/2 and the compressed headers of HPACK as bytes, with the limits the standard asks a reader to keep.
status: partial
built:
  - "`frame` and `write_frame`, which read and write every frame of RFC 9113, with the refusals below, and `decoder`, `decode`, `encoder`, and `encode`, which read and write the headers of RFC 7541 with its static table, its dynamic table, and its short code, written in Smith in `std/http2.smith`"
  - "`Read`, which is `std.http1`'s and is in `std.http2` until `std.http1` is written"
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
- A frame of a kind the standard does not name is `unknown` with its kind, and is passed over, as the standard says. Flags that mean nothing in a kind, and the reserved bit of a number, are ignored.
- `priority` holds its stream, and its dependency and weight, which RFC 9113 deprecates, are not kept: it is written with a dependency of 0 and a weight of 0. `unknown` is written as an empty frame of its kind on stream 0.
- A padded frame holds its padding as `padding: Option[Byte]`, the number of bytes of padding, which are written as zeros, or `.none` when the frame is not padded. A `ping` holds its 8 bytes as a `u64`.
- A frame is refused where RFC 9113 says the connection is in error: one longer than `largest`, a kind whose size is fixed that has another size, a frame on stream 0 that must be on a stream or the other way round, and padding longer than the frame.
- For every frame `f` this module writes, reading what it wrote gives `f`. That holds where each number is of the size its place in the frame has: a kind of 10 to 255, a setting's identifier of 16 bits and its value of 32, a payload of under 16777216 bytes.

The cases hold these, each with its fields public:

```
struct Data { stream, end_stream: bool, padding: Option[Byte], data: Slice[Byte] }
struct Headers { stream, end_stream: bool, end_headers: bool, padding: Option[Byte], priority: Option[Dependency], block: Slice[Byte] }
struct Dependency { exclusive: bool, stream, weight: Byte }
struct Reset { stream, code: u32 }
struct Promise { stream, end_headers: bool, padding: Option[Byte], promised: 0..2147483648, block: Slice[Byte] }
struct Ping { ack: bool, data: u64 }
struct GoAway { last: 0..2147483648, code: u32, debug: Slice[Byte] }
struct Window { stream, increment: 0..2147483648 }
struct Continuation { stream, end_headers: bool, block: Slice[Byte] }
```

`stream` is a `0..2147483648`. A `block` is a field block fragment, which `decode` reads once a `headers` or a `push_promise` and the `continuation`s that follow it are joined.

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
- `largest` is the most bytes the headers of one block may come to once read, the bytes of the names and the values. A block that would come to more is `.err` before the memory for it is asked for, so a few bytes cannot be made into a great many.
- A decoder made with `table` takes a change of the table's size up to `table`, and starts with a table of 4096 bytes or `table` if that is less, as RFC 9113 says. An encoder made with a `table` other than 4096 writes the change of size that tells the decoder at the start of its first block.
- After `.err` the decoder is as the block left it, and the connection is to be closed, as the standard says.
- A name or value that is not UTF-8 is `.err`, as a `str` holds it.
- `encode` writes a header whose name is `authorization`, `cookie`, or `set-cookie` never indexed, and never into the table, so its value is not learned from the size of what follows. It writes a text in the short code when that is no longer than the text as it is.
- For every list of headers, decoding what was encoded gives the list, with a decoder and an encoder that began together.

## The forms

| Written | Gives |
| --- | --- |
| `frame(bytes)` | One frame and how many bytes it took, or that more are needed, or why it is refused |
| `write_frame(to, frame)` | What was given, with the frame added |
| `decode(d, block)` | The headers of a block, and the decoder with its table as the block left it |
| `encode(e, to, headers)` | The block added to what was given, and the encoder with its table as it now is |
