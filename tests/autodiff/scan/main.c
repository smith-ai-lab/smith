// Enzyme reverse mode of reference_scan_forward vs reference_scan_backward vs central finite differences.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <smith/autodiff.h>

#include "base/reference_scan.h"

// Fails (exit status 1) when Enzyme's gradient differs from the hand-written backward by more than this, relative to
// the largest gradient entry of the input.
constexpr double tolerance = 1e-12;
static double worst_relative;

static uint64_t rng = 88172645463325252ull;
static double urand(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (double)(rng >> 11) / 9007199254740992.0;
}
static double *mk(uint64_t n, double lo, double hi) {
    double *p = calloc(n ? n : 1, sizeof(double));
    for (uint64_t i = 0; i < n; i++) p[i] = lo + (hi - lo) * urand();
    return p;
}
static double *zeros(uint64_t n) { return calloc(n ? n : 1, sizeof(double)); }

typedef struct { ReferenceScanShape sh; ReferenceScanInputs in; double *state0, *dy, *dse; uint64_t rows, st; } Problem;

static double loss(Problem *p) {
    uint64_t hp = (uint64_t)p->sh.heads * p->sh.p;
    double *y = zeros(p->st * hp), *se = zeros(p->rows * p->sh.n);
    reference_scan_forward(&p->sh, &p->in, p->state0, y, se, nullptr);
    double l = 0;
    for (uint64_t i = 0; i < p->st * hp; i++) l += y[i] * p->dy[i];
    for (uint64_t i = 0; i < p->rows * p->sh.n; i++) l += se[i] * p->dse[i];
    free(y); free(se);
    return l;
}

static void cmp(const char *name, const double *a, const double *b, uint64_t n, double *maxabs_o, double *maxrel_o) {
    double maxabs = 0, scale = 0;
    for (uint64_t i = 0; i < n; i++) {
        maxabs = fmax(maxabs, fabs(a[i] - b[i]));
        scale = fmax(scale, fabs(b[i]));
    }
    printf("  %-8s n=%-8llu max|ref|=%.3e  maxabs=%.3e  maxrel(to max)=%.3e\n", name, (unsigned long long)n, scale, maxabs,
           scale > 0 ? maxabs / scale : maxabs);
    *maxabs_o = fmax(*maxabs_o, maxabs);
    *maxrel_o = fmax(*maxrel_o, scale > 0 ? maxabs / scale : maxabs);
}

static double fd(Problem *p, double *x, uint64_t i) {
    double h = 1e-6, o = x[i];
    x[i] = o + h; double lp = loss(p);
    x[i] = o - h; double lm = loss(p);
    x[i] = o;
    return (lp - lm) / (2 * h);
}

