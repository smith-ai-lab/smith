// gzip (RFC 1952) and DEFLATE (RFC 1951) in C: smith's own decoder, so `smith_package` extracts a .tar.gz with no
// other process. Included by runtime/package.c, after its SmithPackageError and smith_package_fail; see
// runtime/untar.c for what uses it.
//
// The decoder reads an archive that is already in memory (a mapped file) and pushes what it produces to a sink in
// pieces of up to 32 KiB, the window. It allocates nothing: the window and the tables live in one static struct.
// Every loop is bounded by the input: each symbol takes at least one bit, and bits past the end of the input are zeros
// that count as an overrun. gzip's CRC-32 and size are checked for every member.
//
// Huffman codes are decoded by a table of the first 10 bits, then by the canonical walk of RFC 1951 for longer codes.
#include <stdint.h>
#include <string.h>

// What a decoder pushes its output to: false stops the decoder, and the sink has already set the error.
typedef bool (*SmithArchiveSink)(void *context, const uint8_t *data, size_t size);

constexpr size_t smith_gzip_window = 32768;
constexpr unsigned smith_gzip_fast_bits = 10;
constexpr size_t smith_gzip_members_max = 1000000;

// CRC-32 (gzip and xz, polynomial 0xEDB88320) and CRC-64 (xz, ECMA-182, reflected) by slicing-by-8. The tables are
// made on first use.
static uint32_t smith_crc32_tables[8][256];
static uint64_t smith_crc64_tables[8][256];
static bool smith_crc_ready;

static void smith_crc_init(void) {
    if (smith_crc_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t narrow = i;
        uint64_t wide = i;
        for (int bit = 0; bit < 8; bit++) {
            narrow = (narrow >> 1) ^ (0xEDB88320U & (0U - (narrow & 1U)));
            wide = (wide >> 1) ^ (UINT64_C(0xC96C5795D7870F42) & (UINT64_C(0) - (wide & 1U)));
        }
        smith_crc32_tables[0][i] = narrow;
        smith_crc64_tables[0][i] = wide;
    }
    for (size_t table = 1; table < 8; table++) {
        for (size_t i = 0; i < 256; i++) {
            uint32_t narrow = smith_crc32_tables[table - 1][i];
            uint64_t wide = smith_crc64_tables[table - 1][i];
            smith_crc32_tables[table][i] = (narrow >> 8) ^ smith_crc32_tables[0][narrow & 255U];
            smith_crc64_tables[table][i] = (wide >> 8) ^ smith_crc64_tables[0][wide & 255U];
        }
    }
    smith_crc_ready = true;
}

static uint32_t smith_load32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t smith_load64(const uint8_t *p) {
    return (uint64_t)smith_load32(p) | ((uint64_t)smith_load32(p + 4) << 32);
}

// The CRC-32 of data, continuing from `crc` (0 to start).
static uint32_t smith_crc32(uint32_t crc, const uint8_t *data, size_t size) {
    const uint32_t (*t)[256] = smith_crc32_tables;
    crc = ~crc;
    for (; size >= 8; data += 8, size -= 8) {
        uint32_t low = crc ^ smith_load32(data);
        uint32_t high = smith_load32(data + 4);
        crc = t[7][low & 255U] ^ t[6][(low >> 8) & 255U] ^ t[5][(low >> 16) & 255U] ^ t[4][low >> 24] ^
              t[3][high & 255U] ^ t[2][(high >> 8) & 255U] ^ t[1][(high >> 16) & 255U] ^ t[0][high >> 24];
    }
    for (; size > 0; data++, size--)
        crc = t[0][(crc ^ *data) & 255U] ^ (crc >> 8);
    return ~crc;
}

// The CRC-64 of data, continuing from `crc` (0 to start).
static uint64_t smith_crc64(uint64_t crc, const uint8_t *data, size_t size) {
    const uint64_t (*t)[256] = smith_crc64_tables;
    crc = ~crc;
    for (; size >= 8; data += 8, size -= 8) {
        uint64_t word = crc ^ smith_load64(data);
        crc = t[7][word & 255U] ^ t[6][(word >> 8) & 255U] ^ t[5][(word >> 16) & 255U] ^ t[4][(word >> 24) & 255U] ^
              t[3][(word >> 32) & 255U] ^ t[2][(word >> 40) & 255U] ^ t[1][(word >> 48) & 255U] ^ t[0][word >> 56];
    }
    for (; size > 0; data++, size--)
        crc = t[0][(crc ^ *data) & 255U] ^ (crc >> 8);
    return ~crc;
}

