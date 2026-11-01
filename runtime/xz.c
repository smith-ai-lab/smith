// xz (the .xz container) and LZMA2 in C: smith's own decoder, so `smith_package` extracts a .tar.xz with no other
// process. Included by runtime/package.c after runtime/gzip.c, whose sink type it uses, and after its SHA-256; its
// CRC-32 is std::inflate's.
//
// What it reads: streams (one or more, with the zero padding xz allows between them) of blocks whose one filter is
// LZMA2, the filter every .tar.xz uses, with the check none, CRC-32, CRC-64 or SHA-256. Every field the format lets a
// decoder verify is verified: the CRC-32 of each header, the sizes a block header declares, the block's check, and the
// index against the blocks, then the footer against the stream header. A BCJ or delta filter, or another check, is an
// error that names it.
//
// Memory: the dictionary is one anonymous mapping of the size the block header asks for (at most 1 GiB), which the
// system commits as it is written. Everything else is static. Every loop is bounded by a size the stream states and the
// decoder has checked against the input.
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

// CRC-64 (xz, ECMA-182, reflected) by slicing-by-8. The tables are made on first use.
static uint64_t smith_crc64_tables[8][256];
static bool smith_crc64_ready;

static void smith_crc64_init(void) {
    if (smith_crc64_ready) return;
    for (uint64_t i = 0; i < 256; i++) {
        uint64_t wide = i;
        for (int bit = 0; bit < 8; bit++)
            wide = (wide >> 1) ^ (UINT64_C(0xC96C5795D7870F42) & (UINT64_C(0) - (wide & 1U)));
        smith_crc64_tables[0][i] = wide;
    }
    for (size_t table = 1; table < 8; table++) {
        for (size_t i = 0; i < 256; i++) {
            uint64_t wide = smith_crc64_tables[table - 1][i];
            smith_crc64_tables[table][i] = (wide >> 8) ^ smith_crc64_tables[0][wide & 255U];
        }
    }
    smith_crc64_ready = true;
}

