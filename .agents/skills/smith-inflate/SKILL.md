---
name: smith-inflate
description: "Using smith::inflate (import smith::inflate): DEFLATE raw, zlib and gzip in memory with checksums. Load when code imports smith::inflate or includes <smith/inflate.h>, or on decompressing gzip, zlib, PNG or HDF5 data in .smith or C without zlib."
---

# DEFLATE: smith's inflate

DEFLATE (RFC 1951) with the zlib and gzip wrappers, input whole in memory, no allocation. Damaged input fails with a status (data, so a value); a call that is wrong panics: a null input with a size, a null output with a capacity, a null `used` or `size`, and for `stream` a null window or sink. It never reads or writes outside its buffers. smith's `.tar.gz` extraction is built on it. A `.smith` module; C includes `<smith/inflate.h>`; `smith_library(b, "inflate")` compiles it and needs nothing else.

## In memory

```smith
import smith::inflate;

// Inflates a zlib stream (an HDF5 chunk, a PNG's IDAT) into `out`. Returns the bytes written, or -1.
isize unzip(const u8 *data, usize data_size, u8 *out, usize capacity) {
    usize used = 0;
    usize size = 0;
    inflate::Status status = inflate::decode(inflate::Zlib, data, data_size, out, capacity, &used, &size);
    if (status != inflate::Ok) return -1;
    return (isize)size;
}
```

`Raw`, `Zlib` and `Gzip` say what wraps the stream. `used` is the input the stream took (trailer included), `size` the bytes written; on failure both say how far it got. More than `capacity` bytes is `Overflow`.

## Statuses and the rest

`inflate::status_message(status)` is text for a message. `Truncated`, `Corrupt`, `Overflow` (decode only), `BadHeader`, `BadChecksum` (Adler-32, CRC-32), `BadLength` (gzip's size), `Stopped` (the sink said no and has said why).

- **One stream per call.** A gzip file of several members is a loop over `used` (`runtime/gzip.c`).
- **Checksums:** `inflate::crc32(crc, data, size)` (start 0) and `inflate::adler32(adler, data, size)` (start 1) continue from what they return.

Also in this family: [smith-inflate-sink](../smith-inflate-sink/SKILL.md).
