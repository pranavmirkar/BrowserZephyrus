// Shim for the standalone TSAN harness only. See ring_buffer_tsan_harness.cc.
// DCHECKs are compiled out: TSAN is looking for data races, not assertion
// failures, and the debug-only single-caller check needs //base to build.
#ifndef ZEPHYRUS_TSAN_SHIM_DCHECK_IS_ON_H_
#define ZEPHYRUS_TSAN_SHIM_DCHECK_IS_ON_H_
#define DCHECK_IS_ON() 0
#endif