static uint32_t smith_load32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t smith_load64(const uint8_t *p) {
    return (uint64_t)smith_load32(p) | ((uint64_t)smith_load32(p + 4) << 32);
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

constexpr uint64_t smith_xz_dictionary_max = UINT64_C(1) << 30;
constexpr size_t smith_xz_blocks_max = 65536; // blocks in one stream; xz -T makes tens for the largest packages
constexpr size_t smith_xz_streams_max = 1000000;
constexpr size_t smith_xz_dictionary_min = 4096;

typedef struct {
    uint64_t unpadded; // header, compressed data and check
    uint64_t size;     // uncompressed
} SmithXzRecord;

static SmithXzRecord smith_xz_records[smith_xz_blocks_max];

typedef struct {
    const uint8_t *in;
    const uint8_t *end;
    uint32_t range;
    uint32_t code;
    bool over; // read past the end of the chunk
} SmithRange;

typedef struct {
    uint16_t choice;
    uint16_t choice2;
    uint16_t low[16][8];
    uint16_t mid[16][8];
    uint16_t high[256];
} SmithLzmaLength;

// The probabilities come first and are all reset together; the state follows.
typedef struct {
    uint16_t is_match[12][16];
    uint16_t is_rep[12];
    uint16_t is_rep0[12];
    uint16_t is_rep1[12];
    uint16_t is_rep2[12];
    uint16_t is_rep0_long[12][16];
    uint16_t slot[4][64];
    uint16_t special[115];
    uint16_t align[16];
    SmithLzmaLength match_length;
    SmithLzmaLength rep_length;
    uint16_t literal[0x300U << 4];
    unsigned state;
    uint32_t rep[4];
    unsigned lc;
    unsigned lp;
    unsigned pb;
} SmithLzma;

typedef struct {
    uint8_t *buffer;   // the mapping
    size_t capacity;   // its size
    size_t size;       // the dictionary of this block, at most the capacity
    size_t at;         // where the next byte goes
    size_t flushed;    // bytes before this went to the sink
    uint64_t position; // bytes since the last dictionary reset
} SmithDictionary;

typedef struct {
    SmithArchiveSink sink;
    void *context;
    SmithPackageFault *error;
    SmithDictionary dictionary;
    SmithLzma lzma;
    unsigned check; // the stream's check type
    uint32_t crc32; // of the block so far
    uint64_t crc64;
    SmithSha256 sha256;
    uint64_t block_bytes; // uncompressed bytes of the block so far
} SmithXz;

static bool smith_xz_fail(SmithXz *xz, const char *reason) {
    return smith_package_fail(xz->error, "xz: %s", reason);
}

// The same for functions that return a size (0 is the failure) or a check type (-1 is).
static size_t smith_xz_fail_zero(SmithXz *xz, const char *reason) {
    smith_xz_fail(xz, reason);
    return 0;
}

static int smith_xz_fail_negative(SmithXz *xz, const char *reason) {
    smith_xz_fail(xz, reason);
    return -1;
}

// Flushes the dictionary's new bytes to the sink and the block's check.
static bool smith_xz_flush(SmithXz *xz) {
    SmithDictionary *dictionary = &xz->dictionary;
    size_t count = dictionary->at - dictionary->flushed;
    if (count == 0) return true;
    const uint8_t *data = dictionary->buffer + dictionary->flushed;
    if (xz->check == 1)
        xz->crc32 = smith_inflate_crc32(xz->crc32, data, count);
    else if (xz->check == 4)
        xz->crc64 = smith_crc64(xz->crc64, data, count);
    else if (xz->check == 10)
        smith_sha256_update(&xz->sha256, data, count);
    xz->block_bytes += count;
    dictionary->flushed = dictionary->at;
    return xz->sink(xz->context, data, count);
}

[[gnu::always_inline]] static bool smith_xz_put(SmithXz *xz, uint8_t byte) {
    SmithDictionary *dictionary = &xz->dictionary;
    dictionary->buffer[dictionary->at++] = byte;
    dictionary->position++;
    if (dictionary->at < dictionary->size) return true;
    if (!smith_xz_flush(xz)) return false;
    dictionary->at = 0;
    dictionary->flushed = 0;
    return true;
}

// Copies `length` bytes from `distance` back (1 is the last byte). The caller has checked the distance.
static bool smith_xz_copy(SmithXz *xz, uint32_t distance, uint32_t length) {
    SmithDictionary *dictionary = &xz->dictionary;
    if (dictionary->at >= distance && dictionary->at + length < dictionary->size) { // the common case: no wrap
        uint8_t *to = dictionary->buffer + dictionary->at;
        const uint8_t *source = to - distance;
        if (distance >= length) {
            memcpy(to, source, length);
        } else {
            for (uint32_t i = 0; i < length; i++)
                to[i] = source[i];
        }
        dictionary->at += length;
        dictionary->position += length;
        return true;
    }
    size_t from = dictionary->at >= distance ? dictionary->at - distance : dictionary->at + dictionary->size - distance;
    for (uint32_t i = 0; i < length; i++) {
        if (!smith_xz_put(xz, dictionary->buffer[from])) return false;
        if (++from == dictionary->size) from = 0;
    }
    return true;
}

[[gnu::always_inline]] static void smith_rc_normalize(SmithRange *rc) {
    if (rc->range >= (1U << 24)) return;
    rc->range <<= 8;
    uint32_t byte = 0;
    if (rc->in < rc->end)
        byte = *rc->in++;
    else
        rc->over = true;
    rc->code = (rc->code << 8) | byte;
}

[[gnu::always_inline]] static unsigned smith_rc_bit(SmithRange *rc, uint16_t *probability) {
    uint32_t p = *probability;
    uint32_t bound = (rc->range >> 11) * p;
    unsigned bit = 0;
    if (rc->code < bound) {
        rc->range = bound;
        *probability = (uint16_t)(p + ((2048U - p) >> 5));
    } else {
        rc->range -= bound;
        rc->code -= bound;
        *probability = (uint16_t)(p - (p >> 5));
        bit = 1;
    }
    smith_rc_normalize(rc);
    return bit;
}

static uint32_t smith_rc_direct(SmithRange *rc, unsigned bits) {
    uint32_t result = 0;
    for (unsigned i = 0; i < bits; i++) {
        rc->range >>= 1;
        uint32_t bit = rc->code >= rc->range;
        if (bit != 0) rc->code -= rc->range;
        result = (result << 1) | bit;
        smith_rc_normalize(rc);
    }
    return result;
}

// A tree of `bits` levels of probabilities, most significant bit first; probabilities[1..] are used.
[[gnu::always_inline]] static unsigned smith_rc_tree(SmithRange *rc, uint16_t *probabilities, unsigned bits) {
    unsigned m = 1;
    for (unsigned i = 0; i < bits; i++)
        m = (m << 1) | smith_rc_bit(rc, probabilities + m);
    return m - (1U << bits);
}

[[gnu::always_inline]] static unsigned smith_rc_tree_reverse(SmithRange *rc, uint16_t *probabilities, unsigned bits) {
    unsigned m = 1;
    unsigned result = 0;
    for (unsigned i = 0; i < bits; i++) {
        unsigned bit = smith_rc_bit(rc, probabilities + m);
        m = (m << 1) | bit;
        result |= bit << i;
    }
    return result;
}

// A length, from 0: 0 to 7, 8 to 15 and 16 to 271.
static unsigned smith_lzma_length(SmithRange *rc, SmithLzmaLength *length, unsigned pos_state) {
    if (smith_rc_bit(rc, &length->choice) == 0) return smith_rc_tree(rc, length->low[pos_state], 3);
    if (smith_rc_bit(rc, &length->choice2) == 0) return 8U + smith_rc_tree(rc, length->mid[pos_state], 3);
    return 16U + smith_rc_tree(rc, length->high, 8);
}

// A distance, from 0; 0xFFFFFFFF is the end marker.
static uint32_t smith_lzma_distance(SmithRange *rc, SmithLzma *lzma, unsigned length) {
    unsigned slot = smith_rc_tree(rc, lzma->slot[length < 4 ? length : 3], 6);
    if (slot < 4) return slot;
    unsigned direct = (slot >> 1) - 1U;
    uint32_t distance = (2U | (slot & 1U)) << direct;
    if (slot < 14) return distance + smith_rc_tree_reverse(rc, lzma->special + distance - slot, direct);
    distance += smith_rc_direct(rc, direct - 4U) << 4;
    return distance + smith_rc_tree_reverse(rc, lzma->align, 4);
}

static bool smith_lzma_literal(SmithXz *xz, SmithRange *rc) {
    SmithLzma *lzma = &xz->lzma;
    SmithDictionary *dictionary = &xz->dictionary;
    unsigned previous = 0;
    if (dictionary->position > 0)
        previous = dictionary->buffer[(dictionary->at == 0 ? dictionary->size : dictionary->at) - 1];
    unsigned context =
        (((unsigned)dictionary->position & ((1U << lzma->lp) - 1U)) << lzma->lc) + (previous >> (8U - lzma->lc));
    uint16_t *probabilities = lzma->literal + (size_t)0x300 * context;
    unsigned symbol = 1;
    if (lzma->state >= 7) { // after a match the byte at rep0 steers the first bits
        size_t back = (size_t)lzma->rep[0] + 1U;
        size_t from = dictionary->at >= back ? dictionary->at - back : dictionary->at + dictionary->size - back;
        unsigned match = dictionary->buffer[from];
        for (unsigned i = 0; i < 8 && symbol < 0x100; i++) {
            unsigned match_bit = (match >> 7) & 1U;
            match <<= 1;
            unsigned bit = smith_rc_bit(rc, probabilities + ((1U + match_bit) << 8) + symbol);
            symbol = (symbol << 1) | bit;
            if (match_bit != bit) break;
        }
    }
    for (unsigned i = 0; i < 8 && symbol < 0x100; i++)
        symbol = (symbol << 1) | smith_rc_bit(rc, probabilities + symbol);
    lzma->state = lzma->state < 4 ? 0 : (lzma->state < 10 ? lzma->state - 3 : lzma->state - 6);
    return smith_xz_put(xz, (uint8_t)symbol);
}

// A rep match: the distance is one of the last four, picked by up to three bits. Returns the length to copy from
// rep[0], or 0 for the one-byte short rep.
static int64_t smith_lzma_rep(SmithXz *xz, SmithRange *rc, unsigned pos_state) {
    SmithLzma *lzma = &xz->lzma;
    unsigned state = lzma->state;
    if (smith_rc_bit(rc, &lzma->is_rep0[state]) == 0) {
        if (smith_rc_bit(rc, &lzma->is_rep0_long[state][pos_state]) == 0) {
            lzma->state = state < 7 ? 9 : 11;
            return 0;
        }
    } else {
        uint32_t distance = 0;
        if (smith_rc_bit(rc, &lzma->is_rep1[state]) == 0) {
            distance = lzma->rep[1];
        } else {
            if (smith_rc_bit(rc, &lzma->is_rep2[state]) == 0) {
                distance = lzma->rep[2];
            } else {
                distance = lzma->rep[3];
                lzma->rep[3] = lzma->rep[2];
            }
            lzma->rep[2] = lzma->rep[1];
        }
        lzma->rep[1] = lzma->rep[0];
        lzma->rep[0] = distance;
    }
    lzma->state = state < 7 ? 8 : 11;
    return (int64_t)smith_lzma_length(rc, &lzma->rep_length, pos_state) + 2;
}

// One symbol of a chunk. Returns the bytes it produced, or 0 after setting the error.
static size_t smith_lzma_symbol(SmithXz *xz, SmithRange *rc, uint64_t remaining) {
    SmithLzma *lzma = &xz->lzma;
    SmithDictionary *dictionary = &xz->dictionary;
    unsigned pos_state = (unsigned)dictionary->position & ((1U << lzma->pb) - 1U);
    unsigned state = lzma->state;
    if (smith_rc_bit(rc, &lzma->is_match[state][pos_state]) == 0) {
        return smith_lzma_literal(xz, rc) ? 1 : 0;
    }
    int64_t length = 0;
    if (smith_rc_bit(rc, &lzma->is_rep[state]) == 0) {
        lzma->rep[3] = lzma->rep[2];
        lzma->rep[2] = lzma->rep[1];
        lzma->rep[1] = lzma->rep[0];
        unsigned coded = smith_lzma_length(rc, &lzma->match_length, pos_state);
        lzma->state = state < 7 ? 7 : 10;
        lzma->rep[0] = smith_lzma_distance(rc, lzma, coded);
        if (lzma->rep[0] == UINT32_MAX) return smith_xz_fail_zero(xz, "an LZMA2 chunk holds an end marker");
        length = (int64_t)coded + 2;
    } else {
        length = smith_lzma_rep(xz, rc, pos_state);
    }
    uint64_t available = dictionary->position < dictionary->size ? dictionary->position : dictionary->size;
    if ((uint64_t)lzma->rep[0] + 1U > available)
        return smith_xz_fail_zero(xz, "a match reaches before the start of the data");
    if (length == 0) length = 1;
    if ((uint64_t)length > remaining) return smith_xz_fail_zero(xz, "a match runs past the end of its chunk");
    return smith_xz_copy(xz, lzma->rep[0] + 1U, (uint32_t)length) ? (size_t)length : 0;
}

static void smith_lzma_reset(SmithLzma *lzma) {
    uint16_t *probabilities = (uint16_t *)lzma;
    for (size_t i = 0; i < offsetof(SmithLzma, state) / sizeof(uint16_t); i++)
        probabilities[i] = 1024;
    lzma->state = 0;
    memset(lzma->rep, 0, sizeof lzma->rep);
}

// One LZMA chunk: `compressed` bytes at `in` that decode to exactly `uncompressed` bytes.
static bool smith_lzma_chunk(SmithXz *xz, const uint8_t *in, size_t compressed, uint64_t uncompressed) {
    if (compressed < 5 || in[0] != 0)
        return smith_xz_fail(xz, "an LZMA2 chunk does not start with a range coder header");
    SmithRange rc = {.in = in + 5, .end = in + compressed, .range = 0xFFFFFFFFU};
    rc.code = ((uint32_t)in[1] << 24) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 8) | (uint32_t)in[4];
    uint64_t remaining = uncompressed;
    while (remaining > 0) {
        size_t produced = smith_lzma_symbol(xz, &rc, remaining);
        if (produced == 0) return false;
        if (rc.over) return smith_xz_fail(xz, "an LZMA2 chunk's data is cut short");
        remaining -= produced;
    }
    if (rc.in != rc.end || rc.code != 0) return smith_xz_fail(xz, "an LZMA2 chunk's data is damaged");
    return smith_xz_flush(xz);
}