static void run(ReferenceScanShape sh, const char *label, int samples) {
    printf("== %s: S=%u T=%u H=%u P=%u N=%u\n", label, sh.sequences, sh.steps, sh.heads, sh.p, sh.n);
    Problem p = {.sh = sh};
    p.st = (uint64_t)sh.sequences * sh.steps;
    uint64_t hp = (uint64_t)sh.heads * sh.p;
    p.rows = (uint64_t)sh.sequences * hp;
    double *x = mk(p.st * hp, -1, 1), *dt = mk(p.st * sh.heads, 0.01, 0.5), *a = mk(sh.heads, -2, -0.1);
    double *b = mk(p.st * sh.n, -1, 1), *c = mk(p.st * sh.n, -1, 1), *keep = mk(p.st, 1, 1);
    for (uint64_t i = 0; i < p.st; i++) if (urand() < 0.1) keep[i] = 0.0;
    p.in = (ReferenceScanInputs){x, dt, a, b, c, keep};
    p.state0 = mk(p.rows * sh.n, -1, 1);
    p.dy = mk(p.st * hp, -1, 1);
    p.dse = mk(p.rows * sh.n, -1, 1);

    // hand-written backward
    double *states = zeros(reference_scan_states_count(&sh)), *y = zeros(p.st * hp), *se = zeros(p.rows * sh.n);
    reference_scan_forward(&sh, &p.in, p.state0, y, se, states);
    ReferenceScanGradients h = {zeros(p.st * hp), zeros(p.st * sh.heads), zeros(sh.heads), zeros(p.st * sh.n),
                                zeros(p.st * sh.n), zeros(p.rows * sh.n), nullptr};
    reference_scan_backward(&sh, &p.in, p.state0, states, p.dy, p.dse, &h);

    // Enzyme
    ReferenceScanGradients e = {zeros(p.st * hp), zeros(p.st * sh.heads), zeros(sh.heads), zeros(p.st * sh.n),
                                zeros(p.st * sh.n), zeros(p.rows * sh.n), nullptr};
    double *dkeep = zeros(p.st);
    ReferenceScanInputs din = {e.dx, e.ddt, e.da, e.db, e.dc, dkeep};
    double *ytmp = zeros(p.st * hp), *setmp = zeros(p.rows * sh.n);
    double *dy_seed = malloc(p.st * hp * sizeof(double)), *dse_seed = malloc(p.rows * sh.n * sizeof(double));
    memcpy(dy_seed, p.dy, p.st * hp * sizeof(double));
    memcpy(dse_seed, p.dse, p.rows * sh.n * sizeof(double));
    __enzyme_autodiff((void *)reference_scan_forward, enzyme_const, &sh, enzyme_dup, &p.in, &din, enzyme_dup,
                      p.state0, e.dstate0, enzyme_dup, ytmp, dy_seed, enzyme_dup, setmp, dse_seed, enzyme_const,
                      (double *)nullptr);

    double aa = 0, ar = 0;
    printf(" Enzyme vs hand-written backward:\n");
    cmp("dx", e.dx, h.dx, p.st * hp, &aa, &ar);
    cmp("ddt", e.ddt, h.ddt, p.st * sh.heads, &aa, &ar);
    cmp("da", e.da, h.da, sh.heads, &aa, &ar);
    cmp("db", e.db, h.db, p.st * sh.n, &aa, &ar);
    cmp("dc", e.dc, h.dc, p.st * sh.n, &aa, &ar);
    cmp("dstate0", e.dstate0, h.dstate0, p.rows * sh.n, &aa, &ar);
    printf("  >> worst maxabs=%.3e worst maxrel=%.3e\n", aa, ar);
    worst_relative = fmax(worst_relative, ar);

    // finite differences on a sample of indices of each input
    struct { const char *n; double *v, *ge, *gh; uint64_t len; } in[] = {
        {"dx", x, e.dx, h.dx, p.st * hp}, {"ddt", dt, e.ddt, h.ddt, p.st * sh.heads}, {"da", a, e.da, h.da, sh.heads},
        {"db", b, e.db, h.db, p.st * sh.n}, {"dc", c, e.dc, h.dc, p.st * sh.n},
        {"dstate0", p.state0, e.dstate0, h.dstate0, p.rows * sh.n}};
    printf(" central FD (h=1e-6, %d samples/input): max |FD-Enzyme|, max |FD-hand| relative to max|grad| of the input\n", samples);
    for (int k = 0; k < 6; k++) {
        double me = 0, mh = 0, sc = 0;
        for (int s = 0; s < samples; s++) {
            uint64_t i = in[k].len <= (uint64_t)samples ? (uint64_t)s % in[k].len : (uint64_t)(urand() * (double)in[k].len);
            double f = fd(&p, in[k].v, i);
            me = fmax(me, fabs(f - in[k].ge[i]));
            mh = fmax(mh, fabs(f - in[k].gh[i]));
            sc = fmax(sc, fabs(in[k].gh[i]));
        }
        printf("  %-8s enzyme %.3e  hand %.3e  (scale %.3e)\n", in[k].n, me / sc, mh / sc, sc);
    }
}

int main(void) {
    run((ReferenceScanShape){2, 3, 2, 3, 4}, "small", 400);
    run((ReferenceScanShape){64, 8, 4, 32, 16}, "learner (batch 64, window 8, 4 heads x 32, state 16)", 12);
    printf("worst relative difference %.3e (tolerance %.0e)\n", worst_relative, tolerance);
    return worst_relative <= tolerance ? 0 : 1;
}
