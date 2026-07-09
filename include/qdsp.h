/**
 * @file qdsp.h
 * @brief Branchless, fixed-point (Q15) DSP primitives for FPU-less MCUs.
 *
 * qdsp is a header-only C99 library of digital-signal-processing building
 * blocks that run on microcontrollers without a floating-point unit. Every
 * quantity is Q15 (int16, value = raw / 32768, range [-1, 1)). All routines
 * are `static inline`, have no global state, perform no dynamic allocation,
 * and are written to stay branch-light on ARMv7-M / RISC-V / Xtensa at -O2.
 *
 * It complements MPTC's sliding-DFT with the classic IIR/FIR primitives that
 * real audio and sensor pipelines need: biquad filters, Goertzel tone
 * detection, envelope followers and RMS metering.
 */
#ifndef QDSP_H
#define QDSP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Q15 core arithmetic                                               */
/* ================================================================== */

typedef int16_t q15_t;

#define Q15_ONE  ((q15_t)0x7FFF)   /**< +1.0 (max positive Q15) */
#define Q15_HALF ((q15_t)0x4000)   /**< +0.5                       */
#define Q15_ZERO ((q15_t)0x0000)   /**< 0.0                        */

/** Saturating cast int32 -> q15. */
static inline q15_t q15_sat(int32_t x) {
    if (x > 0x7FFF) return (q15_t)0x7FFF;
    if (x < -0x8000) return (q15_t)(-0x8000);
    return (q15_t)x;
}

/** Q15 multiply: returns sat(a * b). */
static inline q15_t q15_mul(q15_t a, q15_t b) {
    return q15_sat(((int32_t)a * (int32_t)b) >> 15);
}

/** Q15 add: returns sat(a + b). */
static inline q15_t q15_add(q15_t a, q15_t b) {
    return q15_sat((int32_t)a + (int32_t)b);
}

/** Q15 subtract: returns sat(a - b). */
static inline q15_t q15_sub(q15_t a, q15_t b) {
    return q15_sat((int32_t)a - (int32_t)b);
}

/** Absolute value (branchless via arithmetic negation mask). */
static inline q15_t q15_abs(q15_t a) {
    /* mask = 0xFFFF if a<0 else 0x0000; result = (a ^ mask) - mask */
    uint16_t mask = (uint16_t)(-(a < 0));
    return (q15_t)(((uint16_t)a ^ mask) - mask);
}

/* ================================================================== */
/*  Biquad (Direct Form II Transposed)                                */
/* ================================================================== */

/**
 * @brief Second-order IIR section.
 *
 * Coefficients b0,b1,b2,a1,a2 are in Q15. Use tools/gen_biquad.py to emit
 * them from f0 / Q / fs using the RBJ cookbook. a1, a2 are the normalised
 * denominator coefficients (typically negative).
 */
typedef struct {
    q15_t b0, b1, b2, a1, a2;
    q15_t w1, w2;          /**< filter state (Q15) */
} qdsp_biquad_t;

static inline void qdsp_biquad_reset(qdsp_biquad_t *f) {
    f->w1 = Q15_ZERO;
    f->w2 = Q15_ZERO;
}

/** Process one Q15 sample through the biquad. */
static inline q15_t qdsp_biquad_process(qdsp_biquad_t *f, q15_t x) {
    int32_t w = (int32_t)x
              - q15_mul(f->a1, f->w1)
              - q15_mul(f->a2, f->w2);
    q15_t y = q15_sat(
        q15_mul(f->b0, (q15_t)w) +
        q15_mul(f->b1, f->w1) +
        q15_mul(f->b2, f->w2));
    f->w2 = f->w1;
    f->w1 = (q15_t)w;
    return y;
}

/* ================================================================== */
/*  First-order one-pole low/high pass                                */
/* ================================================================== */

typedef struct {
    q15_t coef;     /**< smoothing factor in Q15 (0..1) */
    q15_t state;    /**< previous output (Q15) */
} qdsp_onepole_t;

static inline q15_t qdsp_onepole_process(qdsp_onepole_t *f, q15_t x) {
    /* y = state + coef * (x - state) */
    q15_t diff = q15_sub(x, f->state);
    f->state = q15_sat(f->state + q15_mul(f->coef, diff));
    return f->state;
}

/* ================================================================== */
/*  Goertzel (single-frequency detection)                             */
/* ================================================================== */

/**
 * @brief Goertzel algorithm for detecting a single frequency bin.
 *
 * Call qdsp_goertzel_process() for each sample of a block of length N,
 * then qdsp_goertzel_power() to read the squared magnitude. coeff is
 * 2*cos(2*pi*k/N) in Q15 (use tools/gen_goertzel.py).
 */
