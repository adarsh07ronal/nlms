/**
 * test_aec.c — Unity test suite for AEC-NLMS
 *
 * Suite 1: Buffer / state tests
 *   - zero input → zero output
 *   - filter stays zero if input is zero
 *
 * Suite 2: Signal correctness tests
 *   - echo cancellation achieves > 20 dB ERLE after convergence
 *   - multi-tone convergence
 *
 * Suite 3: Double-talk protection tests
 *   - weights freeze when near-end power dominates
 *   - weights update when only far-end is present
 *
 * Run with: make test
 */

#include "../unity/unity.h"
#include "../src/aec_nlms.h"

#include <math.h>
#include <string.h>

#define PI          3.14159265358979f
#define SR          16000
#define ECHO_DELAY  50
#define ECHO_GAIN   0.7f

/* ── shared helper: simple delay-line ──────────────────────────── */
static float delay_buf[ECHO_DELAY];
static int   delay_idx = 0;

static void delay_reset(void) {
    memset(delay_buf, 0, sizeof(delay_buf));
    delay_idx = 0;
}

static float delay_tick(float x) {
    float out       = delay_buf[delay_idx] * ECHO_GAIN;
    delay_buf[delay_idx] = x;
    delay_idx       = (delay_idx + 1) % ECHO_DELAY;
    return out;
}

/* ── Unity setup / teardown ──────────────────────────────────────── */
void setUp(void)    { delay_reset(); }
void tearDown(void) {}

/* ══════════════════════════════════════════════════════════════════
 * SUITE 1 — Buffer / state
 * ══════════════════════════════════════════════════════════════════ */

void test_zero_input_gives_zero_output(void) {
    AecState aec;
    aec_init(&aec);

    for (int n = 0; n < 1000; n++) {
        float e = aec_process(&aec, 0.0f, 0.0f);
        TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, e);
    }
}

void test_init_zeroes_all_weights(void) {
    AecState aec;
    /* Dirty the memory first */
    memset(&aec, 0xAB, sizeof(aec));
    aec_init(&aec);

    for (int i = 0; i < AEC_FILTER_LEN; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, aec.w[i]);
    }
    TEST_ASSERT_EQUAL_INT(0, aec.buf_idx);
}

void test_buf_idx_wraps_correctly(void) {
    AecState aec;
    aec_init(&aec);

    /* Drive exactly AEC_FILTER_LEN samples through — idx should wrap to 0 */
    for (int n = 0; n < AEC_FILTER_LEN; n++) {
        aec_process(&aec, 0.1f, 0.0f);
    }
    TEST_ASSERT_EQUAL_INT(0, aec.buf_idx);
}

/* ══════════════════════════════════════════════════════════════════
 * SUITE 2 — Signal correctness
 * ══════════════════════════════════════════════════════════════════ */

void test_single_tone_erle_above_20dB(void) {
    AecState aec;
    aec_init(&aec);

    int     N          = SR * 3;
    int     eval_start = SR * 2;       /* skip convergence phase */
    float   echo_power = 0.0f;
    float   out_power  = 0.0f;

    for (int n = 0; n < N; n++) {
        float x    = 0.5f * sinf(2.0f * PI * 440.0f * n / SR);
        float echo = delay_tick(x);
        float e    = aec_process(&aec, x, echo);

        if (n >= eval_start) {
            echo_power += echo * echo;
            out_power  += e   * e;
        }
    }

    float erle = 10.0f * log10f(echo_power / (out_power + 1e-12f));
    printf("\n  [test] Single-tone ERLE = %.1f dB\n", erle);
    TEST_ASSERT_GREATER_THAN_FLOAT(20.0f, erle);
}

void test_multi_tone_erle_above_15dB(void) {
    AecState aec;
    aec_init(&aec);

    int     N          = SR * 4;
    int     eval_start = SR * 2;
    float   echo_power = 0.0f;
    float   out_power  = 0.0f;

    for (int n = 0; n < N; n++) {
        float x =  0.3f * sinf(2.0f * PI *  440.0f * n / SR)
                 + 0.2f * sinf(2.0f * PI *  880.0f * n / SR)
                 + 0.1f * sinf(2.0f * PI * 1320.0f * n / SR);

        float echo = delay_tick(x);
        float e    = aec_process(&aec, x, echo);

        if (n >= eval_start) {
            echo_power += echo * echo;
            out_power  += e   * e;
        }
    }

    float erle = 10.0f * log10f(echo_power / (out_power + 1e-12f));
    printf("\n  [test] Multi-tone ERLE = %.1f dB\n", erle);
    TEST_ASSERT_GREATER_THAN_FLOAT(15.0f, erle);
}