static void smith_xz_reset_dictionary(SmithXz *xz) {
    xz->dictionary.at = 0;
    xz->dictionary.flushed = 0;
    xz->dictionary.position = 0;
}

// The properties byte of an LZMA2 chunk: lc, lp and pb, with lc + lp at most 4.
static bool smith_lzma_properties(SmithXz *xz, unsigned byte) {
    if (byte >= 9U * 5U * 5U) return smith_xz_fail(xz, "an LZMA2 chunk has damaged properties");
    xz->lzma.lc = byte % 9U;
    xz->lzma.lp = byte / 9U % 5U;
    xz->lzma.pb = byte / 45U;
    if (xz->lzma.lc + xz->lzma.lp > 4) return smith_xz_fail(xz, "an LZMA2 chunk has more than 4 bits of lc and lp");
    return true;
}

typedef struct {
    bool need_dictionary;
    bool need_properties;
} SmithLzma2State;

// A compressed chunk's header, which starts at in[at] after the control byte. Returns the bytes before the data, or 0.
static size_t smith_lzma2_header(SmithXz *xz, SmithLzma2State *state, const uint8_t *in, size_t size,
                                 uint64_t *uncompressed, size_t *compressed) {
    unsigned control = in[0];
    unsigned mode = (control >> 5) & 3U;
    size_t header = mode >= 2 ? 6 : 5;
    if (size < header) return smith_xz_fail_zero(xz, "the data ends inside an LZMA2 chunk header");
    *uncompressed = ((((uint64_t)control & 0x1FU) << 16) | ((uint64_t)in[1] << 8) | in[2]) + 1U;
    *compressed = (((size_t)in[3] << 8) | in[4]) + 1U;
    if (mode == 3) {
        smith_xz_reset_dictionary(xz);
        state->need_dictionary = false;
    } else if (state->need_dictionary) {
        return smith_xz_fail_zero(xz, "an LZMA2 stream does not start by resetting its dictionary");
    }
    if (mode >= 2) {
        if (!smith_lzma_properties(xz, in[5])) return 0;
        state->need_properties = false;
    } else if (state->need_properties) {
        return smith_xz_fail_zero(xz, "an LZMA2 chunk comes before any properties");
    }
    if (mode >= 1) smith_lzma_reset(&xz->lzma);
    return header;
}