typedef struct {
    int32_t coeff;     /**< 2*cos(2*pi*k/N) in Q15 (int32_t to accommodate |coeff| <= 2.0) */
    q15_t s_prev;      /**< s[n-1] */
    q15_t s_prev2;     /**< s[n-2] */
    int   n;           /**< samples processed in this block */
    int   N;           /**< block length */
} qdsp_goertzel_t;

static inline void qdsp_goertzel_reset(qdsp_goertzel_t *g) {
    g->s_prev = Q15_ZERO;
    g->s_prev2 = Q15_ZERO;
    g->n = 0;
}

static inline void qdsp_goertzel_process(qdsp_goertzel_t *g, q15_t x) {
    /* s = x + coeff*s_prev - s_prev2  (coeff is Q15 in int32_t) */
    int32_t s = (int32_t)x
              + ((g->coeff * (int32_t)g->s_prev) >> 15)
              - (int32_t)g->s_prev2;
    g->s_prev2 = g->s_prev;
    g->s_prev  = q15_sat(s);
    g->n++;
}

/** Squared magnitude of the detected bin (Q30-ish, clamped to Q15 range). */
static inline q15_t qdsp_goertzel_power(qdsp_goertzel_t *g) {
    int32_t sp  = g->s_prev;
    int32_t sp2 = g->s_prev2;
    int32_t term = (g->coeff * (int32_t)(sp - sp2)) >> 15;
    int32_t mag  = sp * sp + sp2 * sp2 - term * (sp - sp2);
    /* scale down from ~Q30 to Q15 */
    return q15_sat(mag >> 15);
}

/* ================================================================== */
/*  Envelope follower (attacks faster than it releases)               */
/* ================================================================== */

typedef struct {
    q15_t attack;    /**< attack smoothing in Q15 (0..1) */
    q15_t release;   /**< release smoothing in Q15 (0..1) */
    q15_t env;       /**< current envelope (Q15) */
} qdsp_envelope_t;

static inline q15_t qdsp_envelope_process(qdsp_envelope_t *e, q15_t x) {
    q15_t ax = q15_abs(x);
    q15_t coef = (ax > e->env) ? e->attack : e->release;
    q15_t diff = q15_sub(ax, e->env);
    e->env = q15_sat(e->env + q15_mul(coef, diff));
    return e->env;
}

/* ================================================================== */
/*  FIR filter (convolution with symmetric tap vector)                */
/* ================================================================== */

typedef struct {
    const q15_t *taps;   /**< coefficient vector, length n */
    q15_t       *buf;    /**< circular delay line, length n (caller-owned) */
    int          n;
    int          idx;
} qdsp_fir_t;

static inline q15_t qdsp_fir_process(qdsp_fir_t *f, q15_t x) {
    f->buf[f->idx] = x;
    int32_t acc = 0;
    int k = f->idx;
    for (int i = 0; i < f->n; i++) {
        acc += (int32_t)f->taps[i] * (int32_t)f->buf[k];
        k = (k == 0) ? (f->n - 1) : (k - 1);
    }
    f->idx = (f->idx + 1) % f->n;
    return q15_sat(acc >> 15);
}

/* ================================================================== */
/*  Metering: peak + RMS over a block                                 */
/* ================================================================== */

/** Update a running peak (returns new peak). */
static inline q15_t qdsp_peak_update(q15_t peak, q15_t x) {
    q15_t ax = q15_abs(x);
    return (ax > peak) ? ax : peak;
}

/** Single-sample RMS accumulation step; call for every sample of a block,
 *  then call qdsp_rms_final() with the sample count. */
static inline int32_t qdsp_rms_acc(int32_t acc, q15_t x) {
    return acc + (int32_t)x * (int32_t)x;   /* sum of squares (Q30) */
}

/** Finalise RMS from accumulated sum-of-squares and N (returns Q15). */
static inline q15_t qdsp_rms_final(int32_t acc, int n) {
    /* crude integer sqrt via bit-by-bit; input is sum of x^2 in Q30 */
    uint32_t v = (uint32_t)(acc / n);       /* mean square, ~Q30 */
    uint32_t root = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        uint32_t trial = root + bit;
        if (v >= trial) { v -= trial; root = (root >> 1) + bit; }
        else            { root >>= 1; }
        bit >>= 2;
    }
    /* root is sqrt(mean_square) in ~Q15 */
    return q15_sat((int32_t)root);
}

#ifdef __cplusplus
}
#endif

#endif /* QDSP_H */
