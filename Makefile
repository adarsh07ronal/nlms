# ── AEC-NLMS Makefile ─────────────────────────────────────────────
#
# Targets:
#   make          → build and run demo (src/main.c)
#   make test     → build and run all Unity tests
#   make plot     → generate ERLE convergence plot (requires Python3 + matplotlib)
#   make clean    → remove build artefacts

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -Isrc -Iunity
LDFLAGS = -lm

SRC_DIR   = src
TEST_DIR  = test
UNITY_DIR = unity
BUILD_DIR = build

SRCS        = $(SRC_DIR)/aec_nlms.c
MAIN_SRC    = $(SRC_DIR)/main.c
TEST_SRC    = $(TEST_DIR)/test_aec.c
UNITY_SRC   = $(UNITY_DIR)/unity.c

TARGET      = $(BUILD_DIR)/aec_demo
TEST_TARGET = $(BUILD_DIR)/test_aec

# ── default: build + run demo ──────────────────────────────────────
.PHONY: all
all: $(TARGET)
	@mkdir -p results
	./$(TARGET)

$(TARGET): $(SRCS) $(MAIN_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── test target ────────────────────────────────────────────────────
.PHONY: test
test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(SRCS) $(TEST_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── plot target ────────────────────────────────────────────────────
.PHONY: plot
plot: all
	python3 results/plot_erle.py

# ── helpers ────────────────────────────────────────────────────────
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f results/erle_over_time.csv

.PHONY: help
help:
	@echo "Targets:"
	@echo "  make        — build and run demo"
	@echo "  make test   — run Unity test suite"
	@echo "  make plot   — generate ERLE convergence plot"
	@echo "  make clean  — remove build artefacts"
