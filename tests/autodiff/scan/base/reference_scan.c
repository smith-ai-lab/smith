#include "base/reference_scan.h"

#include <assert.h>
#include <stdint.h>

#include "base/reference.h"

constexpr double reference_scan_unit = 5.9604644775390625e-8; // 2^-24

// exp(x) for x at most 0; below -40 it is under 5e-18.
static double reference_scan_exp(double x) {
    return x < -40.0 ? 0.0 : reference_exp_negative(-x);
}

static void reference_scan_check(const ReferenceScanShape *shape) {
    assert(shape != nullptr);
    assert(shape->sequences > 0 && shape->steps > 0 && shape->heads > 0 && shape->p > 0 && shape->n > 0);
}

uint64_t reference_scan_states_count(const ReferenceScanShape *shape) {
    reference_scan_check(shape);
    return (uint64_t)shape->sequences * shape->steps * shape->heads * shape->p * shape->n;
}

void reference_scan_forward(const ReferenceScanShape *shape, const ReferenceScanInputs *inputs, const double *state0,
                            double *y, double *state_end, double *states) {
    reference_scan_check(shape);
    uint64_t rows = (uint64_t)shape->sequences * shape->heads * shape->p; // (s, h, p) rows of n states
    for (uint64_t row = 0; row < rows; row++) {
        uint64_t s = row / ((uint64_t)shape->heads * shape->p);
        uint64_t hp = row % ((uint64_t)shape->heads * shape->p);
        uint64_t h = hp / shape->p;
        double state[64];
        for (uint32_t i = 0; i < shape->n; i++)
            state[i] = state0[row * shape->n + i];
        for (uint32_t t = 0; t < shape->steps; t++) {
            uint64_t st = s * shape->steps + t;
            double decay = inputs->keep[st] * reference_scan_exp(inputs->dt[st * shape->heads + h] * inputs->a[h]);
            double dtx = inputs->dt[st * shape->heads + h] * inputs->x[st * shape->heads * shape->p + hp];
            double out = 0.0;
            for (uint32_t i = 0; i < shape->n; i++) {
                state[i] = decay * state[i] + dtx * inputs->b[st * shape->n + i];
                out += state[i] * inputs->c[st * shape->n + i];
                if (states != nullptr) states[(st * shape->heads * shape->p + hp) * shape->n + i] = state[i];
            }
            y[st * shape->heads * shape->p + hp] = out;
        }
        for (uint32_t i = 0; i < shape->n; i++)
            state_end[row * shape->n + i] = state[i];
    }
}

static void reference_scan_zero(const ReferenceScanShape *shape, const ReferenceScanGradients *g) {
    uint64_t st = (uint64_t)shape->sequences * shape->steps;
    for (uint64_t i = 0; i < st * shape->heads * shape->p; i++)
        g->dx[i] = 0.0;
    for (uint64_t i = 0; i < st * shape->heads; i++)
        g->ddt[i] = 0.0;
    for (uint32_t i = 0; i < shape->heads; i++)
        g->da[i] = 0.0;
    if (g->da_abs != nullptr)
        for (uint64_t i = 0; i < shape->heads; i++)
            g->da_abs[i] = 0.0;
    for (uint64_t i = 0; i < st * shape->n; i++) {
        g->db[i] = 0.0;
        g->dc[i] = 0.0;
    }
}

