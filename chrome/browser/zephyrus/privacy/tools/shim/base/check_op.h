// Shim for the standalone sanitizer harnesses only.
//
// The real base/check_op.h reaches into partition_alloc, which cannot be
// compiled outside the Chromium build graph. These keep the same semantics for
// the harness: evaluate the operands, abort on mismatch.
#ifndef ZEPHYRUS_SHIM_BASE_CHECK_OP_H_
#define ZEPHYRUS_SHIM_BASE_CHECK_OP_H_

#include <cstdio>
#include <cstdlib>

#include "base/check.h"

namespace zephyrus_shim {
inline void Fail(const char* expr, const char* file, int line) {
  std::fprintf(stderr, "CHECK failed: %s at %s:%d\n", expr, file, line);
  std::abort();
}
}  // namespace zephyrus_shim

#define ZEPHYRUS_SHIM_CHECK_OP(a, op, b)                                  \
  do {                                                                    \
    if (!((a)op(b))) {                                                    \
      zephyrus_shim::Fail(#a " " #op " " #b, __FILE__, __LINE__);         \
    }                                                                     \
  } while (0)

#define CHECK_EQ(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, ==, b)
#define CHECK_NE(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, !=, b)
#define CHECK_LT(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, <, b)
#define CHECK_LE(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, <=, b)
#define CHECK_GT(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, >, b)
#define CHECK_GE(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, >=, b)

#if DCHECK_IS_ON()
#define DCHECK_EQ(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, ==, b)
#define DCHECK_NE(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, !=, b)
#define DCHECK_LT(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, <, b)
#define DCHECK_LE(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, <=, b)
#define DCHECK_GT(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, >, b)
#define DCHECK_GE(a, b) ZEPHYRUS_SHIM_CHECK_OP(a, >=, b)
#else
#define DCHECK_EQ(a, b) while (false) ZEPHYRUS_SHIM_CHECK_OP(a, ==, b)
#define DCHECK_NE(a, b) while (false) ZEPHYRUS_SHIM_CHECK_OP(a, !=, b)
#define DCHECK_LT(a, b) while (false) ZEPHYRUS_SHIM_CHECK_OP(a, <, b)
#define DCHECK_LE(a, b) while (false) ZEPHYRUS_SHIM_CHECK_OP(a, <=, b)
#define DCHECK_GT(a, b) while (false) ZEPHYRUS_SHIM_CHECK_OP(a, >, b)
#define DCHECK_GE(a, b) while (false) ZEPHYRUS_SHIM_CHECK_OP(a, >=, b)
#endif

#endif  // ZEPHYRUS_SHIM_BASE_CHECK_OP_H_
