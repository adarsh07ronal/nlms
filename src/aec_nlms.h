#pragma once

/**
 * aec_nlms.h — Acoustic Echo Canceller using NLMS adaptive filter
 *
 * Algorithm:  Normalized Least Mean Squares (NLMS)
 * Purpose:    Estimates and cancels acoustic echo from a microphone signal
 *             using the far-end reference signal.
 *
 * Signal model:
 *   d(n) = s(n) + echo(n)          mic picks up voice + echo
 *   echo(n) = x(n) * H             room convolves far-end with impulse response H
 *   y(n) = x(n) * W(n)             our estimated echo using adaptive filter W
 *   e(n) = d(n) - y(n)             clean output (near-end voice only)
 *
 * NLMS update rule:
 *   W(n+1) = W(n) + (mu / (||X||^2 + delta)) * e(n) * X(n)
 */

#include <stdint.h>

/* ── tuneable parameters ─────────────────────────────────────────── */

/** Filter length in taps.
 *  At 16 kHz, 1 tap = 62.5 µs.
 *  256 taps ≈ 16 ms  — covers direct path + first reflection (close-talk)
 *  512 taps ≈ 32 ms  — small room
 * 1024 taps ≈ 64 ms  — conference room
 */
#define AEC_FILTER_LEN     256

/** NLMS step size µ.  Range: (0, 2).
 *  Higher → faster convergence but risk of instability.
 *  Lower  → slower but smoother.
 *  0.3–0.5 is safe for most voice signals.
 */
#define AEC_STEP_SIZE      0.3f

/** Regularisation constant δ — prevents divide-by-zero during silence. */
#define AEC_DELTA          1e-6f

/** Exponential smoothing factor for power estimate.
 *  0.9 ≈ time constant of ~10 samples at 16 kHz.
 */
#define AEC_POWER_ALPHA    0.9f

/** Double-talk threshold.
 *  If near-end power > this × far-end power, freeze weight update.
 *  Tune higher (e.g. 4.0) to be less aggressive about freezing.
 */
#define AEC_DT_THRESHOLD   2.0f

/* ── state struct ────────────────────────────────────────────────── */

typedef struct {
    float w[AEC_FILTER_LEN];       /**< adaptive filter weights (the echo model) */
    float x_buf[AEC_FILTER_LEN];   /**< circular buffer of far-end reference samples */
    int   buf_idx;                 /**< write pointer into x_buf                    */
    float ref_power;               /**< running power estimate of x(n)              */
    int   double_talk;             /**< 1 when double-talk detected, 0 otherwise     */
} AecState;

/* ── public API ──────────────────────────────────────────────────── */

/**
 * aec_init — reset state to zero (call once before processing)
 */
void  aec_init(AecState *st);

/**
 * aec_process — process one sample
 *
 * @param st   pointer to AecState (must be initialised with aec_init)
 * @param x    far-end reference sample  (what the loudspeaker plays)
 * @param d    microphone input sample   (voice + echo)
 * @return     e(n) — echo-cancelled output sample
 */
float aec_process(AecState *st, float x, float d);

/**
 * aec_get_erle_db — measure Echo Return Loss Enhancement over a buffer
 *
 * @param echo_in   array of echo samples  before cancellation
 * @param echo_out  array of error samples after  cancellation
 * @param len       number of samples
 * @return ERLE in dB (higher = better cancellation)
 */
float aec_get_erle_db(const float *echo_in, const float *echo_out, int len);
