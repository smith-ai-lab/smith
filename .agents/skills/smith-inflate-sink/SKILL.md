---
name: smith-inflate-sink
description: "Streaming inflate to a sink for any size, and smith::inflate from C (#include <smith/inflate.h>). Load when decompressing data bigger than memory or a .tar.gz, or from C."
---

# Inflate to a sink, and from C

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

## From C

```c
#include <smith/inflate.h>
SmithInflateStatus s = smith_inflate_decode(SmithInflateFormat_Gzip, data, n, out, cap, &used, &size);
```

C names: `smith_inflate_<name>`, `SmithInflate<Type>`, `SmithInflateStatus_Ok`; `smith_inflate_stream` and `smith_inflate_window` likewise.

Also in this family: [smith-inflate](../smith-inflate/SKILL.md).
