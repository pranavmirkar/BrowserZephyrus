// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Minimal stand-in for base::span, used ONLY by the standalone TSAN harness in
// this directory. It exists so the harness can compile the real
// privacy_ring_buffer.{h,cc} under a Linux clang with -fsanitize=thread,
// without dragging in //base.
//
// Do not add to a GN target and do not use from production code.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_TOOLS_SHIM_BASE_CONTAINERS_SPAN_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_TOOLS_SHIM_BASE_CONTAINERS_SPAN_H_

#include <stddef.h>

#include <vector>

namespace base {

template <typename T>
class span {
 public:
  span(T* data, size_t size) : data_(data), size_(size) {}
  // NOLINTNEXTLINE(google-explicit-constructor) — matches base::span's implicit
  // conversion from a contiguous container, which is what callers rely on.
  span(std::vector<T>& v) : data_(v.data()), size_(v.size()) {}

  T& operator[](size_t i) const { return data_[i]; }
  size_t size() const { return size_; }

 private:
  T* data_;
  size_t size_;
};

}  // namespace base

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_TOOLS_SHIM_BASE_CONTAINERS_SPAN_H_
