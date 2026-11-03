#include "cache_stats.h"

cache_stats operator-(cache_stats lhs, cache_stats rhs)
{
  cache_stats result;
  result.pf_requested = lhs.pf_requested - rhs.pf_requested;
  result.pf_issued = lhs.pf_issued - rhs.pf_issued;
  result.pf_useful = lhs.pf_useful - rhs.pf_useful;
  result.pf_useless = lhs.pf_useless - rhs.pf_useless;
  result.pf_fill = lhs.pf_fill - rhs.pf_fill;

  result.hits = lhs.hits - rhs.hits;
  result.misses = lhs.misses - rhs.misses;

  result.total_miss_latency_cycles = lhs.total_miss_latency_cycles - rhs.total_miss_latency_cycles;

  result.cycles = lhs.cycles - rhs.cycles;
  result.evictions = lhs.evictions - rhs.evictions;
  result.dirty_evictions = lhs.dirty_evictions - rhs.dirty_evictions;
  result.pf_late = lhs.pf_late - rhs.pf_late;
  result.mshr_occupancy_sum = lhs.mshr_occupancy_sum - rhs.mshr_occupancy_sum;
  result.mshr_full_cycles = lhs.mshr_full_cycles - rhs.mshr_full_cycles;
  result.mshr_full_rejects = lhs.mshr_full_rejects - rhs.mshr_full_rejects;
  result.lower_queue_rejects = lhs.lower_queue_rejects - rhs.lower_queue_rejects;
  return result;
}
