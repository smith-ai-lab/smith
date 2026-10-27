// The learner's handwritten elementwise and reduce kernels (learner/src/kernels/elementwise.metal, reduce.metal and the types
// of kernel_types.h they use), in one file so that it compiles from source: the baseline the .smith kernels of
// elementwise.smith are checked against, bit for bit.
#include <metal_stdlib>
using namespace metal;

typedef enum {
    ReduceBuffer_Uniforms = 0,
    ReduceBuffer_Input = 1,    // a float per element
    ReduceBuffer_Partials = 2, // a float per threadgroup
} ReduceBuffer;

typedef struct {
    uint32_t count; // elements in the input
} ReduceUniforms;
static_assert(sizeof(ReduceUniforms) == 4, "ReduceUniforms layout");

// The matmul kernel computes C = A B for row-major float matrices: A is m by k, B is k by n and C is m by n, any sizes,
// in tiles of matmul_tile by matmul_tile.
typedef enum {
    MatmulBuffer_Uniforms = 0,
    MatmulBuffer_A = 1,
    MatmulBuffer_B = 2,
    MatmulBuffer_C = 3,
} MatmulBuffer;

typedef struct {
    uint32_t m;
    uint32_t n;
    uint32_t k;
} MatmulUniforms;
static_assert(sizeof(MatmulUniforms) == 12, "MatmulUniforms layout");

// The elementwise kernels, each over count floats (one thread each, any count), except the column sum (one thread per
// column) and the sum of squares (partial sums, as the reduce kernel). A kernel reads the buffers it names and may
// write over one of its inputs. columns is the row length of a matrix stored row by row.
typedef enum {
    ElementwiseBuffer_Uniforms = 0,
    ElementwiseBuffer_A = 1,
    ElementwiseBuffer_B = 2,
    ElementwiseBuffer_C = 3,
    ElementwiseBuffer_D = 4,
    ElementwiseBuffer_Out = 5,
} ElementwiseBuffer;

typedef struct {
    uint32_t count;
    uint32_t columns;
    float alpha;
    float beta;
    float gamma;
    float epsilon;
} ElementwiseUniforms;
static_assert(sizeof(ElementwiseUniforms) == 24, "ElementwiseUniforms layout");

// The column sum's two passes: rows by columns of the input, how many row partitions (partial sums a column), alpha.
typedef struct {
    uint32_t rows;
    uint32_t columns;
    uint32_t parts;
    float alpha;
} ElementwiseColsumUniforms;
static_assert(sizeof(ElementwiseColsumUniforms) == 16, "ElementwiseColsumUniforms layout");



constant uint elementwise_threads = 256; // a threadgroup's threads, which the host dispatches
constant uint elementwise_simds_max = 32;
constant uint colsum_tile = 32;               // columns of a first-pass threadgroup, a simdgroup wide
constant uint colsum_lanes = 8;               // row lanes of a first-pass threadgroup
constant float elementwise_tanh_limit = 20.0; // tanh is 1 to float precision beyond this, and exp would overflow

static float elementwise_tanh(float x) {
    return tanh(clamp(x, -elementwise_tanh_limit, elementwise_tanh_limit));
}

// out = tanh(a)
kernel void elementwise_tanh_kernel(uint i [[thread_position_in_grid]],
                                    constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                    device const float *a [[buffer(ElementwiseBuffer_A)]],
                                    device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    if (i < uniforms.count) out[i] = elementwise_tanh(a[i]);
}

// out = 1 - tanh(a)^2, the slope of tanh at a
kernel void elementwise_dtanh_kernel(uint i [[thread_position_in_grid]],
                                     constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                     device const float *a [[buffer(ElementwiseBuffer_A)]],
                                     device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    if (i >= uniforms.count) return;
    float t = elementwise_tanh(a[i]);
    out[i] = 1.0 - t * t;
}

// out = a + b[column]: a bias vector of length columns added to every row of a
kernel void elementwise_bias_kernel(uint i [[thread_position_in_grid]],
                                    constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                    device const float *a [[buffer(ElementwiseBuffer_A)]],
                                    device const float *b [[buffer(ElementwiseBuffer_B)]],
                                    device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    if (i < uniforms.count) out[i] = a[i] + b[i % uniforms.columns];
}

constant float elementwise_gelu_c = 0.7978845608028654; // sqrt(2 / pi)
constant float elementwise_gelu_k = 0.044715;

