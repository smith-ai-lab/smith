// Runs the kernels that smith wrote from elementwise.smith (argv[1], a .metallib or Metal source) and the learner's
// handwritten ones (argv[2], source) over the same random inputs and checks that every output is the same, bit for
// bit; the sums are also checked against a double precision sum. Both libraries are built without fast math. Exits 77
// when there is no Metal device.
#import <Metal/Metal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t count, columns;
    float alpha, beta, gamma, epsilon;
} Uniforms;

typedef struct {
    uint32_t rows, columns, parts;
    float alpha;
} ColsumUniforms;

static id<MTLDevice> device;

static id<MTLLibrary> library(const char *path) {
    NSError *error = nil;
    NSData *data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
    if (!data) return nil;
    MTLCompileOptions *options = [MTLCompileOptions new];
    options.mathMode = MTLMathModeSafe; // what `metal -fno-fast-math` builds, with the precise float functions
    options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
    id<MTLLibrary> result = nil;
    if (data.length > 4 && memcmp(data.bytes, "MTLB", 4) == 0) {
        dispatch_data_t bytes = dispatch_data_create(data.bytes, data.length, nil, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        result = [device newLibraryWithData:bytes error:&error];
    } else {
        NSString *source = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        result = [device newLibraryWithSource:source options:options error:&error];
    }
    if (!result) fprintf(stderr, "%s: %s\n", path, [[error localizedDescription] UTF8String]);
    return result;
}

static id<MTLBuffer> floats(uint32_t count, uint32_t seed, float scale) {
    id<MTLBuffer> buffer = [device newBufferWithLength:count * sizeof(float) options:MTLResourceStorageModeShared];
    float *p = buffer.contents;
    uint32_t x = seed * 2654435761u + 12345u;
    for (uint32_t i = 0; i < count; i++) {
        x = x * 1664525u + 1013904223u;
        p[i] = ((float)(x >> 8) / (float)(1 << 24) - 0.5f) * 2.0f * scale;
    }
    return buffer;
}

static id<MTLBuffer> copy(id<MTLBuffer> source) {
    return [device newBufferWithBytes:source.contents length:source.length options:MTLResourceStorageModeShared];
}

// One dispatch: the uniforms at buffer 0 as bytes, then buffers (slot, buffer) pairs.
typedef struct {
    uint32_t slot;
    id<MTLBuffer> buffer;
} Bind;

static bool run(id<MTLLibrary> lib, const char *entry, const void *uniforms, size_t size, const Bind *binds,
                size_t count, MTLSize groups, MTLSize threads) {
    NSError *error = nil;
    id<MTLFunction> function = [lib newFunctionWithName:[NSString stringWithUTF8String:entry]];
    id<MTLComputePipelineState> pipeline = function ? [device newComputePipelineStateWithFunction:function error:&error]
                                                    : nil;
    if (!pipeline) {
        fprintf(stderr, "no kernel %s: %s\n", entry, [[error localizedDescription] UTF8String]);
        return false;
    }
    id<MTLCommandBuffer> commands = [[device newCommandQueue] commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    [encoder setBytes:uniforms length:size atIndex:0];
    for (size_t i = 0; i < count; i++)
        [encoder setBuffer:binds[i].buffer offset:0 atIndex:binds[i].slot];
    [encoder dispatchThreadgroups:groups threadsPerThreadgroup:threads];
    [encoder endEncoding];
    [commands commit];
    [commands waitUntilCompleted];
    return commands.status == MTLCommandBufferStatusCompleted;
}

static int failures;

static void same(const char *what, id<MTLBuffer> got, id<MTLBuffer> want, uint32_t count) {
    if (memcmp(got.contents, want.contents, count * sizeof(float)) == 0) return;
    const float *g = got.contents, *w = want.contents;
    for (uint32_t i = 0; i < count; i++) {
        if (memcmp(g + i, w + i, sizeof(float)) != 0) {
            fprintf(stderr, "%s differs at %u: smith %.9g handwritten %.9g\n", what, i, g[i], w[i]);
            break;
        }
    }
    failures++;
}

static MTLSize size(uint32_t x, uint32_t y) {
    return MTLSizeMake(x, y, 1);
}

// The pointwise kernels, one thread an element in groups of 256. `hand` and `mine` are the buffer slots of
// (a, b, c, d, out) in the handwritten and the smith kernel; -1 means the kernel has none.
static void pointwise(id<MTLLibrary> mine, id<MTLLibrary> hand, const char *name, const int *hand_slots,
                      const int *mine_slots, const Uniforms *uniforms) {
    uint32_t n = uniforms->count;
    char entry[64];
    snprintf(entry, sizeof entry, "elementwise_%s_kernel", name);
    id<MTLBuffer> inputs[4] = {floats(n, 1, 3.0f), floats(n, 2, 2.0f), floats(n, 3, 1.5f), floats(n, 4, 1.0f)};
    id<MTLBuffer> bias = floats(uniforms->columns, 5, 2.0f);
    id<MTLBuffer> out[2] = {floats(n, 9, 1.0f), floats(n, 9, 1.0f)};
    id<MTLBuffer> first_moment = nil, second_moment = nil; // adam works in place: its moments are compared too
    for (int side = 0; side < 2; side++) {
        const int *slots = side == 0 ? mine_slots : hand_slots;
        Bind binds[6];
        size_t used = 0;
        id<MTLBuffer> own[4] = {copy(inputs[0]), copy(inputs[1]), copy(inputs[2]), copy(inputs[3])};
        if (strcmp(name, "bias") == 0) own[1] = bias;
        for (int k = 0; k < 4; k++) {
            if (slots[k] >= 0) binds[used++] = (Bind){(uint32_t)slots[k], own[k]};
        }
        if (slots[4] >= 0) binds[used++] = (Bind){(uint32_t)slots[4], out[side]};
        uint32_t groups = (n + 255) / 256;
        if (!run(side == 0 ? mine : hand, entry, uniforms, sizeof *uniforms, binds, used, size(groups, 1),
                 size(256, 1))) {
            failures++;
            return;
        }
        if (strcmp(name, "adam") == 0) { // in place: compare a, c and d
            out[side] = own[0];
            if (side == 0) {
                first_moment = own[2];
                second_moment = own[3];
            } else {
                same("adam first moment", first_moment, own[2], n);
                same("adam second moment", second_moment, own[3], n);
            }
        }
    }
    same(entry, out[0], out[1], n);
}

static void columns(id<MTLLibrary> mine, id<MTLLibrary> hand, uint32_t rows, uint32_t cols, uint32_t parts) {
    ColsumUniforms u = {rows, cols, parts, 0.5f};
    id<MTLBuffer> a = floats(rows * cols, 21, 2.0f);
    id<MTLBuffer> partials[2] = {floats(parts * cols, 0, 0), floats(parts * cols, 0, 0)};
    id<MTLBuffer> out[2] = {floats(cols, 0, 0), floats(cols, 0, 0)};
    uint32_t tiles = (cols + 31) / 32;
    for (int side = 0; side < 2; side++) {
        id<MTLLibrary> lib = side == 0 ? mine : hand;
        Bind first[2] = {{1, a}, {side == 0 ? 2u : 3u, partials[side]}};
        Bind second[2] = {{side == 0 ? 1u : 3u, partials[side]}, {side == 0 ? 2u : 5u, out[side]}};
        if (!run(lib, "elementwise_colsum_kernel", &u, sizeof u, first, 2, size(tiles, parts), size(32, 8)) ||
            !run(lib, "elementwise_colsum_finish_kernel", &u, sizeof u, second, 2, size(tiles, 1), size(32, 8))) {
            failures++;
            return;
        }
    }
    same("colsum partials", partials[0], partials[1], parts * cols);
    same("colsum", out[0], out[1], cols);
    const float *x = a.contents, *got = out[0].contents;
    for (uint32_t c = 0; c < cols; c++) {
        double sum = 0;
        for (uint32_t r = 0; r < rows; r++)
            sum += x[(size_t)r * cols + c];
        if (fabs(got[c] - 0.25 * sum) > 1e-3 * (1 + fabs(sum))) {
            fprintf(stderr, "colsum column %u: %g, want %g\n", c, got[c], 0.25 * sum);
            failures++;
            return;
        }
    }
}

// The reductions: partial sums of a threadgroup each, the same from both and close to the sum in double.
static void reductions(id<MTLLibrary> mine, id<MTLLibrary> hand, const char *mine_name, const char *hand_name,
                       uint32_t hand_out, uint32_t n, bool squares) {
    uint32_t groups = 48;
    id<MTLBuffer> a = floats(n, 31, 2.0f);
    id<MTLBuffer> out[2] = {floats(groups, 0, 0), floats(groups, 0, 0)};
    Uniforms u = {n, 0, 0, 0, 0, 0};
    for (int side = 0; side < 2; side++) {
        Bind binds[2] = {{1, a}, {side == 0 ? 2u : hand_out, out[side]}};
        const char *entry = side == 0 ? mine_name : hand_name;
        if (!run(side == 0 ? mine : hand, entry, &u, squares ? sizeof u : 4, binds, 2, size(groups, 1),
                 size(256, 1))) {
            failures++;
            return;
        }
    }
    same(mine_name, out[0], out[1], groups);
    const float *x = a.contents, *got = out[0].contents;
    double want = 0, have = 0;
    for (uint32_t i = 0; i < n; i++)
        want += squares ? (double)x[i] * x[i] : x[i];
    for (uint32_t g = 0; g < groups; g++)
        have += got[g];
    if (fabs(have - want) > 1e-4 * (1 + fabs(want))) {
        fprintf(stderr, "%s: %g, want %g\n", mine_name, have, want);
        failures++;
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 3) return 2;
        device = MTLCreateSystemDefaultDevice();
        if (!device) return 77;
        id<MTLLibrary> mine = library(argv[1]);
        id<MTLLibrary> hand = library(argv[2]);
        if (!mine || !hand) return 1;
        uint32_t n = 100003;
        Uniforms u = {n, 37, 1.25f, -0.75f, 0.1f, 1e-8f};
        // slots of (a, b, c, d, out) for the handwritten kernel (its fixed buffer numbers) and the smith one (the
        // parameters in order after the thread)
        const int h1[5] = {1, -1, -1, -1, 5}, m1[5] = {1, -1, -1, -1, 2};
        const int h2[5] = {1, 2, -1, -1, 5}, m2[5] = {1, 2, -1, -1, 3};
        const int h4[5] = {1, 2, 3, 4, 5}, m4[5] = {1, 2, 3, 4, 5};
        const int hadam[5] = {1, 2, 3, 4, -1}, madam[5] = {1, 2, 3, 4, -1};
        pointwise(mine, hand, "tanh", h1, m1, &u);
        pointwise(mine, hand, "dtanh", h1, m1, &u);
        pointwise(mine, hand, "gelu", h1, m1, &u);
        pointwise(mine, hand, "bias", h2, m2, &u);
        pointwise(mine, hand, "gelu_back", h2, m2, &u);
        pointwise(mine, hand, "axpby", h2, m2, &u);
        pointwise(mine, hand, "mul", h2, m2, &u);
        pointwise(mine, hand, "settle", h4, m4, &u);
        pointwise(mine, hand, "adam", hadam, madam, &u);
        columns(mine, hand, 1000, 100, 7);
        columns(mine, hand, 5, 33, 1);
        reductions(mine, hand, "elementwise_sq_kernel", "elementwise_sq_kernel", 5, 1u << 20, true);
        reductions(mine, hand, "elementwise_sum_kernel", "reduce_sum_kernel", 2, 1u << 20, false);
        reductions(mine, hand, "elementwise_sum_kernel", "reduce_sum_kernel", 2, 777, false);
        if (failures == 0) printf("ok: the kernels of smith and the handwritten ones give the same bits\n");
        return failures == 0 ? 0 : 1;
    }
}
