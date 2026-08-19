#!/bin/bash
# Build and run the Zephyrus privacy LSAN harness under WSL.
#
# Run from WSL:  bash chrome/browser/zephyrus/privacy/tools/run_lsan.sh
#
# Two false negatives had to be designed out of this, and both look exactly like
# a pass:
#
#  1. detect_leaks=1 is REQUIRED. This Ubuntu's clang 18 does not enable leak
#     detection by default, so ASAN runs and reports nothing at all.
#  2. The positive control must retain NO pointer to the leaked block and must
#     be large. An earlier version leaked a small std::string and printed its
#     address; LSAN classified it as "still reachable" and stayed silent even
#     with detection on.
#
# If the control below does not report a leak, STOP: the real run beneath it
# proves nothing.
cd "$(dirname "$0")" || exit 1

echo "=== compiling ==="
clang++ -std=c++20 -fsanitize=address -g -O1 \
  -I shim -I ../../../../.. \
  privacy_lsan_harness.cc ../privacy_ring_buffer.cc ../domain_string_table.cc \
  -o /tmp/privacy_lsan || { echo "COMPILE FAILED"; exit 1; }
echo "compiled"

echo
echo "=== POSITIVE CONTROL: LSAN must report a leak here ==="
ASAN_OPTIONS=detect_leaks=1 /tmp/privacy_lsan --leak-positive-control
echo "control exit=$?  (non-zero expected)"

echo
echo "=== REAL RUN: ring buffer + string table, no deliberate leak ==="
ASAN_OPTIONS=detect_leaks=1 /tmp/privacy_lsan
echo "real exit=$?  (zero expected)"
