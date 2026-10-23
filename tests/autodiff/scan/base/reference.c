#include "base/reference.h"

#include <assert.h>
#include <stdint.h>

double reference_sum(const float *values, uint64_t count, double *absolute) {
    assert(values != nullptr || count == 0);
    double sum = 0.0;
    double magnitude = 0.0;
    for (uint64_t i = 0; i < count; i++) {
        sum += (double)values[i];
        magnitude += __builtin_fabs((double)values[i]);
    }
    if (absolute != nullptr) *absolute = magnitude;
    return sum;
}

double reference_sum_tolerance(uint64_t count, double absolute) {
    assert(absolute >= 0.0);
    return 2.0 * (double)count * reference_unit_roundoff * absolute;
}

void reference_matmul(const float *a, const float *b, double *c, double *bound, uint32_t m, uint32_t n, uint32_t k) {
    assert(a != nullptr && b != nullptr && c != nullptr);
    for (uint32_t row = 0; row < m; row++) {
        for (uint32_t column = 0; column < n; column++) {
            double sum = 0.0;
            double magnitude = 0.0;
            for (uint32_t t = 0; t < k; t++) {
                double product = (double)a[(uint64_t)row * k + t] * (double)b[(uint64_t)t * n + column];
                sum += product;
                magnitude += __builtin_fabs(product);
            }
            c[(uint64_t)row * n + column] = sum;
            if (bound != nullptr) bound[(uint64_t)row * n + column] = magnitude;
        }
    }
}

double reference_matmul_tolerance(uint32_t k, double bound) {
    assert(bound >= 0.0);
    return 2.0 * (double)k * reference_unit_roundoff * bound;
}

// exp(-y) for y from 0 to 40, as the Taylor series of exp(-y / 256) squared 8 times.
double reference_exp_negative(double y) {
    constexpr uint32_t halvings = 8;
    constexpr uint32_t terms = 16;
    double argument = y / 256.0;
    double term = 1.0;
    double sum = 1.0;
    for (uint32_t n = 1; n <= terms; n++) {
        term *= -argument / (double)n;
        sum += term;
    }
    for (uint32_t i = 0; i < halvings; i++)
        sum *= sum;
    return sum;
}

double reference_tanh(double x) {
    double magnitude = __builtin_fabs(x);
    if (magnitude > 20.0) return x > 0.0 ? 1.0 : -1.0;
    if (magnitude < 1e-3) {
        double square = x * x;
        return x * (1.0 - square / 3.0 + 2.0 * square * square / 15.0);
    }
    double e = reference_exp_negative(2.0 * magnitude);
    double t = (1.0 - e) / (1.0 + e);
    return x < 0.0 ? -t : t;
}

double reference_sigmoid(double x) {
    double magnitude = __builtin_fabs(x);
    if (magnitude > 40.0) return x > 0.0 ? 1.0 : 0.0;
    double e = reference_exp_negative(magnitude); // e^-|x|
    return x >= 0.0 ? 1.0 / (1.0 + e) : e / (1.0 + e);
}

void reference_adam(const float *weights, const float *gradient, const float *first, const float *second,
                    double *weights_out, double *first_out, double *second_out, uint32_t count, double rate,
                    double beta1, double beta2, double epsilon) {
    assert(weights != nullptr && gradient != nullptr && first != nullptr && second != nullptr);
    for (uint32_t i = 0; i < count; i++) {
        double g = (double)gradient[i];
        double m = beta1 * (double)first[i] + (1.0 - beta1) * g;
        double v = beta2 * (double)second[i] + (1.0 - beta2) * g * g;
        first_out[i] = m;
        second_out[i] = v;
        weights_out[i] = (double)weights[i] - rate * m / (__builtin_sqrt(v) + epsilon);
    }
}

// The activations and the loss gradients at every layer, in double, for a batch.
typedef struct {
    double *act[reference_layers_max + 1];
    double *delta[reference_layers_max + 1];
} ReferenceTape;

