#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace me::bench {

// Process-wide allocation counter. Defined in allocation_counter.cpp, which
// overrides global operator new/delete for every benchmark binary that
// links it — this is the only way to honestly report "allocation count"
// rather than infer it from the design.
struct AllocationCounter {
  static std::uint64_t Allocations();
  static std::uint64_t Deallocations();
  static void Reset();
};

// Wall-clock latency distribution + throughput for one benchmark. Timing
// uses std::chrono::steady_clock (monotonic, unaffected by wall-clock
// adjustments), one sample per logical operation.
class LatencyRecorder {
 public:
  explicit LatencyRecorder(std::string name) : name_(std::move(name)) {}

  void Record(std::chrono::nanoseconds ns) { samples_.push_back(ns.count()); }

  void Report(std::uint64_t trade_count = 0) const {
    if (samples_.empty()) {
      std::printf("%-40s (no samples)\n", name_.c_str());
      return;
    }
    std::vector<std::int64_t> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    auto pct = [&](double p) -> std::int64_t {
      std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
      return sorted[idx];
    };

    double total_ns = 0;
    for (auto s : sorted) total_ns += static_cast<double>(s);
    double total_sec = total_ns / 1e9;
    double ops_per_sec = total_sec > 0 ? static_cast<double>(sorted.size()) / total_sec : 0.0;

    std::printf("== %s ==\n", name_.c_str());
    std::printf("  operations       : %zu\n", sorted.size());
    std::printf("  ops/sec          : %.0f\n", ops_per_sec);
    if (trade_count > 0) {
      std::printf("  trades/sec       : %.0f\n", static_cast<double>(trade_count) / total_sec);
    }
    std::printf("  p50 latency (ns) : %lld\n", static_cast<long long>(pct(0.50)));
    std::printf("  p95 latency (ns) : %lld\n", static_cast<long long>(pct(0.95)));
    std::printf("  p99 latency (ns) : %lld\n", static_cast<long long>(pct(0.99)));
    std::printf("  p99.9 latency(ns): %lld\n", static_cast<long long>(pct(0.999)));
    std::printf("  allocations      : %llu\n",
                static_cast<unsigned long long>(AllocationCounter::Allocations()));
    std::printf("  deallocations    : %llu\n",
                static_cast<unsigned long long>(AllocationCounter::Deallocations()));
    std::printf("\n");
  }

 private:
  std::string name_;
  std::vector<std::int64_t> samples_;
};

// Runs `fn` (taking the iteration index) `iterations` times after `warmup`
// untimed iterations, recording one latency sample per call. Resets the
// allocation counter right before the timed region so the reported
// allocation count reflects only the measured operations.
template <typename Fn>
LatencyRecorder RunBenchmark(const std::string& name, std::size_t warmup, std::size_t iterations,
                              Fn&& fn) {
  for (std::size_t i = 0; i < warmup; ++i) fn(i);

  AllocationCounter::Reset();
  LatencyRecorder recorder(name);
  for (std::size_t i = 0; i < iterations; ++i) {
    auto start = std::chrono::steady_clock::now();
    fn(warmup + i);
    auto end = std::chrono::steady_clock::now();
    recorder.Record(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
  }
  return recorder;
}

}  // namespace me::bench