void test_erle_improves_over_time(void) {
    /* ERLE in the second half of processing must be higher than first half */
    AecState aec;
    aec_init(&aec);

    int   N         = SR * 4;
    int   half      = N / 2;
    float echo_p1   = 0.0f, out_p1 = 0.0f;
    float echo_p2   = 0.0f, out_p2 = 0.0f;

    for (int n = 0; n < N; n++) {
        float x    = 0.5f * sinf(2.0f * PI * 440.0f * n / SR);
        float echo = delay_tick(x);
        float e    = aec_process(&aec, x, echo);

        if (n < half) {
            echo_p1 += echo * echo;
            out_p1  += e   * e;
        } else {
            echo_p2 += echo * echo;
            out_p2  += e   * e;
        }
    }

    float erle1 = 10.0f * log10f(echo_p1 / (out_p1 + 1e-12f));
    float erle2 = 10.0f * log10f(echo_p2 / (out_p2 + 1e-12f));
    printf("\n  [test] ERLE first half=%.1f dB  second half=%.1f dB\n", erle1, erle2);
    TEST_ASSERT_GREATER_THAN_FLOAT(erle1, erle2);
}

/* ══════════════════════════════════════════════════════════════════
 * SUITE 3 — Double-talk protection
 * ══════════════════════════════════════════════════════════════════ */

void test_double_talk_flag_set_when_near_end_loud(void) {
    AecState aec;
    aec_init(&aec);

    /* Pre-converge */
    for (int n = 0; n < SR; n++) {
        float x    = 0.5f * sinf(2.0f * PI * 440.0f * n / SR);
        float echo = delay_tick(x);
        aec_process(&aec, x, echo);
    }

    /* Inject loud near-end — double_talk flag should fire */
    int dt_detected = 0;
    for (int n = 0; n < SR / 4; n++) {
        float x    = 0.1f * sinf(2.0f * PI * 440.0f * n / SR);   /* quiet far-end */
        float near = 2.0f * sinf(2.0f * PI * 300.0f * n / SR);   /* loud near-end */
        float echo = delay_tick(x);
        aec_process(&aec, x, echo + near);
        if (aec.double_talk) dt_detected = 1;
    }
    TEST_ASSERT_EQUAL_INT(1, dt_detected);
}

void test_weights_stable_during_double_talk(void) {
    AecState aec;
    aec_init(&aec);

    /* Pre-converge */
    delay_reset();
    for (int n = 0; n < SR; n++) {
        float x    = 0.5f * sinf(2.0f * PI * 440.0f * n / SR);
        float echo = delay_tick(x);
        aec_process(&aec, x, echo);
    }

    float w_ref[AEC_FILTER_LEN];
    memcpy(w_ref, aec.w, sizeof(w_ref));

    /* Double-talk phase: loud near-end, quiet far-end */
    for (int n = 0; n < SR / 2; n++) {
        float x    = 0.05f * sinf(2.0f * PI * 440.0f * n / SR);
        float near = 1.5f  * sinf(2.0f * PI * 300.0f * n / SR);
        float echo = delay_tick(x);
        aec_process(&aec, x, echo + near);
    }

    float drift = 0.0f;
    for (int i = 0; i < AEC_FILTER_LEN; i++)
        drift += fabsf(aec.w[i] - w_ref[i]);

    printf("\n  [test] Weight drift during double-talk = %.6f\n", drift);
    /* Drift should be significantly less than without DTD protection.
     * Without DTD, drift over the same period would be ~15–20.
     * With DTD freezing most updates, we expect < 5.0 total across all taps. */
    TEST_ASSERT_LESS_THAN_FLOAT(5.0f, drift);
}

void test_weights_update_when_no_double_talk(void) {
    AecState aec;
    aec_init(&aec);
    /* all zeros initially */
    float w_before[AEC_FILTER_LEN];
    memcpy(w_before, aec.w, sizeof(w_before));

    /* Far-end only — weights MUST change */
    for (int n = 0; n < SR / 4; n++) {
        float x    = 0.5f * sinf(2.0f * PI * 440.0f * n / SR);
        float echo = delay_tick(x);
        aec_process(&aec, x, echo);
    }

    float total_change = 0.0f;
    for (int i = 0; i < AEC_FILTER_LEN; i++)
        total_change += fabsf(aec.w[i] - w_before[i]);

    printf("\n  [test] Weight change (no double-talk) = %.4f\n", total_change);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.01f, total_change);
}

/* ══════════════════════════════════════════════════════════════════
 * Runner
 * ══════════════════════════════════════════════════════════════════ */

int main(void) {
    UNITY_BEGIN();

    /* Suite 1 */
    RUN_TEST(test_zero_input_gives_zero_output);
    RUN_TEST(test_init_zeroes_all_weights);
    RUN_TEST(test_buf_idx_wraps_correctly);

    /* Suite 2 */
    RUN_TEST(test_single_tone_erle_above_20dB);
    RUN_TEST(test_multi_tone_erle_above_15dB);
    RUN_TEST(test_erle_improves_over_time);

    /* Suite 3 */
    RUN_TEST(test_double_talk_flag_set_when_near_end_loud);
    RUN_TEST(test_weights_stable_during_double_talk);
    RUN_TEST(test_weights_update_when_no_double_talk);

    return UNITY_END();
}
