#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "base/reference_scan.h"
extern int enzyme_dup, enzyme_const;
extern void __enzyme_autodiff(void *, ...);
static uint64_t rng = 88172645463325252ull;
static double urand(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (double)(rng >> 11) / 9007199254740992.0; }
static double *mk(uint64_t n, double lo, double hi) { double *p = calloc(n ? n : 1, 8); for (uint64_t i = 0; i < n; i++) p[i] = lo + (hi - lo) * urand(); return p; }
static double *zeros(uint64_t n) { return calloc(n ? n : 1, 8); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }
static int cmpd(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return x < y ? -1 : x > y; }
static double med(double *v, int n) { qsort(v, n, sizeof(double), cmpd); return v[n / 2]; }
int main(void) {
    ReferenceScanShape sh = {64, 8, 4, 32, 16};
    uint64_t st = (uint64_t)sh.sequences * sh.steps, hp = (uint64_t)sh.heads * sh.p, rows = sh.sequences * hp;
    double *x = mk(st * hp, -1, 1), *dt = mk(st * sh.heads, .01, .5), *a = mk(sh.heads, -2, -.1), *b = mk(st * sh.n, -1, 1),
           *c = mk(st * sh.n, -1, 1), *keep = mk(st, 1, 1), *s0 = mk(rows * sh.n, -1, 1), *dy = mk(st * hp, -1, 1), *dse = mk(rows * sh.n, -1, 1);
    ReferenceScanInputs in = {x, dt, a, b, c, keep};
    double *states = zeros(reference_scan_states_count(&sh)), *y = zeros(st * hp), *se = zeros(rows * sh.n);
    ReferenceScanGradients h = {zeros(st * hp), zeros(st * sh.heads), zeros(sh.heads), zeros(st * sh.n), zeros(st * sh.n), zeros(rows * sh.n), 0};
    ReferenceScanGradients e = {zeros(st * hp), zeros(st * sh.heads), zeros(sh.heads), zeros(st * sh.n), zeros(st * sh.n), zeros(rows * sh.n), 0};
    double *dkeep = zeros(st);
    ReferenceScanInputs din = {e.dx, e.ddt, e.da, e.db, e.dc, dkeep};
    double *dyseed = zeros(st * hp), *dseseed = zeros(rows * sh.n), *yt = zeros(st * hp), *st_ = zeros(rows * sh.n);
    enum { N = 300 };
    static double tf[N], tfs[N], tb[N], tfb[N], te[N];
    for (int r = 0; r < N + 20; r++) {
        int k = r - 20;
        double t0 = now();
        reference_scan_forward(&sh, &in, s0, y, se, 0);
        double t1 = now();
        reference_scan_forward(&sh, &in, s0, y, se, states);
        double t2 = now();
        reference_scan_backward(&sh, &in, s0, states, dy, dse, &h);
        double t3 = now();
        memcpy(dyseed, dy, st * hp * 8); memcpy(dseseed, dse, rows * sh.n * 8);
        memset(e.dx, 0, st*hp*8); memset(e.ddt,0,st*sh.heads*8); memset(e.da,0,sh.heads*8); memset(e.db,0,st*sh.n*8); memset(e.dc,0,st*sh.n*8); memset(e.dstate0,0,rows*sh.n*8); memset(dkeep,0,st*8);
        double t4 = now();
        __enzyme_autodiff((void *)reference_scan_forward, enzyme_const, &sh, enzyme_dup, &in, &din, enzyme_dup, s0, e.dstate0, enzyme_dup, yt, dyseed, enzyme_dup, st_, dseseed, enzyme_const, (double *)0);
        double t5 = now();
        if (k >= 0) { tf[k] = t1 - t0; tfs[k] = t2 - t1; tb[k] = t3 - t2; tfb[k] = t3 - t1; te[k] = t5 - t4; }
    }
    printf("learner shape S=64 T=8 H=4 P=32 N=16, %d reps, median ms:\n", N);
    printf("  forward (no states)            %8.3f\n  forward (with states)          %8.3f\n  hand backward only             %8.3f\n  hand fwd(states)+backward      %8.3f\n  Enzyme autodiff (fwd+tape+rev) %8.3f\n", med(tf, N), med(tfs, N), med(tb, N), med(tfb, N), med(te, N));
    printf("  ratio Enzyme / (hand fwd+bwd) = %.2f ; Enzyme / hand backward only = %.2f ; Enzyme / forward = %.2f\n", med(te, N) / med(tfb, N), med(te, N) / med(tb, N), med(te, N) / med(tf, N));
    return 0;
}
