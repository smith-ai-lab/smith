// The C reference of every Metal kernel: the plain CPU computation the kernel must match within a stated tolerance, and
// the bound on how far a float32 kernel may stray from it. They accumulate in double, so they are the nearly exact
// answer the GPU's float sums are judged against. Every kernel has one here and a parity test in tests/.
#ifndef BASE_REFERENCE_H
#define BASE_REFERENCE_H

#include <smith/arena.h>
#include <stdint.h>

constexpr double reference_unit_roundoff = 0x1p-24; // u for float32, round to nearest

// The sum of count values, in double. absolute, if not nullptr, gets the sum of their magnitudes, which scales the
// error.
[[nodiscard]] double reference_sum(const float *values, uint64_t count, double *absolute);

// The most a float32 sum of count values, in any order, may differ from the exact sum: count * u * (the sum of their
// magnitudes), twice over for a margin.
[[nodiscard]] double reference_sum_tolerance(uint64_t count, double absolute);

// C = A B for row-major matrices, A m by k, B k by n, C m by n, in double. bound, if not nullptr, gets for each element
// the sum of |A| |B| products, which scales the error.
void reference_matmul(const float *a, const float *b, double *c, double *bound, uint32_t m, uint32_t n, uint32_t k);

// The most a float32 matmul element may differ from the exact one, given its sum of magnitudes: k * u * bound, twice
// over.
[[nodiscard]] double reference_matmul_tolerance(uint32_t k, double bound);

// tanh in double, accurate to a few double roundings (the C library's tanhf is float only).
[[nodiscard]] double reference_tanh(double x);

// exp(-y) in double for y from 0 to 40, to a few roundings.
[[nodiscard]] double reference_exp_negative(double y);

// 1 / (1 + e^-x) in double, as accurate as reference_tanh.
[[nodiscard]] double reference_sigmoid(double x);

// One Adam step in double over count values: first = beta1 first + (1 - beta1) g, second = beta2 second + (1 - beta2)
// g^2, weights -= rate first / (sqrt(second) + epsilon). The sqrt is the exactly rounded one.
void reference_adam(const float *weights, const float *gradient, const float *first, const float *second,
                    double *weights_out, double *first_out, double *second_out, uint32_t count, double rate,
                    double beta1, double beta2, double epsilon);

// A tanh network: layer_count weight layers, widths sizes[0] (the input) to sizes[layer_count] (the linear output).
// Layer l holds a weight matrix of sizes[l + 1] rows by sizes[l] columns, row-major, and a bias of sizes[l + 1].
constexpr uint32_t reference_layers_max = 6;
typedef struct {
    uint32_t layer_count;
    uint32_t sizes[reference_layers_max + 1];
} ReferenceNet;

// The loss, the mean over the batch of half the squared distance of the output to the target, and its gradient with
// respect to every weight and bias, by backprop in double from float parameters. output, if not nullptr, gets the
// network's output (batch by sizes[layer_count]). arena holds the activations during the call.
void reference_mlp_gradient(const ReferenceNet *net, const float *const *weights, const float *const *biases,
                            const float *input, const float *target, uint32_t batch, double *const *weight_gradients,
                            double *const *bias_gradients, double *loss, double *output, SmithArena *arena);

#endif
