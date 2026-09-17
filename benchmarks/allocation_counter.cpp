// Overrides global operator new/delete for benchmark binaries so allocation
// counts reported by bench_harness.hpp reflect what the process actually
// did, not an assumption about the design. Every overload forwards to the
// ordinary allocator; only the counting is added.
#include <atomic>
#include <cstdlib>
#include <new>

#include "bench_harness.hpp"

namespace {
std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_deallocations{0};
}  // namespace

void* operator new(std::size_t size) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(size)) return p;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* p) noexcept {
  g_deallocations.fetch_add(1, std::memory_order_relaxed);
  std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace me::bench {

std::uint64_t AllocationCounter::Allocations() {
  return g_allocations.load(std::memory_order_relaxed);
}
std::uint64_t AllocationCounter::Deallocations() {
  return g_deallocations.load(std::memory_order_relaxed);
}
void AllocationCounter::Reset() {
  g_allocations.store(0, std::memory_order_relaxed);
  g_deallocations.store(0, std::memory_order_relaxed);
}

}  // namespace me::bench