// Bits, least significant first. Past the end of the input the reader supplies zeros and counts them in `fake`, so a
// decoder checks `count < fake` after a symbol to learn that it read bits that are not there.
typedef struct {
    const uint8_t *in;
    size_t size;
    size_t at; // the next byte to load; may pass `size` by the fake bytes
    uint64_t bits;
    unsigned count;
    unsigned fake;
} SmithBitReader;

[[gnu::always_inline]] static void smith_bits_refill(SmithBitReader *reader) {
    if (reader->at + 8 <= reader->size) {
        reader->bits |= smith_load64(reader->in + reader->at) << reader->count;
        unsigned take = (63U - reader->count) >> 3;
        reader->at += take;
        reader->count += take * 8U;
        return;
    }
    for (; reader->count < 56; reader->count += 8, reader->at++) {
        uint64_t byte = 0;
        if (reader->at < reader->size)
            byte = reader->in[reader->at];
        else
            reader->fake += 8;
        reader->bits |= byte << reader->count;
    }
}

[[gnu::always_inline]] static uint32_t smith_bits_peek(const SmithBitReader *reader, unsigned n) {
    return (uint32_t)(reader->bits & ((UINT64_C(1) << n) - 1U));
}

[[gnu::always_inline]] static void smith_bits_drop(SmithBitReader *reader, unsigned n) {
    reader->bits >>= n;
    reader->count -= n;
}

[[gnu::always_inline]] static uint32_t smith_bits_take(SmithBitReader *reader, unsigned n) {
    uint32_t value = smith_bits_peek(reader, n);
    smith_bits_drop(reader, n);
    return value;
}

[[gnu::always_inline]] static bool smith_bits_overrun(const SmithBitReader *reader) {
    return reader->count < reader->fake;
}

typedef struct {
    uint16_t fast[1U << smith_gzip_fast_bits]; // symbol << 4 | length for codes up to the table's bits, else 0
    uint16_t count[16];                        // codes of each length
    uint16_t symbol[288];                      // symbols in canonical order
} SmithHuffman;

// Builds the decoder of a canonical code from its lengths. False when the code is over-subscribed, or incomplete
// (zlib's rule: except the one code of length 1 that a lone distance may be).
static bool smith_huffman_build(SmithHuffman *huffman, const uint8_t *lengths, size_t n) {
    SMITH_ASSERT(n <= 288);
    memset(huffman->count, 0, sizeof huffman->count);
    memset(huffman->fast, 0, sizeof huffman->fast);
    for (size_t i = 0; i < n; i++)
        huffman->count[lengths[i]]++;
    huffman->count[0] = 0;
    int left = 1;
    uint16_t offsets[16] = {0};
    size_t used = 0;
    for (size_t length = 1; length < 16; length++) {
        left = left * 2 - huffman->count[length];
        if (left < 0) return false;
        offsets[length] = (uint16_t)used;
        used += huffman->count[length];
    }
    if (left > 0 && !(used == 1 && huffman->count[1] == 1)) return used == 0;
    uint16_t next[16];
    memcpy(next, offsets, sizeof next);
    for (size_t i = 0; i < n; i++) {
        if (lengths[i] != 0) huffman->symbol[next[lengths[i]]++] = (uint16_t)i;
    }
    uint32_t code = 0;
    size_t index = 0;
    for (unsigned length = 1; length <= smith_gzip_fast_bits; length++) {
        for (uint16_t k = 0; k < huffman->count[length]; k++, code++, index++) {
            uint32_t reversed = 0;
            for (unsigned bit = 0; bit < length; bit++)
                reversed |= ((code >> bit) & 1U) << (length - 1U - bit);
            for (uint32_t fill = reversed; fill < (1U << smith_gzip_fast_bits); fill += 1U << length)
                huffman->fast[fill] = (uint16_t)(((uint32_t)huffman->symbol[index] << 4) | length);
        }
        code <<= 1;
    }
    return true;
}