// The gradient through step t of one row: gradient (of S_t, with dy C added), the state before and after.
static void reference_scan_step_back(const ReferenceScanShape *shape, const ReferenceScanInputs *inputs,
                                     const ReferenceScanGradients *grads, const double *before, const double *after,
                                     double *gradient, double dy, uint64_t st, uint64_t h, uint64_t hp) {
    double dt = inputs->dt[st * shape->heads + h];
    double x = inputs->x[st * shape->heads * shape->p + hp];
    double a = reference_scan_exp(dt * inputs->a[h]);
    double keep = inputs->keep[st];
    double dx = 0.0;
    double d_decay = 0.0; // the gradient of keep a: the sum of gradient * S_{t-1}
    for (uint32_t i = 0; i < shape->n; i++) {
        gradient[i] += dy * inputs->c[st * shape->n + i];
        grads->dc[st * shape->n + i] += dy * after[i];
        dx += gradient[i] * inputs->b[st * shape->n + i];
        grads->db[st * shape->n + i] += gradient[i] * dt * x;
        d_decay += gradient[i] * before[i];
    }
    grads->dx[st * shape->heads * shape->p + hp] = dt * dx;
    grads->ddt[st * shape->heads + h] += x * dx + keep * a * inputs->a[h] * d_decay;
    double da_term = keep * a * dt * d_decay;
    grads->da[h] += da_term;
    if (grads->da_abs != nullptr) grads->da_abs[h] += __builtin_fabs(da_term);
    for (uint32_t i = 0; i < shape->n; i++)
        gradient[i] *= keep * a;
}

void reference_scan_backward(const ReferenceScanShape *shape, const ReferenceScanInputs *inputs, const double *state0,
                             const double *states, const double *dy, const double *dstate_end,
                             const ReferenceScanGradients *gradients) {
    reference_scan_check(shape);
    reference_scan_zero(shape, gradients);
    uint64_t hp_count = (uint64_t)shape->heads * shape->p;
    for (uint64_t row = 0; row < (uint64_t)shape->sequences * hp_count; row++) {
        uint64_t s = row / hp_count;
        uint64_t hp = row % hp_count;
        double gradient[64];
        for (uint32_t i = 0; i < shape->n; i++)
            gradient[i] = dstate_end != nullptr ? dstate_end[row * shape->n + i] : 0.0;
        for (uint32_t t = shape->steps; t-- > 0;) {
            uint64_t st = s * shape->steps + t;
            const double *after = states + (st * hp_count + hp) * shape->n;
            const double *before = t > 0 ? after - hp_count * shape->n : state0 + row * shape->n;
            reference_scan_step_back(shape, inputs, gradients, before, after, gradient, dy[st * hp_count + hp], st,
                                     hp / shape->p, hp);
        }
        for (uint32_t i = 0; i < shape->n; i++)
            gradients->dstate0[row * shape->n + i] = gradient[i];
    }
}

double reference_scan_tolerance(const ReferenceScanShape *shape) {
    reference_scan_check(shape);
    double terms = (double)shape->steps + (double)shape->n + (double)shape->sequences +
                   2.0 * (double)shape->heads * (double)shape->p;
    return 4.0 * reference_scan_unit * terms;
}

// dA of a head is the sum over sequences (S), places (the shuffle tree over p) and steps (T) of keep a dt (g . state),
// and each term's relative error counts the operations it passes through in the kernel:
//   - the state before the step, by the forward recurrence fma(decay, state, dt x B): 2 roundings a step, 2T;
//   - the gradient g, by the backward recurrence fma(dy, C, g) then g * decay: 2 roundings a step, 2T;
//   - the dot of g with the state over n: n roundings;
//   - the products decay * dt * (dot), and the exp inside decay: 3;
//   - the lane's chain da = fma(decay dt, dot, da) over the T steps: T;
//   - the shuffle tree over the p places: ceil(log2 p) levels;
//   - the sum over the S sequences: S.
// So k = 4T + n + 3 + T + ceil(log2 p) + S, and the tolerance is twice k u sum(abs terms).
double reference_scan_da_tolerance(const ReferenceScanShape *shape, double abs_terms) {
    reference_scan_check(shape);
    uint32_t levels = 0;
    while ((1u << levels) < shape->p)
        levels++;
    double k = 4.0 * shape->steps + shape->n + 3.0 + shape->steps + levels + shape->sequences;
    return 2.0 * k * reference_scan_unit * abs_terms;
}
