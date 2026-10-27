// Runs the gelu kernel that smith wrote (argv[1], entry argv[3]) and the learner's handwritten one (argv[2]) on the
// GPU over the same input, and checks both against gelu in double precision with the learner's tolerance
// (4e-6 * (1 + |x|), tests/transformer_test.smith) and one against the other. Exit 77 when there is no Metal device.
#import <Metal/Metal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static id<MTLLibrary> library(id<MTLDevice> device, const char *path) {
    NSError *error = nil;
    NSString *source = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:path]
                                                 encoding:NSUTF8StringEncoding
                                                    error:&error];
    id<MTLLibrary> result = source ? [device newLibraryWithSource:source options:[MTLCompileOptions new] error:&error]
                                   : nil;
    if (!result) fprintf(stderr, "%s: %s\n", path, [[error localizedDescription] UTF8String]);
    return result;
}

static id<MTLBuffer> run(id<MTLDevice> device, id<MTLLibrary> lib, const char *entry, id<MTLBuffer> in, uint32_t count,
                         int out_index, bool uniforms) {
    NSError *error = nil;
    id<MTLFunction> function = [lib newFunctionWithName:[NSString stringWithUTF8String:entry]];
    id<MTLComputePipelineState> pipeline = function ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
    if (!pipeline) {
        fprintf(stderr, "no kernel %s: %s\n", entry, [[error localizedDescription] UTF8String]);
        return nil;
    }
    id<MTLBuffer> out = [device newBufferWithLength:count * sizeof(float) options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> commands = [[device newCommandQueue] commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    if (uniforms) {
        uint32_t block[6] = {count, 0, 0, 0, 0, 0};
        [encoder setBytes:block length:sizeof block atIndex:0];
    } else {
        [encoder setBytes:&count length:sizeof count atIndex:0];
    }
    [encoder setBuffer:in offset:0 atIndex:1];
    [encoder setBuffer:out offset:0 atIndex:out_index];
    [encoder dispatchThreads:MTLSizeMake(count, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
    [encoder endEncoding];
    [commands commit];
    [commands waitUntilCompleted];
    return out;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 4) return 2;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 77;
        id<MTLLibrary> smith = library(device, argv[1]);
        id<MTLLibrary> hand = library(device, argv[2]);
        if (!smith || !hand) return 1;
        uint32_t count = 1u << 20;
        id<MTLBuffer> in = [device newBufferWithLength:count * sizeof(float) options:MTLResourceStorageModeShared];
        float *a = in.contents;
        for (uint32_t i = 0; i < count; i++) a[i] = (float)((i * 2654435761u) % 40000) / 2000.0f - 10.0f; // -10..10
        id<MTLBuffer> got = run(device, smith, argv[3], in, count, 2, false);
        id<MTLBuffer> want = run(device, hand, "elementwise_gelu_kernel", in, count, 5, true);
        if (!got || !want) return 1;
        const float *g = got.contents, *w = want.contents;
        double worst = 0, apart = 0;
        for (uint32_t i = 0; i < count; i++) {
            double x = a[i], gelu = 0.5 * x * (1 + tanh(0.7978845608028654 * (x + 0.044715 * x * x * x)));
            double tolerance = 4e-6 * (1 + fabs(x));
            if (fabs(g[i] - gelu) > tolerance || fabs(w[i] - gelu) > tolerance) {
                fprintf(stderr, "x=%g smith %g handwritten %g reference %g\n", x, g[i], w[i], gelu);
                return 1;
            }
            worst = fmax(worst, fabs(g[i] - gelu));
            apart = fmax(apart, fabs(g[i] - w[i]));
        }
        printf("ok %u elements, worst error %.2e, smith against handwritten %.2e\n", count, worst, apart);
        return apart <= 1e-6 ? 0 : 1;
    }
}
