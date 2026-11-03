#ifndef CACHE_STATS_H
#define CACHE_STATS_H

#include <cstdint>
#include <string>
#include <utility>

#include "event_counter.h"
#include "access_type.h"

struct cache_stats {
  std::string name;
  // prefetch stats
  uint64_t pf_requested = 0;
  uint64_t pf_issued = 0;
  uint64_t pf_useful = 0;
  uint64_t pf_useless = 0;
  uint64_t pf_fill = 0;

  champsim::stats::event_counter<std::pair<access_type, std::size_t>> hits = {};
  champsim::stats::event_counter<std::pair<access_type, std::size_t>> misses = {};
  champsim::stats::event_counter<std::pair<access_type, std::size_t>> miss_merge = {};
  champsim::stats::event_counter<std::pair<access_type, std::size_t>> fill = {};

  long total_miss_latency_cycles{};

  // Performance-counter style events (cf. L2_LINES_OUT, L1D_PEND_MISS on x86)
  uint64_t cycles = 0;               // cycles this cache operated, for the averages below
  uint64_t evictions = 0;            // valid lines replaced by fills
  uint64_t dirty_evictions = 0;      // evictions that wrote the line back to the lower level
  uint64_t pf_late = 0;              // demand misses that merged into an in-flight prefetch from this cache (also counted as useful)
  uint64_t mshr_occupancy_sum = 0;   // MSHR entries in use, summed over cycles
  uint64_t mshr_full_cycles = 0;     // cycles every MSHR was in use
  uint64_t mshr_full_rejects = 0;    // misses that couldn't allocate an MSHR and had to be retried
  uint64_t lower_queue_rejects = 0;  // misses the lower level's queue had no room for, and had to be retried
};

cache_stats operator-(cache_stats lhs, cache_stats rhs);

namespace champsim {
struct cache_queue_stats {
  uint64_t RQ_ACCESS = 0;
  uint64_t RQ_FULL = 0;
  uint64_t RQ_TO_CACHE = 0;
  uint64_t PQ_ACCESS = 0;
  uint64_t PQ_FULL = 0;
  uint64_t PQ_TO_CACHE = 0;
  uint64_t WQ_ACCESS = 0;
  uint64_t WQ_FULL = 0;
  uint64_t WQ_TO_CACHE = 0;
};
} // namespace champsim

#endif