// The next symbol, or -1 for bits that are no code. The reader must hold at least 15 bits.
[[gnu::always_inline]] static int smith_huffman_decode(const SmithHuffman *huffman, SmithBitReader *reader) {
    uint16_t entry = huffman->fast[smith_bits_peek(reader, smith_gzip_fast_bits)];
    if (entry != 0) {
        smith_bits_drop(reader, entry & 15U);
        return entry >> 4;
    }
    unsigned code = 0;  // the bits read so far, first bit highest
    unsigned first = 0; // the first code of this length
    unsigned index = 0; // where the codes of this length start in `symbol`
    for (unsigned length = 1; length < 16; length++) {
        code = (code << 1) + (unsigned)((reader->bits >> (length - 1U)) & 1U);
        unsigned count = huffman->count[length];
        if (code - first < count) {
            smith_bits_drop(reader, length);
            return huffman->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
    }
    return -1;
}

typedef struct {
    SmithBitReader reader;
    SmithArchiveSink sink;
    void *context;
    SmithPackageError *error;
    uint64_t total; // bytes of this member produced, flushed or not
    uint32_t crc;   // of the bytes flushed
    size_t at;      // bytes in the window
    uint64_t guard; // loop bound: bits of input plus slack
    SmithHuffman literals;
    SmithHuffman distances;
    uint8_t window[smith_gzip_window];
} SmithInflate;

static bool smith_inflate_fail(SmithInflate *inflate, const char *reason) {
    return smith_package_fail(inflate->error, "gzip: %s", reason);
}

static bool smith_inflate_flush(SmithInflate *inflate) {
    inflate->crc = smith_crc32(inflate->crc, inflate->window, inflate->at);
    bool ok = inflate->at == 0 || inflate->sink(inflate->context, inflate->window, inflate->at);
    inflate->at = 0;
    return ok;
}

[[gnu::always_inline]] static bool smith_inflate_put(SmithInflate *inflate, uint8_t byte) {
    inflate->window[inflate->at++] = byte;
    inflate->total++;
    return inflate->at < smith_gzip_window || smith_inflate_flush(inflate);
}

// A stored block: whole bytes after the header, copied from the input.
static bool smith_inflate_stored(SmithInflate *inflate) {
    SmithBitReader *reader = &inflate->reader;
    smith_bits_drop(reader, reader->count & 7U);
    uint32_t length = smith_bits_take(reader, 16);
    uint32_t inverse = smith_bits_take(reader, 16);
    if (smith_bits_overrun(reader)) return smith_inflate_fail(inflate, "the data ends inside a block");
    if ((length ^ inverse) != 0xFFFFU) return smith_inflate_fail(inflate, "a stored block's length is damaged");
    reader->at -= reader->count / 8U; // hand the whole bytes still in the buffer back
    reader->bits = 0;
    reader->count = 0;
    reader->fake = 0;
    if (reader->size - reader->at < length) return smith_inflate_fail(inflate, "the data ends inside a block");
    for (uint32_t i = 0; i < length; i++) {
        if (!smith_inflate_put(inflate, reader->in[reader->at + i])) return false;
    }
    reader->at += length;
    return true;
}

static const uint16_t smith_inflate_length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t smith_inflate_length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t smith_inflate_distance_base[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                         33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t smith_inflate_distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Copies `length` bytes from `distance` back, which may overlap what it writes.
static bool smith_inflate_copy(SmithInflate *inflate, uint32_t length, uint32_t distance) {
    if (distance > inflate->total) return smith_inflate_fail(inflate, "a match reaches before the start of the data");
    if (inflate->at >= distance && inflate->at + length < smith_gzip_window) { // the common case: no wrap, no flush
        uint8_t *to = inflate->window + inflate->at;
        const uint8_t *from = to - distance;
        if (distance >= length) {
            memcpy(to, from, length);
        } else {
            for (uint32_t i = 0; i < length; i++)
                to[i] = from[i];
        }
        inflate->at += length;
        inflate->total += length;
        return true;
    }
    for (uint32_t i = 0; i < length; i++) {
        size_t from = (inflate->at - distance) & (smith_gzip_window - 1U);
        if (!smith_inflate_put(inflate, inflate->window[from])) return false;
    }
    return true;
}

// The symbols of one Huffman block, up to its end-of-block.
static bool smith_inflate_symbols(SmithInflate *inflate) {
    SmithBitReader *reader = &inflate->reader;
    for (uint64_t step = 0; step < inflate->guard; step++) {
        smith_bits_refill(reader); // 15 + 5 + 15 + 13 bits is the most one symbol and its match take: under 56
        int symbol = smith_huffman_decode(&inflate->literals, reader);
        if (symbol < 0) return smith_inflate_fail(inflate, "a code in the data is not valid");
        if (symbol < 256) {
            if (!smith_inflate_put(inflate, (uint8_t)symbol)) return false;
        } else if (symbol == 256) {
            return !smith_bits_overrun(reader) || smith_inflate_fail(inflate, "the data ends inside a block");
        } else {
            symbol -= 257;
            if (symbol >= 29) return smith_inflate_fail(inflate, "a length code in the data is not valid");
            uint32_t length =
                smith_inflate_length_base[symbol] + smith_bits_take(reader, smith_inflate_length_extra[symbol]);
            int slot = smith_huffman_decode(&inflate->distances, reader);
            if (slot < 0 || slot >= 30) return smith_inflate_fail(inflate, "a distance code in the data is not valid");
            uint32_t distance =
                smith_inflate_distance_base[slot] + smith_bits_take(reader, smith_inflate_distance_extra[slot]);
            if (smith_bits_overrun(reader)) return smith_inflate_fail(inflate, "the data ends inside a block");
            if (!smith_inflate_copy(inflate, length, distance)) return false;
        }
        if (smith_bits_overrun(reader)) return smith_inflate_fail(inflate, "the data ends inside a block");
    }
    return smith_inflate_fail(inflate, "a block does not end");
}

static bool smith_inflate_fixed(SmithInflate *inflate) {
    uint8_t lengths[288];
    memset(lengths, 8, 144);
    memset(lengths + 144, 9, 112);
    memset(lengths + 256, 7, 24);
    memset(lengths + 280, 8, 8);
    bool ok = smith_huffman_build(&inflate->literals, lengths, 288);
    memset(lengths, 5, 32); // 30 and 31 are codes too, and a match that uses them is refused
    ok = ok && smith_huffman_build(&inflate->distances, lengths, 32);
    SMITH_ASSERT(ok);
    return smith_inflate_symbols(inflate);
}

// Reads `count` code lengths with the code-length code, whose repeats 16, 17 and 18 may cross from literals to
// distances.
static bool smith_inflate_lengths(SmithInflate *inflate, const SmithHuffman *code, uint8_t *lengths, size_t count) {
    SmithBitReader *reader = &inflate->reader;
    for (size_t at = 0, step = 0; at < count; step++) {
        if (step > count) return smith_inflate_fail(inflate, "the code lengths do not end");
        smith_bits_refill(reader);
        int symbol = smith_huffman_decode(code, reader);
        if (symbol < 0) return smith_inflate_fail(inflate, "a code length in the data is not valid");
        uint32_t repeat = 1;
        uint8_t value = (uint8_t)symbol;
        if (symbol == 16) {
            if (at == 0) return smith_inflate_fail(inflate, "a repeat in the code lengths has nothing to repeat");
            value = lengths[at - 1];
            repeat = 3 + smith_bits_take(reader, 2);
        } else if (symbol == 17) {
            value = 0;
            repeat = 3 + smith_bits_take(reader, 3);
        } else if (symbol == 18) {
            value = 0;
            repeat = 11 + smith_bits_take(reader, 7);
        }
        if (smith_bits_overrun(reader)) return smith_inflate_fail(inflate, "the data ends inside a block header");
        if (repeat > count - at) return smith_inflate_fail(inflate, "the code lengths overrun their table");
        memset(lengths + at, value, repeat);
        at += repeat;
    }
    return true;
}

static bool smith_inflate_dynamic(SmithInflate *inflate) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    SmithBitReader *reader = &inflate->reader;
    smith_bits_refill(reader);
    size_t literals = 257U + smith_bits_take(reader, 5);
    size_t distances = 1U + smith_bits_take(reader, 5);
    size_t codes = 4U + smith_bits_take(reader, 4);
    if (literals > 286 || distances > 30) return smith_inflate_fail(inflate, "a block declares too many codes");
    uint8_t lengths[320] = {0};
    for (size_t i = 0; i < codes; i++) {
        smith_bits_refill(reader);
        lengths[order[i]] = (uint8_t)smith_bits_take(reader, 3);
    }
    SmithHuffman code;
    if (smith_bits_overrun(reader) || !smith_huffman_build(&code, lengths, 19)) {
        return smith_inflate_fail(inflate, "a block's code-length code is not valid");
    }
    if (!smith_inflate_lengths(inflate, &code, lengths, literals + distances)) return false;
    if (lengths[256] == 0) return smith_inflate_fail(inflate, "a block has no end-of-block code");
    if (!smith_huffman_build(&inflate->literals, lengths, literals) ||
        !smith_huffman_build(&inflate->distances, lengths + literals, distances)) {
        return smith_inflate_fail(inflate, "a block's codes are not valid");
    }
    return smith_inflate_symbols(inflate);
}

// The blocks of one DEFLATE stream, to the one marked final.
static bool smith_inflate_blocks(SmithInflate *inflate) {
    SmithBitReader *reader = &inflate->reader;
    for (uint64_t block = 0; block < inflate->guard; block++) {
        smith_bits_refill(reader);
        bool last = smith_bits_take(reader, 1) != 0;
        uint32_t type = smith_bits_take(reader, 2);
        if (smith_bits_overrun(reader)) return smith_inflate_fail(inflate, "the data ends before a block");
        bool ok = false;
        if (type == 0)
            ok = smith_inflate_stored(inflate);
        else if (type == 1)
            ok = smith_inflate_fixed(inflate);
        else if (type == 2)
            ok = smith_inflate_dynamic(inflate);
        else
            return smith_inflate_fail(inflate, "a block has the reserved type 3");
        if (!ok) return false;
        if (last) return true;
    }
    return smith_inflate_fail(inflate, "the blocks do not end");
}

// The bytes of a member's header, after the ten fixed ones: extra field, name, comment and header CRC. Returns where
// the DEFLATE data starts, or 0 when the header is cut off or damaged.
static size_t smith_gzip_header(const uint8_t *in, size_t size, size_t *error_at) {
    constexpr uint8_t extra = 4, name = 8, comment = 16, crc16 = 2;
    size_t at = 10;
    uint8_t flags = in[3];
    if (flags & extra) {
        if (size - at < 2) return 0;
        size_t length = (size_t)in[at] | ((size_t)in[at + 1] << 8);
        if (size - at - 2 < length) return 0;
        at += 2 + length;
    }
    for (uint8_t flag = name; flag <= comment; flag = (uint8_t)(flag * 2)) {
        if (!(flags & flag)) continue;
        const uint8_t *end = at < size ? memchr(in + at, 0, size - at) : nullptr;
        if (end == nullptr) return 0;
        at = (size_t)(end - in) + 1;
    }
    if (flags & crc16) {
        if (size - at < 2) return 0;
        uint32_t wanted = (uint32_t)in[at] | ((uint32_t)in[at + 1] << 8);
        if ((smith_crc32(0, in, at) & 0xFFFFU) != wanted) {
            *error_at = at;
            return 0;
        }
        at += 2;
    }
    return at;
}

// One member: header, DEFLATE data, trailer. Returns the bytes it takes in `used`.
static bool smith_gzip_member(SmithInflate *inflate, const uint8_t *in, size_t size, size_t *used) {
    if (size < 18 || in[0] != 0x1F || in[1] != 0x8B) return smith_inflate_fail(inflate, "not a gzip member");
    if (in[2] != 8 || (in[3] & 0xE0U) != 0)
        return smith_inflate_fail(inflate, "a gzip header uses an unknown method or flag");
    size_t error_at = 0;
    size_t start = smith_gzip_header(in, size, &error_at);
    if (start == 0)
        return smith_inflate_fail(inflate, error_at != 0 ? "the header's CRC does not match" : "the header is cut off");
    inflate->reader = (SmithBitReader){.in = in + start, .size = size - start};
    inflate->total = 0;
    inflate->crc = 0;
    inflate->at = 0;
    inflate->guard = (uint64_t)(size - start) * 8U + 64U;
    if (!smith_inflate_blocks(inflate) || !smith_inflate_flush(inflate)) return false;
    SmithBitReader *reader = &inflate->reader;
    size_t end = reader->at - reader->count / 8U; // whole bytes not yet used go back
    if (end > reader->size || reader->size - end < 8) return smith_inflate_fail(inflate, "the trailer is cut off");
    const uint8_t *trailer = reader->in + end;
    if (smith_load32(trailer) != inflate->crc)
        return smith_inflate_fail(inflate, "the CRC-32 of the data does not match");
    if (smith_load32(trailer + 4) != (uint32_t)inflate->total)
        return smith_inflate_fail(inflate, "the size of the data does not match");
    *used = start + end + 8;
    return true;
}

// Decodes a gzip file, which is one or more members one after the other (as `cat a.gz b.gz` makes), to the sink.
// Zero bytes after the last member are padding and ignored; anything else is an error.
static bool smith_gzip_decode(const uint8_t *in, size_t size, SmithArchiveSink sink, void *context,
                              SmithPackageError *error) {
    smith_crc_init();
    static SmithInflate inflate;
    inflate.sink = sink;
    inflate.context = context;
    inflate.error = error;
    size_t at = 0;
    for (size_t member = 0; member < smith_gzip_members_max; member++) {
        size_t used = 0;
        if (!smith_gzip_member(&inflate, in + at, size - at, &used)) return false;
        at += used;
        while (at < size && in[at] == 0)
            at++;
        if (at == size) return true;
    }
    return smith_package_fail(error, "gzip: too many members");
}
