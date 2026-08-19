// Shim for the standalone sanitizer harnesses only.
#ifndef ZEPHYRUS_SHIM_BASE_SYNCHRONIZATION_LOCK_H_
#define ZEPHYRUS_SHIM_BASE_SYNCHRONIZATION_LOCK_H_

#include <mutex>

// Chromium's thread-safety annotations compile to nothing without -Wthread-
// safety; the harness does not enable it, so these are no-ops here.
#define GUARDED_BY(x)
#define EXCLUSIVE_LOCKS_REQUIRED(...)

namespace base {

class Lock {
 public:
  void Acquire() { mutex_.lock(); }
  void Release() { mutex_.unlock(); }

 private:
  std::mutex mutex_;
};

class AutoLock {
 public:
  explicit AutoLock(Lock& lock) : lock_(lock) { lock_.Acquire(); }
  ~AutoLock() { lock_.Release(); }
  AutoLock(const AutoLock&) = delete;
  AutoLock& operator=(const AutoLock&) = delete;

 private:
  Lock& lock_;
};

}  // namespace base

#endif  // ZEPHYRUS_SHIM_BASE_SYNCHRONIZATION_LOCK_H_