// out = gelu(a), the tanh form: a (1 + tanh(sqrt(2 / pi) (a + 0.044715 a^3))) / 2
kernel void elementwise_gelu_kernel(uint i [[thread_position_in_grid]],
                                    constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                    device const float *a [[buffer(ElementwiseBuffer_A)]],
                                    device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    if (i >= uniforms.count) return;
    float x = a[i];
    out[i] = 0.5 * x * (1.0 + elementwise_tanh(elementwise_gelu_c * (x + elementwise_gelu_k * x * x * x)));
}

// out = b gelu'(a): the gradient b of gelu's output carried back to its input a
kernel void elementwise_gelu_back_kernel(uint i [[thread_position_in_grid]],
                                         constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                         device const float *a [[buffer(ElementwiseBuffer_A)]],
                                         device const float *b [[buffer(ElementwiseBuffer_B)]],
                                         device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    if (i >= uniforms.count) return;
    float x = a[i];
    float t = elementwise_tanh(elementwise_gelu_c * (x + elementwise_gelu_k * x * x * x));
    float slope =
        0.5 * (1.0 + t) + 0.5 * x * (1.0 - t * t) * elementwise_gelu_c * (1.0 + 3.0 * elementwise_gelu_k * x * x);
    out[i] = b[i] * slope;
}

// out = alpha a + beta b: a copy, a difference, a blend, a scaled add
kernel void elementwise_axpby_kernel(uint i [[thread_position_in_grid]],
                                     constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                     device const float *a [[buffer(ElementwiseBuffer_A)]],
                                     device const float *b [[buffer(ElementwiseBuffer_B)]],
                                     device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    if (i < uniforms.count) out[i] = uniforms.alpha * a[i] + uniforms.beta * b[i];
}

// out = a b
kernel void elementwise_mul_kernel(uint i [[thread_position_in_grid]],
                                   constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                   device const float *a [[buffer(ElementwiseBuffer_A)]],
                                   device const float *b [[buffer(ElementwiseBuffer_B)]],
                                   device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    if (i < uniforms.count) out[i] = a[i] * b[i];
}

// One step of settling a hidden layer by gradient descent on the energy: out = a + gamma (-b + c d), where a is the
// value nodes x, b their error, c the slope of the activation at x and d the error of the layer above carried back
// through the weights.
kernel void elementwise_settle_kernel(uint i [[thread_position_in_grid]],
                                      constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                      device const float *a [[buffer(ElementwiseBuffer_A)]],
                                      device const float *b [[buffer(ElementwiseBuffer_B)]],
                                      device const float *c [[buffer(ElementwiseBuffer_C)]],
                                      device const float *d [[buffer(ElementwiseBuffer_D)]],
                                      device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    if (i < uniforms.count) out[i] = a[i] + uniforms.gamma * (c[i] * d[i] - b[i]);
}

// The column sum, in two passes, with no atomics so the order of every sum is fixed. a is count / columns rows by
// columns. First pass: a threadgroup is a tile of colsum_tile columns by colsum_lanes row lanes, and group y of the
// grid takes the rows y * lanes + lane, stepping by parts * lanes; a simdgroup reads one row of the tile, colsum_tile
// floats in a row, which is coalesced. The lanes are added in order through threadgroup memory and the group writes
// its partial sum of each column, times alpha, to partials[part * columns + column]; with one partition the host
// binds out there and alpha, and the second pass is skipped.
kernel void elementwise_colsum_kernel(uint2 local [[thread_position_in_threadgroup]],
                                      uint2 group [[threadgroup_position_in_grid]],
                                      uint2 groups [[threadgroups_per_grid]],
                                      constant ElementwiseColsumUniforms &uniforms
                                      [[buffer(ElementwiseBuffer_Uniforms)]],
                                      device const float *a [[buffer(ElementwiseBuffer_A)]],
                                      device float *partials [[buffer(ElementwiseBuffer_C)]]) {
    threadgroup float lanes[colsum_lanes][colsum_tile];
    uint column = group.x * colsum_tile + local.x;
    float sum = 0.0;
    if (column < uniforms.columns) {
        for (uint row = group.y * colsum_lanes + local.y; row < uniforms.rows; row += groups.y * colsum_lanes)
            sum += a[(ulong)row * uniforms.columns + column];
    }
    lanes[local.y][local.x] = sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (local.y != 0 || column >= uniforms.columns) return;
    float total = lanes[0][local.x];
    for (uint lane = 1; lane < colsum_lanes; lane++)
        total += lanes[lane][local.x];
    partials[(ulong)group.y * uniforms.columns + column] = uniforms.alpha * total;
}

