#include "core_stats.h"

cpu_stats operator-(cpu_stats lhs, cpu_stats rhs)
{
  lhs.begin_instrs -= rhs.begin_instrs;
  lhs.begin_cycles -= rhs.begin_cycles;
  lhs.end_instrs -= rhs.end_instrs;
  lhs.end_cycles -= rhs.end_cycles;
  lhs.total_rob_occupancy_at_branch_mispredict -= rhs.total_rob_occupancy_at_branch_mispredict;
  lhs.fdip_wp_cycles -= rhs.fdip_wp_cycles;
  lhs.fdip_wp_stall_cycles -= rhs.fdip_wp_stall_cycles;
  lhs.wp_episodes -= rhs.wp_episodes;
  lhs.wp_episodes_ran_out -= rhs.wp_episodes_ran_out;
  for (const auto& [n, counts] : rhs.wp_episode_instrs) {
    lhs.wp_episode_instrs[n].first -= counts.first;
    lhs.wp_episode_instrs[n].second -= counts.second;
  }

  lhs.total_branch_types -= rhs.total_branch_types;
  lhs.branch_type_misses -= rhs.branch_type_misses;

  for (const auto& [stage, counts] : rhs.stage_counts) {
    lhs.stage_counts[stage].first -= counts.first;
    lhs.stage_counts[stage].second -= counts.second;
  }

  lhs.retired_loads -= rhs.retired_loads;
  lhs.retired_load_ops -= rhs.retired_load_ops;
  lhs.retired_stores -= rhs.retired_stores;
  lhs.retired_store_ops -= rhs.retired_store_ops;
  lhs.retired_branches -= rhs.retired_branches;
  lhs.retired_taken_branches -= rhs.retired_taken_branches;

  const auto subtract_histogram = [](std::vector<uint64_t>& l, const std::vector<uint64_t>& r) {
    if (std::size(l) < std::size(r))
      l.resize(std::size(r));
    for (std::size_t i = 0; i < std::size(r); ++i)
      l[i] -= r[i];
  };
  subtract_histogram(lhs.retire_width_histogram, rhs.retire_width_histogram);
  subtract_histogram(lhs.issue_width_histogram, rhs.issue_width_histogram);
  subtract_histogram(lhs.dispatch_width_histogram, rhs.dispatch_width_histogram);

  lhs.occupancy_cycles -= rhs.occupancy_cycles;
  lhs.rob_occupancy_sum -= rhs.rob_occupancy_sum;
  lhs.lq_occupancy_sum -= rhs.lq_occupancy_sum;
  lhs.sq_occupancy_sum -= rhs.sq_occupancy_sum;
  lhs.rob_full_cycles -= rhs.rob_full_cycles;
  lhs.lq_full_cycles -= rhs.lq_full_cycles;
  lhs.sq_full_cycles -= rhs.sq_full_cycles;

  lhs.recovery_cycles -= rhs.recovery_cycles;
  lhs.fetch_resteer_cycles -= rhs.fetch_resteer_cycles;
  lhs.mispredicts_resolved_at_decode -= rhs.mispredicts_resolved_at_decode;
  lhs.mispredicts_resolved_at_execute -= rhs.mispredicts_resolved_at_execute;

  lhs.loads_forwarded_from_store -= rhs.loads_forwarded_from_store;
  lhs.loads_waiting_on_store -= rhs.loads_waiting_on_store;

  return lhs;
}
