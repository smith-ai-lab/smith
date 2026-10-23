---
name: smith-inflate
description: "Using smith::inflate (import smith::inflate; <smith/inflate.h> from C), smith's DEFLATE: raw, zlib and gzip with checksums, in memory or streamed to a sink. Load when code imports smith::inflate or includes <smith/inflate.h>, when a build.c calls smith_library(b, \"inflate\"), or on a question about decompressing gzip, zlib, PNG or HDF5 data, or a .tar.gz, in C or .smith without zlib."
---

# DEFLATE: smith's inflate

This skill ships in the smith plugin, from `.agents/skills/smith-inflate/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

DEFLATE (RFC 1951) with the zlib and gzip wrappers, input whole in memory, no allocation. Damaged input fails with a status and never reads or writes outside its buffers. smith's `.tar.gz` extraction is built on it. A `.smith` module; C includes `<smith/inflate.h>`; `smith_library(b, "inflate")` compiles it and needs nothing else.

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

## To a sink, for any size

```smith
import smith::inflate;

typedef struct {
    u64 total;
} Count;

static bool count(void *context, const u8 *data, usize size) {
    Count *count = context;
    count->total += size;
    (void)data;
    return true; // false stops the decoder: inflate::Stopped
}

bool gunzip_size(const u8 *data, usize data_size, u64 *size) {
    static u8 window[inflate::window];
    Count counted = {};
    usize used = 0;
    inflate::Status status = inflate::stream(inflate::Gzip, data, data_size, window, count, &counted, &used, size);
    return status == inflate::Ok;
}
```

The sink gets the bytes in order, in pieces of up to `inflate::window` (32 KiB), the size of the buffer you give. A wrong trailer is reported after every byte was delivered.

## Statuses and the rest

`inflate::status_message(status)` is text for a message. `Truncated`, `Corrupt`, `Overflow` (decode only), `BadHeader`, `BadChecksum` (Adler-32, CRC-32), `BadLength` (gzip's size), `Stopped` (the sink said no and has said why).

- **One stream per call.** A gzip file of several members is a loop over `used` (`runtime/gzip.c`).
- **Checksums:** `inflate::crc32(crc, data, size)` (start 0) and `inflate::adler32(adler, data, size)` (start 1) continue from what they return.

## From C

```c
#include <smith/inflate.h>
SmithInflateStatus s = smith_inflate_decode(SmithInflateFormat_Gzip, data, n, out, cap, &used, &size);
```

C names: `smith_inflate_<name>`, `SmithInflate<Type>`, `SmithInflateStatus_Ok`; `smith_inflate_stream` and `smith_inflate_window` likewise.
