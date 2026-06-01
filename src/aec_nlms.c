/**
 * aec_nlms.c — NLMS Acoustic Echo Canceller implementation
 *
 * Key design decisions:
 *  1. Circular buffer  — O(1) sample insert, no memmove overhead
 *  2. Exponential power — avoids full recompute of ||X||^2 every sample
 *  3. Double-talk detect — compares near-end power to far-end power
 *  4. Freeze on double-talk — protects weights from corruption
 *
 * All math in float32.  For embedded fixed-point port, see comments tagged [FP].
 */

#include "aec_nlms.h"

#include <string.h>   /* memset */
#include <math.h>     /* log10f, fabsf */

/* ── helpers ─────────────────────────────────────────────────────── */

/** Read circular buffer with index wrapping — branchless modulo via mask.
 *  Works only when AEC_FILTER_LEN is a power of two.
 *  For non-power-of-two lengths, use: idx = (idx == 0) ? N-1 : idx-1
 */
static inline int prev_idx(int idx) {
    return (idx == 0) ? (AEC_FILTER_LEN - 1) : (idx - 1);
}

/* ── public functions ─────────────────────────────────────────────── */

void aec_init(AecState *st) {
    memset(st, 0, sizeof(AecState));
}

float aec_process(AecState *st, float x, float d) {

    /* ── 1. Write new reference sample into circular buffer ── */
    st->x_buf[st->buf_idx] = x;

    /* ── 2. Compute echo estimate: y = dot(W, X) ──────────────
     *
     *  Traverse buffer backwards from buf_idx — this gives samples
     *  in order [x(n), x(n-1), ..., x(n-L+1)] which is what we want.
     *
     *  [FP]: replace float multiply-add with Q15 MAC:
     *        acc += (int32_t)w[i] * (int32_t)x_buf[idx]  (Q30)
     *        then right-shift result by 15 to return to Q15.
     */
    float y   = 0.0f;
    int   idx = st->buf_idx;

    for (int i = 0; i < AEC_FILTER_LEN; i++) {
        y  += st->w[i] * st->x_buf[idx];
        idx = prev_idx(idx);
    }

    /* ── 3. Error signal (candidate clean output) ─────────── */
    float e = d - y;

    /* ── 4. Update running power of reference signal ──────────
     *
     *  Exponential moving average is O(1) per sample.
     *  Alternative: recompute full sum every sample → O(L), expensive.
     *  Full recompute is more accurate but 256× slower — not worth it
     *  for voice signals where power changes slowly.
     *
     *  [FP]: power in Q0 integer — just accumulate x*x in int32 and
     *        normalise by L to prevent overflow.
     */
    st->ref_power = AEC_POWER_ALPHA * st->ref_power
                  + (1.0f - AEC_POWER_ALPHA) * (x * x);

    /* ── 5. Double-talk detection ─────────────────────────────
     *
     *  Heuristic: if the error signal is much louder than the reference,
     *  a near-end speaker is active. Updating weights during double-talk
     *  corrupts W — the filter starts cancelling near-end voice instead
     *  of echo. Freeze update until far-end is dominant again.
     *
     *  More robust production approaches:
     *    - Geigel algorithm (compares max of d to max of x)
     *    - Cross-correlation based DTD
     *    - ML classifier
     */
    float near_power = e * e;
    float far_power  = st->ref_power + AEC_DELTA;
    st->double_talk  = (near_power > AEC_DT_THRESHOLD * far_power) ? 1 : 0;

    /* ── 6. NLMS weight update (freeze during double-talk) ────
     *
     *  Full update equation:
     *    W(n+1) = W(n) + [mu / (L * ref_power + delta)] * e(n) * X(n)
     *
     *  Normalisation denominator: L * ref_power approximates ||X||^2
     *  (since ref_power ≈ mean(x^2) and ||X||^2 = sum(x^2) ≈ L*mean(x^2))
     *
     *  [FP]: step in Q31 — be careful of overflow when multiplying
     *        Q31 step × Q15 error × Q15 sample. Use 64-bit accumulator.
     */
    if (!st->double_talk) {
        float norm  = (float)AEC_FILTER_LEN * st->ref_power + AEC_DELTA;
        float step  = AEC_STEP_SIZE / norm;

        idx = st->buf_idx;
        for (int i = 0; i < AEC_FILTER_LEN; i++) {
            st->w[i] += step * e * st->x_buf[idx];
            idx = prev_idx(idx);
        }
    }

    /* ── 7. Advance circular buffer pointer ─────────────────── */
    st->buf_idx = (st->buf_idx + 1) % AEC_FILTER_LEN;

    return e;
}

float aec_get_erle_db(const float *echo_in, const float *echo_out, int len) {
    float power_in  = 0.0f;
    float power_out = 0.0f;

    for (int i = 0; i < len; i++) {
        power_in  += echo_in[i]  * echo_in[i];
        power_out += echo_out[i] * echo_out[i];
    }

    if (power_out < 1e-12f) return 99.0f;   /* silence → perfect cancellation */

    return 10.0f * log10f(power_in / power_out);
}
