#pragma once

#include <cstddef>
#include <new>
#include <vector>

namespace me {

// Fixed-capacity, preallocated free-list pool. All storage is allocated once
// in the constructor; Acquire()/Release() perform no heap allocation, so the
// matching hot path never calls new/malloc per order.
//
// Not thread-safe by design: each matching-engine partition owns its own
// pool and touches it only from its single owning thread.
template <typename T>
class ObjectPool {
 public:
  explicit ObjectPool(std::size_t capacity) : slots_(capacity) {
    for (std::size_t i = 0; i + 1 < capacity; ++i) {
      slots_[i].next_free = &slots_[i + 1];
    }
    if (capacity > 0) {
      slots_[capacity - 1].next_free = nullptr;
      free_head_ = &slots_[0];
    }
  }

  ObjectPool(const ObjectPool&) = delete;
  ObjectPool& operator=(const ObjectPool&) = delete;

  ~ObjectPool() = default;

  // Returns a pointer to a live, constructed T, or nullptr if the pool is
  // exhausted. Args are forwarded to T's constructor (placement-new).
  template <typename... Args>
  T* Acquire(Args&&... args) {
    if (free_head_ == nullptr) return nullptr;
    Slot* slot = free_head_;
    free_head_ = slot->next_free;
    T* obj = ::new (static_cast<void*>(&slot->storage)) T(std::forward<Args>(args)...);
    ++in_use_;
    return obj;
  }

  // Destroys `obj` and returns its slot to the free list. `obj` must have
  // been returned by Acquire() on this pool and not already released.
  void Release(T* obj) {
    obj->~T();
    Slot* slot = reinterpret_cast<Slot*>(obj);
    slot->next_free = free_head_;
    free_head_ = slot;
    --in_use_;
  }

  [[nodiscard]] std::size_t capacity() const { return slots_.size(); }
  [[nodiscard]] std::size_t in_use() const { return in_use_; }
  [[nodiscard]] std::size_t available() const { return capacity() - in_use_; }

 private:
  union Slot {
    Slot() {}
    ~Slot() {}
    alignas(T) std::byte storage[sizeof(T)];
    Slot* next_free;
  };

  std::vector<Slot> slots_;
  Slot* free_head_ = nullptr;
  std::size_t in_use_ = 0;
};

}  // namespace me
