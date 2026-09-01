// Shim for the standalone TSAN harness only. See ring_buffer_tsan_harness.cc.
#ifndef ZEPHYRUS_TSAN_SHIM_CHECK_H_
#define ZEPHYRUS_TSAN_SHIM_CHECK_H_
#include "base/dcheck_is_on.h"

// The real base/check.h defines this; this shim shadows that header, so it must
// define it too. Needed since privacy_ring_buffer.cc gained
// DCHECK_CALLED_ON_VALID_SEQUENCE, which expands to EAT_CHECK_STREAM_PARAMS()
// via the REAL base/sequence_checker.h (there is no shim for that one).
//
// A no-op is the honest shim: these harnesses build without DCHECKs, which is
// exactly what the macro degrades to upstream. Variadic because the sequence
// checker invokes it with no argument at all.
//
// This bit-rotted silently once already — the harness simply stopped compiling
// and, because nothing runs it automatically, nobody noticed that §12.4
// sanitizer coverage had lapsed.
#ifndef EAT_CHECK_STREAM_PARAMS
#define EAT_CHECK_STREAM_PARAMS(...) (void)0
#endif

#endif
