---
name: smith-inflate-sink
description: "Streaming inflate to a sink for any size, and std::inflate from C (#include <smith/inflate.h>). Load when decompressing data bigger than memory or a .tar.gz, or from C."
---

# Inflate to a sink, and from C

## To a sink, for any size

```smith
import std::inflate;

typedef struct {
    U64 total;
} Count;

static Bool count(void *context, const U8 *data, Usize size) {
    Count *count = context;
    count->total += size;
    (void)data;
    return true; // false stops the decoder: inflate::Stopped
}

Bool gunzip_size(const U8 *data, Usize data_size, U64 *size) {
    static U8 window[inflate::window];
    Count counted = {};
    Usize used = 0;
    inflate::Status status = inflate::stream(inflate::Gzip, data, data_size, window, count, &counted, &used, size);
    return status == inflate::Ok;
}
```

The sink gets the bytes in order, in pieces of up to `inflate::window` (32 KiB), the size of the buffer you give. A wrong trailer is reported after every byte was delivered.

## From C

```c
#include <smith/inflate.h>
SmithInflateStatus s = smith_inflate_decode(SmithInflateFormat_Gzip, data, n, out, cap, &used, &size);
```

C names: `smith_inflate_<name>`, `SmithInflate<Type>`, `SmithInflateStatus_Ok`; `smith_inflate_stream` and `smith_inflate_window` likewise.

Also in this family: [smith-inflate](../smith-inflate/SKILL.md).