// An uncompressed chunk's data goes through the dictionary like decoded bytes.
static bool smith_lzma2_stored(SmithXz *xz, const uint8_t *in, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (!smith_xz_put(xz, in[i])) return false;
    }
    return smith_xz_flush(xz);
}

// The LZMA2 data of a block, to its end byte. `used` is the bytes it takes.
static bool smith_xz_lzma2(SmithXz *xz, const uint8_t *in, size_t size, size_t *used) {
    SmithLzma2State state = {.need_dictionary = true, .need_properties = true};
    size_t at = 0;
    for (size_t chunk = 0; chunk <= size; chunk++) {
        if (at >= size) return smith_xz_fail(xz, "the data ends inside a block");
        unsigned control = in[at];
        if (control == 0) {
            *used = at + 1;
            return smith_xz_flush(xz);
        }
        if (control >= 0x80) {
            uint64_t uncompressed = 0;
            size_t compressed = 0;
            size_t header = smith_lzma2_header(xz, &state, in + at, size - at, &uncompressed, &compressed);
            if (header == 0) return false;
            if (size - at - header < compressed) return smith_xz_fail(xz, "the data ends inside an LZMA2 chunk");
            if (!smith_lzma_chunk(xz, in + at + header, compressed, uncompressed)) return false;
            at += header + compressed;
        } else if (control <= 2) {
            if (size - at < 3) return smith_xz_fail(xz, "the data ends inside an LZMA2 chunk header");
            size_t length = (((size_t)in[at + 1] << 8) | in[at + 2]) + 1U;
            if (control == 1) {
                smith_xz_reset_dictionary(xz);
                state.need_dictionary = false;
                state.need_properties = true;
            } else if (state.need_dictionary) {
                return smith_xz_fail(xz, "an LZMA2 stream does not start by resetting its dictionary");
            }
            if (size - at - 3 < length) return smith_xz_fail(xz, "the data ends inside an LZMA2 chunk");
            if (!smith_lzma2_stored(xz, in + at + 3, length)) return false;
            at += 3 + length;
        } else {
            return smith_xz_fail(xz, "an LZMA2 chunk has an unknown control byte");
        }
    }
    return smith_xz_fail(xz, "an LZMA2 stream does not end");
}

