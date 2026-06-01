# AEC-NLMS: Acoustic Echo Canceller

A production-style Acoustic Echo Canceller (AEC) implemented in C using the
**Normalized Least Mean Squares (NLMS)** adaptive filter algorithm.

Built as a portfolio project demonstrating embedded audio DSP skills for roles at
companies like Qualcomm, Amazon Alexa, Dolby, and Apple India.

---

## What it does

When you're on a video call, the far-end speaker's voice plays through your
loudspeaker, bounces off the walls, and re-enters your microphone as echo.
This module estimates the room's acoustic impulse response adaptively and
subtracts the echo in real time:

```
d(n) = s(n) + echo(n)        mic = near-end voice + echo
echo(n) = x(n) * H           room convolves far-end with impulse response H
y(n) = x(n) * W(n)           estimated echo using adaptive filter W
e(n) = d(n) - y(n)           clean output  ← near-end voice only
```

The filter W adapts using NLMS until W ≈ H and echo is cancelled.

---

## NLMS update rule

```
W(n+1) = W(n)  +  [µ / (L·P(n) + δ)]  ·  e(n)  ·  X(n)
```

| Symbol | Meaning |
|--------|---------|
| `W(n)` | Filter weight vector (L taps — the echo model) |
| `X(n)` | Circular buffer of last L far-end reference samples |
| `e(n)` | Error = mic input − echo estimate |
| `µ`    | Step size (0 < µ < 2). Default: 0.3 |
| `P(n)` | Exponential moving average of reference power |
| `L`    | Filter length. Default: 256 taps = 16 ms at 16 kHz |
| `δ`    | Regularisation constant (1e-6) — prevents div-by-zero |

---

## Results

| Metric | Value |
|--------|-------|
| ERLE (single-tone, post-convergence) | **≥ 30 dB** |
| ERLE (multi-tone, post-convergence)  | **≥ 20 dB** |
| Convergence time                     | ~1–2 s at 16 kHz |
| Double-talk weight drift             | < 0.5 (normalised) |
| Unity tests passing                  | **9 / 9** |

ERLE = Echo Return Loss Enhancement = how many dB the echo was reduced.
30 dB means echo power reduced by 1000×.

---

## Project structure

```
aec-nlms/
├── src/
│   ├── aec_nlms.h       # public API + tuning parameters
│   ├── aec_nlms.c       # NLMS core + double-talk detection
│   └── main.c           # demo: 3 test scenarios + CSV export
├── test/
│   └── test_aec.c       # 9 Unity tests across 3 suites
├── unity/               # Unity test framework (fetched at build)
│   ├── unity.h
│   ├── unity.c
│   └── unity_internals.h
├── results/
│   ├── plot_erle.py          # Python: plot ERLE convergence
│   └── erle_convergence.png  # generated after `make plot`
└── Makefile
```

---

## Build and run

**Requirements:** `gcc`, `make`, `python3 + matplotlib` (optional, for plot)

```bash
# Clone
git clone https://github.com/adarsh07ronal/nlms.git
cd nlms

# Build and run demo (prints ERLE for 3 scenarios)
make

# Run all 9 Unity tests
make test

# Generate ERLE convergence plot → results/erle_convergence.png
make plot

# Clean build artefacts
make clean
```

Expected demo output:
```
AEC-NLMS  |  filter_len=256  step=0.30  delay=50 samples @ 16000 Hz

=== Test 1: Single-tone echo (440 Hz) ===
  ERLE = 32.4 dB  (target: > 25 dB)

=== Test 2: Multi-tone echo + CSV export ===
  CSV written to results/erle_over_time.csv
  Run:  python3 results/plot_erle.py

=== Test 3: Double-talk detection ===
  Double-talk detected 82% of samples
  Total weight drift   = 0.003214  (lower = better protection)

Done. ERLE > 25 dB on test 1 = PASS.
```

---

## Test suites

| Suite | Test | What it verifies |
|-------|------|-----------------|
| 1 — Buffer | `zero_input_gives_zero_output` | No arithmetic errors on silence |
| 1 — Buffer | `init_zeroes_all_weights` | Clean init even with dirty memory |
| 1 — Buffer | `buf_idx_wraps_correctly` | Circular buffer modulo wraps at L |
| 2 — Signal | `single_tone_erle_above_20dB` | Core cancellation works |
| 2 — Signal | `multi_tone_erle_above_15dB` | Convergence on realistic stimulus |
| 2 — Signal | `erle_improves_over_time` | Filter is actually learning |
| 3 — DTD | `double_talk_flag_set_when_near_end_loud` | DTD fires correctly |
| 3 — DTD | `weights_stable_during_double_talk` | Filter protected from corruption |
| 3 — DTD | `weights_update_when_no_double_talk` | Normal adaptation continues |

---

## Tuning parameters

All in `src/aec_nlms.h`:

| Parameter | Default | Effect |
|-----------|---------|--------|
| `AEC_FILTER_LEN` | 256 | Longer = more echo tail coverage, slower convergence |
| `AEC_STEP_SIZE` | 0.3 | Higher = faster convergence, risk of instability |
| `AEC_DELTA` | 1e-6 | Prevent div-by-zero; raise if you see NaN in silence |
| `AEC_POWER_ALPHA` | 0.9 | EMA smoothing; lower = more responsive to level changes |
| `AEC_DT_THRESHOLD` | 2.0 | DTD sensitivity; raise to freeze less aggressively |

Filter length guidelines at 16 kHz:

| Scenario | Recommended taps |
|----------|-----------------|
| Earbuds / headset | 128 (8 ms) |
| Laptop / phone | 256 (16 ms) |
| Small room | 512 (32 ms) |
| Conference room | 1024 (64 ms) |

---

## Embedded port notes (ARM Cortex-M)

The code is float32. For a bare-metal Cortex-M4F port:

1. **Enable FPU** — `SCB->CPACR |= (0xF << 20)` — float32 runs in hardware
2. **Fixed-point alternative** — replace `float` with `int32_t` Q15 throughout;
   use 64-bit accumulator for dot product; see `[FP]` comments in `aec_nlms.c`
3. **CMSIS-DSP** — replace inner dot product loop with `arm_dot_prod_f32()`
   which auto-vectorises to NEON on Cortex-A or uses DSP instructions on M4
4. **DMA integration** — call `aec_process()` from your I2S DMA half-complete
   callback at 16 kHz; ensure state struct is in fast SRAM (DTCM on STM32H7)

---

## Concepts covered (interview topics)

- Adaptive filtering theory — LMS vs NLMS vs RLS tradeoffs
- NLMS normalisation — why dividing by ||X||² prevents divergence
- Circular buffer implementation — O(1) insert, cache-friendly read
- Double-talk detection — Geigel algorithm, correlation-based DTD
- ERLE measurement — how to quantify echo cancellation performance
- Fixed-point DSP — Q15/Q31 arithmetic, overflow prevention
- Embedded optimisation — CMSIS-DSP, NEON intrinsics, DMA callbacks

---

## Author

Adarsh Ramakrishna — Senior Audio DSP / Embedded Software Engineer  
Sony Corporation (Tokyo) | IIT Madras M.Tech  
[linkedin.com/in/adarsh-ramakrishna](https://linkedin.com/in/adarsh-ramakrishna)
