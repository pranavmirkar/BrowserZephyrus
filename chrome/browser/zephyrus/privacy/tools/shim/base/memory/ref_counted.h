// Shim for the standalone sanitizer harnesses only. See
// ../../privacy_lsan_harness.cc.
//
// Deliberately a REAL refcount, not a stub that never frees: the whole point of
// running LSAN over this is to find an object that is allocated and never
// released, and a shim that leaked by construction would report the shim's bug
// instead of ours.
#ifndef ZEPHYRUS_SHIM_BASE_MEMORY_REF_COUNTED_H_
#define ZEPHYRUS_SHIM_BASE_MEMORY_REF_COUNTED_H_

#include <atomic>
#include <utility>

template <typename T>
class scoped_refptr {
 public:
  scoped_refptr() = default;
  scoped_refptr(std::nullptr_t) {}
  explicit scoped_refptr(T* p) : ptr_(p) {
    if (ptr_) {
      ptr_->AddRef();
    }
  }
  scoped_refptr(const scoped_refptr& other) : scoped_refptr(other.ptr_) {}
  scoped_refptr(scoped_refptr&& other) noexcept : ptr_(other.ptr_) {
    other.ptr_ = nullptr;
  }
  scoped_refptr& operator=(scoped_refptr other) {
    std::swap(ptr_, other.ptr_);
    return *this;
  }
  ~scoped_refptr() {
    if (ptr_) {
      ptr_->Release();
    }
  }
  T* get() const { return ptr_; }
  T* operator->() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  T* ptr_ = nullptr;
};

namespace base {

template <typename T>
class RefCountedThreadSafe {
 public:
  void AddRef() const { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void Release() const {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete static_cast<const T*>(this);
    }
  }

 protected:
  RefCountedThreadSafe() = default;
  ~RefCountedThreadSafe() = default;

 private:
  mutable std::atomic<int> ref_count_{0};
};

template <typename T, typename... Args>
scoped_refptr<T> MakeRefCounted(Args&&... args) {
  return scoped_refptr<T>(new T(std::forward<Args>(args)...));
}

}  // namespace base

#endif  // ZEPHYRUS_SHIM_BASE_MEMORY_REF_COUNTED_H_
