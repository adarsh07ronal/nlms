/**
 * main.c — AEC-NLMS demo and ERLE measurement
 *
 * Simulates three test scenarios:
 *   1. Single-tone echo (440 Hz) — validates basic cancellation
 *   2. Multi-tone echo (440+880+1320 Hz mix) — validates convergence on richer signal
 *   3. Double-talk — far-end echo + near-end voice, confirms weights are frozen
 *
 * Outputs:
 *   - ERLE in dB printed to stdout
 *   - results/erle_over_time.csv for Python plot script
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "aec_nlms.h"

#define PI          3.14159265358979f
#define SAMPLE_RATE 16000
#define ECHO_DELAY  50              /* samples — simulates ~3 ms room delay   */
#define ECHO_GAIN   0.7f            /* echo attenuation factor                */
#define TEST_SECS   4               /* total test duration in seconds         */
#define WARMUP_SECS 2               /* exclude first N seconds from ERLE avg  */

/* ── simple delay-line echo simulator ──────────────────────────── */
typedef struct {
    float buf[ECHO_DELAY];
    int   idx;
} DelayLine;

static void  delay_init(DelayLine *d) { memset(d, 0, sizeof(DelayLine)); }
static float delay_process(DelayLine *d, float x) {
    float out    = d->buf[d->idx] * ECHO_GAIN;
    d->buf[d->idx] = x;
    d->idx       = (d->idx + 1) % ECHO_DELAY;
    return out;
}

/* ── test 1: single-tone echo ────────────────────────────────────── */
static void test_single_tone(void) {
    printf("\n=== Test 1: Single-tone echo (440 Hz) ===\n");

    int    N = SAMPLE_RATE * TEST_SECS;
    float *echo_buf = malloc(N * sizeof(float));
    float *out_buf  = malloc(N * sizeof(float));

    AecState aec;
    DelayLine room;
    aec_init(&aec);
    delay_init(&room);

    for (int n = 0; n < N; n++) {
        float x    = 0.5f * sinf(2.0f * PI * 440.0f * n / SAMPLE_RATE);
        float echo = delay_process(&room, x);
        float d    = echo;                   /* mic = echo only (no near-end) */
        float e    = aec_process(&aec, x, d);

        echo_buf[n] = echo;
        out_buf[n]  = e;
    }

    int   eval_start = SAMPLE_RATE * WARMUP_SECS;
    float erle = aec_get_erle_db(
        echo_buf + eval_start,
        out_buf  + eval_start,
        N - eval_start
    );
    printf("  ERLE = %.1f dB  (target: > 25 dB)\n", erle);

    free(echo_buf);
    free(out_buf);
}

/* ── test 2: multi-tone echo, export CSV for plot ────────────────── */
static void test_multi_tone_csv(void) {
    printf("\n=== Test 2: Multi-tone echo + CSV export ===\n");

    int    N = SAMPLE_RATE * TEST_SECS;
    int    window = SAMPLE_RATE / 4;   /* measure ERLE every 250 ms */

    FILE *f = fopen("results/erle_over_time.csv", "w");
    if (!f) { perror("fopen"); return; }
    fprintf(f, "time_s,erle_db\n");

    AecState aec;
    DelayLine room;
    aec_init(&aec);
    delay_init(&room);

    float *echo_w = malloc(window * sizeof(float));
    float *out_w  = malloc(window * sizeof(float));
    int    w_idx  = 0;
    float  time_s = 0.0f;

    for (int n = 0; n < N; n++) {
        /* mix of three harmonics — realistic voice-like stimulus */
        float x =  0.3f * sinf(2.0f * PI *  440.0f * n / SAMPLE_RATE)
                 + 0.2f * sinf(2.0f * PI *  880.0f * n / SAMPLE_RATE)
                 + 0.1f * sinf(2.0f * PI * 1320.0f * n / SAMPLE_RATE);

        float echo = delay_process(&room, x);
        float e    = aec_process(&aec, x, echo);

        echo_w[w_idx] = echo;
        out_w [w_idx] = e;
        w_idx++;

        if (w_idx == window) {
            time_s = (float)n / SAMPLE_RATE;
            float erle = aec_get_erle_db(echo_w, out_w, window);
            fprintf(f, "%.3f,%.2f\n", time_s, erle);
            w_idx = 0;
        }
    }

    fclose(f);
    printf("  CSV written to results/erle_over_time.csv\n");
    printf("  Run:  python3 results/plot_erle.py\n");

    free(echo_w);
    free(out_w);
}

/* ── test 3: double-talk ─────────────────────────────────────────── */
static void test_double_talk(void) {
    printf("\n=== Test 3: Double-talk detection ===\n");

    int N = SAMPLE_RATE * 2;

    AecState aec;
    DelayLine room;
    aec_init(&aec);
    delay_init(&room);

    /* pre-converge filter for 1 second without near-end */
    for (int n = 0; n < SAMPLE_RATE; n++) {
        float x    = 0.5f * sinf(2.0f * PI * 440.0f * n / SAMPLE_RATE);
        float echo = delay_process(&room, x);
        aec_process(&aec, x, echo);
    }

    /* snapshot weights after convergence */
    float w_before[AEC_FILTER_LEN];
    memcpy(w_before, aec.w, sizeof(w_before));

    /* now apply double-talk: far-end echo + loud near-end voice */
    int dt_freeze_count = 0;
    for (int n = 0; n < N; n++) {
        float x      = 0.5f * sinf(2.0f * PI * 440.0f * n / SAMPLE_RATE);
        float echo   = delay_process(&room, x);
        float near   = 0.8f * sinf(2.0f * PI * 300.0f * n / SAMPLE_RATE); /* loud near-end */
        float d      = echo + near;

        aec_process(&aec, x, d);
        if (aec.double_talk) dt_freeze_count++;
    }

    /* measure weight drift — should be small if DTD worked */
    float drift = 0.0f;
    for (int i = 0; i < AEC_FILTER_LEN; i++)
        drift += fabsf(aec.w[i] - w_before[i]);

    float dt_pct = 100.0f * dt_freeze_count / N;
    printf("  Double-talk detected %.0f%% of samples\n", dt_pct);
    printf("  Total weight drift   = %.6f  (lower = better protection)\n", drift);
}

/* ── main ─────────────────────────────────────────────────────────── */
int main(void) {
    printf("AEC-NLMS  |  filter_len=%d  step=%.2f  delay=%d samples @ %d Hz\n",
           AEC_FILTER_LEN, AEC_STEP_SIZE, ECHO_DELAY, SAMPLE_RATE);

    test_single_tone();
    test_multi_tone_csv();
    test_double_talk();

    printf("\nDone. ERLE > 25 dB on test 1 = PASS.\n");
    return 0;
}