// An xz variable-length integer: 7 bits a byte, low first, at most 9 bytes, the shortest form. False when it is cut off
// at `end` or damaged.
static bool smith_xz_varint(const uint8_t **at, const uint8_t *end, uint64_t *value) {
    uint64_t result = 0;
    for (unsigned i = 0; i < 9; i++) {
        if (*at >= end) return false;
        uint8_t byte = *(*at)++;
        result |= (uint64_t)(byte & 0x7FU) << (7U * i);
        if ((byte & 0x80U) == 0) {
            if (byte == 0 && i > 0) return false;
            *value = result;
            return true;
        }
    }
    return false;
}

// Maps a dictionary of at least `size` bytes, keeping an earlier mapping when it is big enough.
static bool smith_xz_dictionary(SmithXz *xz, uint64_t size) {
    SmithDictionary *dictionary = &xz->dictionary;
    if (size < smith_xz_dictionary_min) size = smith_xz_dictionary_min;
    if (size > smith_xz_dictionary_max) return smith_xz_fail(xz, "a block asks for a dictionary over 1 GiB");
    if (dictionary->capacity < size) {
        if (dictionary->buffer != nullptr) munmap(dictionary->buffer, dictionary->capacity);
        dictionary->buffer = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        dictionary->capacity = 0;
        if (dictionary->buffer == MAP_FAILED) {
            dictionary->buffer = nullptr;
            return smith_package_fail(xz->error, "xz: cannot map a dictionary of %llu bytes: %s",
                                      (unsigned long long)size, strerror(errno));
        }
        dictionary->capacity = (size_t)size;
    }
    dictionary->size = (size_t)size;
    return true;
}

