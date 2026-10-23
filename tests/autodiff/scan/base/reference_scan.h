// The C reference of the selective scan (kernels/scan.metal and ops/ops_scan.h) in double on double inputs: the
// recurrence
//
//     a_t = exp(dt_t A_h),   S_t = keep_t a_t S_{t-1} + dt_t x_t B_t^T,   y_t = S_t C_t,
//
// forward, with all the states kept if asked, and the backward from all the stored states (the GPU keeps a checkpoint
// every few steps and recomputes the rest; this one stores every state, so the two are different computations of the
// same gradient). Layouts are the ops': x and y [S, T, H, P], dt [S, T, H], A [H], B and C [S, T, N], keep [S, T],
// states [S, H, P, N], all states [S, T, H, P, N] (the state after each step). The backward is also checked against
// central finite differences of this forward in tests/test_scan.c.
#ifndef BASE_REFERENCE_SCAN_H
#define BASE_REFERENCE_SCAN_H

#include <stdint.h>

typedef struct {
    uint32_t sequences;
    uint32_t steps;
    uint32_t heads;
    uint32_t p;
    uint32_t n;
} ReferenceScanShape;

typedef struct {
    const double *x;
    const double *dt;
    const double *a;
    const double *b;
    const double *c;
    const double *keep;
} ReferenceScanInputs;

typedef struct {
    double *dx;
    double *ddt;
    double *da;
    double *db;
    double *dc;
    double *dstate0;
    double *da_abs; // [heads], or nullptr: the sum of the absolute values of the terms added into each head of da
} ReferenceScanGradients;

// Floats of the all-states array.
[[nodiscard]] uint64_t reference_scan_states_count(const ReferenceScanShape *shape);

// y and the final state from state0; states (nullptr for none) gets the state after every step.
void reference_scan_forward(const ReferenceScanShape *shape, const ReferenceScanInputs *inputs, const double *state0,
                            double *y, double *state_end, double *states);

// The gradients of every input and of state0 from dy and dstate_end (nullptr for zero), given the states the forward
// kept. Every gradient is replaced.
void reference_scan_backward(const ReferenceScanShape *shape, const ReferenceScanInputs *inputs, const double *state0,
                             const double *states, const double *dy, const double *dstate_end,
                             const ReferenceScanGradients *gradients);

// The relative error a float32 scan may have, against the largest magnitude of the output: each value is a sum of
// about steps + n + sequences + 2 heads p terms (the recurrence, the dot with C, the sums over P and heads), each
// rounding with the unit roundoff 2^-24, with a factor of 4 for the GPU's exp and the order of the sums.
[[nodiscard]] double reference_scan_tolerance(const ReferenceScanShape *shape);

// The error dA may have, for the head whose terms sum in absolute value to abs_terms (da_abs of the backward): dA is a
// sum over sequences, steps and places of keep a dt (gradient . state), terms of both signs that can cancel by a
// thousand times, so its bound is relative to the sum of the terms, not to the largest output. Twice k u abs_terms,
// with k the operations that sum's value passes through in the kernel (see reference_scan.c).
[[nodiscard]] double reference_scan_da_tolerance(const ReferenceScanShape *shape, double abs_terms);

#endif