static void reference_mlp_forward(const ReferenceNet *net, const float *const *weights, const float *const *biases,
                                  uint32_t batch, ReferenceTape *tape) {
    for (uint32_t l = 0; l < net->layer_count; l++) {
        uint32_t in = net->sizes[l];
        uint32_t out = net->sizes[l + 1];
        bool last = l + 1 == net->layer_count;
        for (uint32_t s = 0; s < batch; s++) {
            for (uint32_t o = 0; o < out; o++) {
                double sum = (double)biases[l][o];
                for (uint32_t i = 0; i < in; i++)
                    sum += (double)weights[l][(uint64_t)o * in + i] * tape->act[l][(uint64_t)s * in + i];
                tape->act[l + 1][(uint64_t)s * out + o] = last ? sum : reference_tanh(sum);
            }
        }
    }
}

// The gradient of layer l's weights and biases from the loss gradient delta[l + 1], and delta[l] from it.
static void reference_mlp_layer_backward(const ReferenceNet *net, const float *const *weights, uint32_t batch,
                                         const ReferenceTape *tape, uint32_t l, double *weight_gradient,
                                         double *bias_gradient) {
    uint32_t in = net->sizes[l];
    uint32_t out = net->sizes[l + 1];
    for (uint32_t o = 0; o < out; o++) {
        double bias_sum = 0.0;
        for (uint32_t s = 0; s < batch; s++)
            bias_sum += tape->delta[l + 1][(uint64_t)s * out + o];
        bias_gradient[o] = bias_sum;
        for (uint32_t i = 0; i < in; i++) {
            double sum = 0.0;
            for (uint32_t s = 0; s < batch; s++)
                sum += tape->delta[l + 1][(uint64_t)s * out + o] * tape->act[l][(uint64_t)s * in + i];
            weight_gradient[(uint64_t)o * in + i] = sum;
        }
    }
    if (l == 0) return;
    for (uint32_t s = 0; s < batch; s++) {
        for (uint32_t i = 0; i < in; i++) {
            double sum = 0.0;
            for (uint32_t o = 0; o < out; o++)
                sum += tape->delta[l + 1][(uint64_t)s * out + o] * (double)weights[l][(uint64_t)o * in + i];
            double a = tape->act[l][(uint64_t)s * in + i];
            tape->delta[l][(uint64_t)s * in + i] = sum * (1.0 - a * a);
        }
    }
}

void reference_mlp_gradient(const ReferenceNet *net, const float *const *weights, const float *const *biases,
                            const float *input, const float *target, uint32_t batch, double *const *weight_gradients,
                            double *const *bias_gradients, double *loss, double *output, SmithArena *arena) {
    assert(net != nullptr && net->layer_count >= 1 && net->layer_count <= reference_layers_max);
    assert(input != nullptr && target != nullptr && batch > 0 && arena != nullptr);
    SmithArenaScope scope = smith_arena_scope_begin(arena);
    ReferenceTape tape = {};
    for (uint32_t l = 0; l <= net->layer_count; l++) {
        tape.act[l] = smith_arena_push_array(arena, sizeof(double), (uint64_t)batch * net->sizes[l], alignof(double));
        tape.delta[l] = smith_arena_push_array(arena, sizeof(double), (uint64_t)batch * net->sizes[l], alignof(double));
    }
    for (uint64_t i = 0; i < (uint64_t)batch * net->sizes[0]; i++)
        tape.act[0][i] = (double)input[i];
    reference_mlp_forward(net, weights, biases, batch, &tape);
    uint32_t last = net->layer_count;
    double total = 0.0;
    for (uint64_t i = 0; i < (uint64_t)batch * net->sizes[last]; i++) {
        double difference = tape.act[last][i] - (double)target[i];
        total += 0.5 * difference * difference;
        tape.delta[last][i] = difference;
        if (output != nullptr) output[i] = tape.act[last][i];
    }
    if (loss != nullptr) *loss = total / (double)batch;
    for (uint32_t l = net->layer_count; l-- > 0;)
        reference_mlp_layer_backward(net, weights, batch, &tape, l, weight_gradients[l], bias_gradients[l]);
    for (uint32_t l = 0; l < net->layer_count; l++) {
        for (uint64_t i = 0; i < (uint64_t)net->sizes[l] * net->sizes[l + 1]; i++)
            weight_gradients[l][i] /= (double)batch;
        for (uint32_t o = 0; o < net->sizes[l + 1]; o++)
            bias_gradients[l][o] /= (double)batch;
    }
    smith_arena_scope_end(scope);
}
