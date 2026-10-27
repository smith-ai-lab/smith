// The learner's elementwise_gelu_kernel, copied from learner/src/kernels/elementwise.metal (with the uniforms of
// kernel_types.h): the kernel that smith's output is checked against.
#include <metal_stdlib>
using namespace metal;

struct ElementwiseUniforms {
    uint count;
    uint columns;
    float alpha;
    float beta;
    float gamma;
    float epsilon;
};

constant float elementwise_tanh_limit = 20.0;
constant float elementwise_gelu_c = 0.7978845608028654; // sqrt(2 / pi)
constant float elementwise_gelu_k = 0.044715;

static float elementwise_tanh(float x) {
    return tanh(clamp(x, -elementwise_tanh_limit, elementwise_tanh_limit));
}

kernel void elementwise_gelu_kernel(uint i [[thread_position_in_grid]],
                                    constant ElementwiseUniforms &uniforms [[buffer(0)]],
                                    device const float *a [[buffer(1)]],
                                    device float *out [[buffer(5)]]) {
    if (i >= uniforms.count) return;
    float x = a[i];
    out[i] = 0.5 * x * (1.0 + elementwise_tanh(elementwise_gelu_c * (x + elementwise_gelu_k * x * x * x)));
}