// A block header at in[0..size): checks it and maps the dictionary. Returns its length, or 0. `declared` gets the
// compressed and uncompressed sizes it states, or UINT64_MAX when it does not.
static size_t smith_xz_block_header(SmithXz *xz, const uint8_t *in, size_t size, uint64_t declared[2]) {
    size_t length = ((size_t)in[0] + 1U) * 4U;
    if (size < length) return smith_xz_fail_zero(xz, "the data ends inside a block header");
    if (smith_inflate_crc32(0, in, length - 4) != smith_load32(in + length - 4)) {
        return smith_xz_fail_zero(xz, "a block header's CRC-32 does not match");
    }
    unsigned flags = in[1];
    const uint8_t *at = in + 2;
    const uint8_t *end = in + length - 4;
    declared[0] = declared[1] = UINT64_MAX;
    bool ok = (flags & 0x3CU) == 0;
    if (ok && (flags & 0x40U) != 0) ok = smith_xz_varint(&at, end, &declared[0]);
    if (ok && (flags & 0x80U) != 0) ok = smith_xz_varint(&at, end, &declared[1]);
    uint64_t filter = 0;
    uint64_t properties = 0;
    ok = ok && smith_xz_varint(&at, end, &filter) && smith_xz_varint(&at, end, &properties);
    if (!ok) return smith_xz_fail_zero(xz, "a block header is damaged");
    if ((flags & 3U) != 0 || filter != 0x21) {
        smith_package_fail(xz->error, "xz: unsupported filter 0x%llx (only LZMA2 is)", (unsigned long long)filter);
        return 0;
    }
    if (properties != 1 || at >= end || *at > 40)
        return smith_xz_fail_zero(xz, "a block's LZMA2 dictionary size is damaged");
    unsigned byte = *at++;
    for (; at < end; at++) {
        if (*at != 0) return smith_xz_fail_zero(xz, "a block header's padding is not zero");
    }
    uint64_t dictionary = byte == 40 ? UINT64_C(0xFFFFFFFF) : (UINT64_C(2) | (byte & 1U)) << (byte / 2U + 11U);
    return smith_xz_dictionary(xz, dictionary) ? length : 0;
}

