// SHA-256 in plain C, for the programs in scripts/ that run before there is a smith (stage0.smith, llvm_linux.smith):
// they check a download's pin and hash what an LLVM install links. runtime/package.c has smith's own copy, which the
// build program embeds. Include it once, in a file that has <stdint.h>, <stdio.h> and <string.h>.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} Sha256;

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t sha256_rotate(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

static void sha256_init(Sha256 *sha) {
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(sha->state, start, sizeof start);
    sha->bytes = 0;
    sha->used = 0;
}

static void sha256_block(Sha256 *sha, const uint8_t *block) {
    uint32_t w[64];
    for (size_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotate(w[i - 15], 7) ^ sha256_rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha256_rotate(w[i - 2], 17) ^ sha256_rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t v[8];
    memcpy(v, sha->state, sizeof v);
    for (size_t i = 0; i < 64; i++) {
        uint32_t s1 = sha256_rotate(v[4], 6) ^ sha256_rotate(v[4], 11) ^ sha256_rotate(v[4], 25);
        uint32_t choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t first = v[7] + s1 + choose + sha256_k[i] + w[i];
        uint32_t s0 = sha256_rotate(v[0], 2) ^ sha256_rotate(v[0], 13) ^ sha256_rotate(v[0], 22);
        uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += first;
        v[0] = first + s0 + majority;
    }
    for (size_t i = 0; i < 8; i++)
        sha->state[i] += v[i];
}

static void sha256_update(Sha256 *sha, const void *data, size_t size) {
    const uint8_t *bytes = data;
    sha->bytes += size;
    for (size_t at = 0; at < size;) { // each pass takes at least one byte
        size_t take = sizeof sha->block - sha->used;
        if (take > size - at) take = size - at;
        memcpy(sha->block + sha->used, bytes + at, take);
        sha->used += take;
        at += take;
        if (sha->used == sizeof sha->block) {
            sha256_block(sha, sha->block);
            sha->used = 0;
        }
    }
}

// The digest as 64 lowercase hex digits and a terminator.
static void sha256_final(Sha256 *sha, char hex[65]) {
    uint64_t bits = sha->bytes * 8U;
    uint8_t pad[72] = {0x80};
    size_t zeros = (sha->used < 56 ? 56 : 120) - sha->used; // up to 56 bytes into a block, then the length
    sha256_update(sha, pad, zeros);
    uint8_t length[8];
    for (size_t i = 0; i < 8; i++)
        length[i] = (uint8_t)(bits >> (56U - 8U * i));
    sha256_update(sha, length, sizeof length);
    for (size_t i = 0; i < 8; i++)
        snprintf(hex + 8 * i, 9, "%08x", sha->state[i]);
}

// The SHA-256 of a file, as 64 lowercase hex digits; false when it cannot be read.
[[maybe_unused]] static bool sha256_file(const char *path, char hex[65]) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    Sha256 sha;
    sha256_init(&sha);
    static uint8_t buffer[1 << 16];
    for (size_t got = 1; got > 0;) { // each pass reads one buffer until the end
        got = fread(buffer, 1, sizeof buffer, file);
        sha256_update(&sha, buffer, got);
    }
    bool read = ferror(file) == 0;
    fclose(file);
    sha256_final(&sha, hex);
    return read;
}