// Second pass: out[column] = alpha times the partial sums of the column added in a fixed order. The same tile of
// colsum_tile columns by colsum_lanes lanes: lane l adds the partials l, l + lanes, ... and the lanes are added in
// order.
kernel void elementwise_colsum_finish_kernel(uint2 local [[thread_position_in_threadgroup]],
                                             uint2 group [[threadgroup_position_in_grid]],
                                             constant ElementwiseColsumUniforms &uniforms
                                             [[buffer(ElementwiseBuffer_Uniforms)]],
                                             device const float *partials [[buffer(ElementwiseBuffer_C)]],
                                             device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    threadgroup float lanes[colsum_lanes][colsum_tile];
    uint column = group.x * colsum_tile + local.x;
    float sum = 0.0;
    if (column < uniforms.columns) {
        for (uint part = local.y; part < uniforms.parts; part += colsum_lanes)
            sum += partials[(ulong)part * uniforms.columns + column];
    }
    lanes[local.y][local.x] = sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (local.y != 0 || column >= uniforms.columns) return;
    float total = lanes[0][local.x];
    for (uint lane = 1; lane < colsum_lanes; lane++)
        total += lanes[lane][local.x];
    out[column] = uniforms.alpha * total;
}

// The sum of squares, as reduce_sum_kernel sums: each threadgroup writes the sum of its share of a squared to
// out[group], and the host adds the partials.
kernel void elementwise_sq_kernel(uint index [[thread_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]],
                                  uint simd [[simdgroup_index_in_threadgroup]],
                                  uint simd_count [[simdgroups_per_threadgroup]],
                                  uint group [[threadgroup_position_in_grid]],
                                  uint group_count [[threadgroups_per_grid]],
                                  constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                  device const float *a [[buffer(ElementwiseBuffer_A)]],
                                  device float *out [[buffer(ElementwiseBuffer_Out)]]) {
    threadgroup float sums[elementwise_simds_max];
    float sum = 0.0;
    for (uint i = group * elementwise_threads + index; i < uniforms.count; i += group_count * elementwise_threads)
        sum += a[i] * a[i];
    sum = simd_sum(sum);
    if (lane == 0) sums[simd] = sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (simd != 0) return;
    float total = simd_sum(lane < simd_count ? sums[lane] : 0.0);
    if (lane == 0) out[group] = total;
}

// One Adam step on weights a with gradient b and moment estimates c (of the gradient) and d (of its square): alpha is
// the learning rate already corrected for the step's bias, beta and gamma the moments' decay rates, epsilon the floor.
kernel void elementwise_adam_kernel(uint i [[thread_position_in_grid]],
                                    constant ElementwiseUniforms &uniforms [[buffer(ElementwiseBuffer_Uniforms)]],
                                    device float *a [[buffer(ElementwiseBuffer_A)]],
                                    device const float *b [[buffer(ElementwiseBuffer_B)]],
                                    device float *c [[buffer(ElementwiseBuffer_C)]],
                                    device float *d [[buffer(ElementwiseBuffer_D)]]) {
    if (i >= uniforms.count) return;
    float gradient = b[i];
    float first = fma(uniforms.beta, c[i], (1.0 - uniforms.beta) * gradient);
    float second = fma(uniforms.gamma, d[i], (1.0 - uniforms.gamma) * (gradient * gradient));
    c[i] = first;
    d[i] = second;
    a[i] -= uniforms.alpha * first / (sqrt(second) + uniforms.epsilon);
}


constant uint reduce_threads = 256; // a threadgroup's threads, which the host dispatches
constant uint reduce_simds_max = 32;

// Sums the input: each thread adds the elements a whole grid apart, a simd group adds its threads' sums with simd_sum,
// and the first simd group adds the groups' sums. Thread 0 writes the threadgroup's sum.
kernel void reduce_sum_kernel(uint index [[thread_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]],
                              uint simd [[simdgroup_index_in_threadgroup]],
                              uint simd_count [[simdgroups_per_threadgroup]],
                              uint group [[threadgroup_position_in_grid]], uint group_count [[threadgroups_per_grid]],
                              constant ReduceUniforms &uniforms [[buffer(ReduceBuffer_Uniforms)]],
                              device const float *input [[buffer(ReduceBuffer_Input)]],
                              device float *partials [[buffer(ReduceBuffer_Partials)]]) {
    threadgroup float sums[reduce_simds_max];
    float sum = 0.0;
    for (uint i = group * reduce_threads + index; i < uniforms.count; i += group_count * reduce_threads)
        sum += input[i];
    sum = simd_sum(sum);
    if (lane == 0) sums[simd] = sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (simd != 0) return;
    float total = simd_sum(lane < simd_count ? sums[lane] : 0.0);
    if (lane == 0) partials[group] = total;
}