static size_t smith_xz_check_size(unsigned check) {
    return check == 0 ? 0 : (check == 1 ? 4 : (check == 4 ? 8 : 32));
}

static void smith_xz_check_start(SmithXz *xz) {
    xz->crc32 = 0;
    xz->crc64 = 0;
    xz->block_bytes = 0;
    smith_sha256_init(&xz->sha256);
}

// Compares the block's check, stored after its data, with the one computed on the way.
static bool smith_xz_check_finish(SmithXz *xz, const uint8_t *stored) {
    bool same = true;
    if (xz->check == 1) {
        same = smith_load32(stored) == xz->crc32;
    } else if (xz->check == 4) {
        same = smith_load64(stored) == xz->crc64;
    } else if (xz->check == 10) {
        char hex[smith_sha256_hex_size];
        smith_sha256_final(&xz->sha256, hex); // leaves the digest in the state, as eight big-endian words
        for (size_t i = 0; i < 32 && same; i++)
            same = stored[i] == (uint8_t)(xz->sha256.state[i / 4] >> (24U - 8U * (i % 4U)));
    }
    return same || smith_xz_fail(xz, "a block's check does not match its data");
}

// One block at in[0..size): header, LZMA2 data, padding and check. Returns the bytes it takes in `used` and what the
// index should say about it in `record`.
static bool smith_xz_block(SmithXz *xz, const uint8_t *in, size_t size, size_t *used, SmithXzRecord *record) {
    uint64_t declared[2];
    size_t header = smith_xz_block_header(xz, in, size, declared);
    if (header == 0) return false;
    smith_xz_check_start(xz);
    smith_xz_reset_dictionary(xz);
    size_t data = 0;
    if (!smith_xz_lzma2(xz, in + header, size - header, &data)) return false;
    if ((declared[0] != UINT64_MAX && declared[0] != data) ||
        (declared[1] != UINT64_MAX && declared[1] != xz->block_bytes)) {
        return smith_xz_fail(xz, "a block's size does not match its header");
    }
    size_t padding = (4U - (data & 3U)) & 3U;
    size_t check = smith_xz_check_size(xz->check);
    if (size - header - data < padding + check) return smith_xz_fail(xz, "the data ends inside a block's check");
    for (size_t i = 0; i < padding; i++) {
        if (in[header + data + i] != 0) return smith_xz_fail(xz, "a block's padding is not zero");
    }
    if (!smith_xz_check_finish(xz, in + header + data + padding)) return false;
    *used = header + data + padding + check;
    record->unpadded = header + data + check;
    record->size = xz->block_bytes;
    return true;
}

// The index at in[0..size), which must list exactly `count` blocks like `records`. Returns the bytes it takes.
static size_t smith_xz_index(SmithXz *xz, const uint8_t *in, size_t size, const SmithXzRecord *records, size_t count) {
    const uint8_t *at = in + 1; // after the 0x00 indicator
    const uint8_t *end = in + size;
    uint64_t listed = 0;
    if (!smith_xz_varint(&at, end, &listed) || listed != count) {
        return smith_xz_fail_zero(xz, "the index does not list the blocks of the stream");
    }
    for (size_t i = 0; i < count; i++) {
        uint64_t unpadded = 0;
        uint64_t uncompressed = 0;
        if (!smith_xz_varint(&at, end, &unpadded) || !smith_xz_varint(&at, end, &uncompressed)) {
            return smith_xz_fail_zero(xz, "the index is cut off or damaged");
        }
        if (unpadded != records[i].unpadded || uncompressed != records[i].size) {
            return smith_xz_fail_zero(xz, "the index does not match the blocks");
        }
    }
    size_t body = (size_t)(at - in);
    size_t padded = (body + 3U) & ~(size_t)3U;
    if (size < padded + 4) return smith_xz_fail_zero(xz, "the data ends inside the index");
    for (size_t i = body; i < padded; i++) {
        if (in[i] != 0) return smith_xz_fail_zero(xz, "the index's padding is not zero");
    }
    if (smith_inflate_crc32(0, in, padded) != smith_load32(in + padded))
        return smith_xz_fail_zero(xz, "the index's CRC-32 does not match");
    return padded + 4;
}

// The 12-byte stream header at in[0], or footer. Returns the check type, or -1 after setting the error.
static int smith_xz_stream_header(SmithXz *xz, const uint8_t *in, size_t size) {
    static const uint8_t magic[6] = {0xFD, '7', 'z', 'X', 'Z', 0};
    if (size < 12 || memcmp(in, magic, 6) != 0) return smith_xz_fail_negative(xz, "not an xz stream");
    if (in[6] != 0 || (in[7] & 0xF0U) != 0) return smith_xz_fail_negative(xz, "a stream header uses an unknown flag");
    if (smith_inflate_crc32(0, in + 6, 2) != smith_load32(in + 8))
        return smith_xz_fail_negative(xz, "a stream header's CRC-32 does not match");
    unsigned check = in[7] & 0x0FU;
    if (check != 0 && check != 1 && check != 4 && check != 10) {
        smith_package_fail(xz->error, "xz: unsupported check type %u (only none, CRC-32, CRC-64 and SHA-256 are)",
                           check);
        return -1;
    }
    return (int)check;
}

static bool smith_xz_footer(SmithXz *xz, const uint8_t *footer, const uint8_t *header, size_t index_size) {
    if (footer[10] != 'Y' || footer[11] != 'Z') return smith_xz_fail(xz, "a stream's footer is damaged");
    if (smith_inflate_crc32(0, footer + 4, 6) != smith_load32(footer))
        return smith_xz_fail(xz, "a stream footer's CRC-32 does not match");
    if (((uint64_t)smith_load32(footer + 4) + 1U) * 4U != index_size || memcmp(footer + 8, header + 6, 2) != 0) {
        return smith_xz_fail(xz, "a stream's footer does not match its header");
    }
    return true;
}

// One stream at in[0..size): header, blocks, index, footer. Returns the bytes it takes.
static size_t smith_xz_stream(SmithXz *xz, const uint8_t *in, size_t size) {
    int check = smith_xz_stream_header(xz, in, size);
    if (check < 0) return 0;
    xz->check = (unsigned)check;
    size_t at = 12;
    size_t blocks = 0;
    for (size_t step = 0; step <= size; step++) {
        if (at >= size) return smith_xz_fail_zero(xz, "the data ends inside a stream");
        if (in[at] == 0) break; // the index
        if (blocks >= smith_xz_blocks_max) return smith_xz_fail_zero(xz, "a stream has more than 65536 blocks");
        size_t used = 0;
        if (!smith_xz_block(xz, in + at, size - at, &used, &smith_xz_records[blocks])) return 0;
        blocks++;
        at += used;
    }
    size_t index = smith_xz_index(xz, in + at, size - at, smith_xz_records, blocks);
    if (index == 0) return 0;
    at += index;
    if (size - at < 12) return smith_xz_fail_zero(xz, "the data ends inside a stream's footer");
    return smith_xz_footer(xz, in + at, in, index) ? at + 12 : 0;
}

// Decodes an xz file to the sink. Zero padding between streams (a multiple of 4 bytes) and after the last is allowed.
static bool smith_xz_decode_streams(SmithXz *xz, const uint8_t *in, size_t size) {
    size_t at = 0;
    for (size_t stream = 0; stream < smith_xz_streams_max; stream++) {
        size_t used = smith_xz_stream(xz, in + at, size - at);
        if (used == 0) return false;
        at += used;
        size_t zeros = 0;
        while (at + zeros < size && in[at + zeros] == 0)
            zeros++;
        if (at + zeros == size && zeros % 4 == 0) return true;
        if (zeros % 4 != 0) return smith_xz_fail(xz, "the padding after a stream is not a multiple of 4 bytes");
        at += zeros;
    }
    return smith_xz_fail(xz, "too many streams");
}

static bool smith_xz_decode(const uint8_t *in, size_t size, SmithArchiveSink sink, void *context,
                            SmithPackageFault *error) {
    smith_crc64_init();
    static SmithXz xz;
    xz = (SmithXz){.sink = sink, .context = context, .error = error};
    bool ok = smith_xz_decode_streams(&xz, in, size);
    if (xz.dictionary.buffer != nullptr) munmap(xz.dictionary.buffer, xz.dictionary.capacity);
    return ok;
}
