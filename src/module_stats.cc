/*
 * Static formatters for the built-in module interfaces. The
 * implementations (CACHE, O3_CPU, MEMORY_CONTROLLER) inherit
 * ``module_stat`` and route ``print_stats`` / ``json_stats`` through
 * these helpers so the formatting logic lives next to the data.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <ratio>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "modules.h"
#include "json_stat_builder.h"

namespace
{
template <typename N, typename D>
auto print_ratio(N num, D denom)
{
  if (denom > 0) {
    return fmt::format("{:.4g}", std::ceil(num) / std::ceil(denom));
  }
  return std::string{"-"};
}

// Formats a histogram as " k:count" pairs, skipping empty buckets
std::string format_histogram(const std::vector<uint64_t>& histogram)
{
  std::string retval;
  for (std::size_t k = 0; k < std::size(histogram); ++k) {
    if (histogram[k] != 0)
      retval += fmt::format(" {}:{}", k, histogram[k]);
  }
  return retval;
}

// stages are printed in pipeline order; any stages not listed here are printed afterwards
constexpr std::array<std::string_view, 8> pipeline_stage_order{"fetch", "dib_hit", "decode", "dispatch", "schedule", "execute", "complete", "retire"};

template <typename F>
void for_each_stage(const cpu_stats& stats, F&& func)
{
  for (auto stage : pipeline_stage_order) {
    if (auto it = stats.stage_counts.find(std::string{stage}); it != std::end(stats.stage_counts)) {
      func(it->first, it->second);
    }
  }
  for (const auto& [stage, counts] : stats.stage_counts) {
    if (std::find(std::begin(pipeline_stage_order), std::end(pipeline_stage_order), stage) == std::end(pipeline_stage_order)) {
      func(stage, counts);
    }
  }
}
} // namespace

// ============================================================================
// core_module formatting
// ============================================================================

std::vector<std::string> champsim::modules::core_module::format_plaintext(const stats_type& stats)
{
  constexpr std::array types{branch_type::BRANCH_DIRECT_JUMP, branch_type::BRANCH_INDIRECT,      branch_type::BRANCH_CONDITIONAL,
                             branch_type::BRANCH_DIRECT_CALL, branch_type::BRANCH_INDIRECT_CALL, branch_type::BRANCH_RETURN};
  auto total_branch = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0LL, [tbt = stats.total_branch_types](auto acc, auto next) { return acc + tbt.value_or(next, 0); }));
  auto total_mispredictions = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0LL, [btm = stats.branch_type_misses](auto acc, auto next) { return acc + btm.value_or(next, 0); }));

  std::vector<std::string> lines{};
  lines.push_back(fmt::format("{} cumulative IPC: {} instructions: {} cycles: {}", stats.name, ::print_ratio(stats.instrs(), stats.cycles()), stats.instrs(),
                              stats.cycles()));

  lines.push_back(fmt::format("{} Branch Prediction Accuracy: {}% MPKI: {} Average ROB Occupancy at Mispredict: {}", stats.name,
                              ::print_ratio(100 * (total_branch - total_mispredictions), total_branch),
                              ::print_ratio(std::kilo::num * total_mispredictions, stats.instrs()),
                              ::print_ratio(stats.total_rob_occupancy_at_branch_mispredict, total_mispredictions)));

  lines.push_back(fmt::format("{} FDIP wrong path cycles: {} stalled waiting for wrong path instrs.: {}", stats.name, stats.fdip_wp_cycles,
                              stats.fdip_wp_stall_cycles));

  lines.push_back(fmt::format("{} WP episodes: {} ran out of WP instrs.: {} ({}%)", stats.name, stats.wp_episodes, stats.wp_episodes_ran_out,
                              ::print_ratio(100 * stats.wp_episodes_ran_out, stats.wp_episodes)));
  std::string histogram;
  for (const auto& [n, counts] : stats.wp_episode_instrs) {
    if (counts.first != 0 || counts.second != 0)
      histogram += fmt::format(" {}:{}/{}", n, counts.first, counts.second);
  }
  lines.push_back(fmt::format("{} WP instrs. pulled per episode (instrs:not ran out/ran out):{}", stats.name, histogram));

  lines.emplace_back("Branch type MPKI");
  for (auto idx : types) {
    lines.push_back(fmt::format("{}: {}", branch_type_names.at(champsim::to_underlying(idx)),
                                ::print_ratio(std::kilo::num * stats.branch_type_misses.value_or(idx, 0), stats.instrs())));
  }

  if (!std::empty(stats.stage_counts)) {
    lines.emplace_back("Pipeline stage counts");
    ::for_each_stage(stats, [&lines](const auto& stage, const auto& counts) {
      lines.push_back(fmt::format("{}: correct path: {} wrong path: {}", stage, counts.first, counts.second));
    });
  }

  lines.emplace_back("Retired instruction mix");
  lines.push_back(fmt::format("{} retired loads: {} (memory operands: {}) stores: {} (memory operands: {})", stats.name, stats.retired_loads,
                              stats.retired_load_ops, stats.retired_stores, stats.retired_store_ops));
  const auto retired_total = std::accumulate(std::begin(types), std::end(types), 0LL, [&](auto acc, auto t) { return acc + stats.retired_branches.value_or(t, 0); });
  const auto taken_total = std::accumulate(std::begin(types), std::end(types), 0LL, [&](auto acc, auto t) { return acc + stats.retired_taken_branches.value_or(t, 0); });
  lines.push_back(fmt::format("{} retired branches: {} taken: {}", stats.name, retired_total, taken_total));
  for (auto idx : types) {
    lines.push_back(fmt::format("{}: retired: {} taken: {}", branch_type_names.at(champsim::to_underlying(idx)), stats.retired_branches.value_or(idx, 0),
                                stats.retired_taken_branches.value_or(idx, 0)));
  }

  lines.emplace_back("Core activity");
  lines.push_back(fmt::format("{} average occupancy ROB: {} LQ: {} SQ: {} cycles full ROB: {} LQ: {} SQ: {}", stats.name,
                              ::print_ratio(stats.rob_occupancy_sum, stats.occupancy_cycles), ::print_ratio(stats.lq_occupancy_sum, stats.occupancy_cycles),
                              ::print_ratio(stats.sq_occupancy_sum, stats.occupancy_cycles), stats.rob_full_cycles, stats.lq_full_cycles, stats.sq_full_cycles));
  lines.push_back(fmt::format("{} mispredict recovery cycles: {} fetch resteer cycles: {} mispredicts resolved at decode: {} at execute: {}", stats.name,
                              stats.recovery_cycles, stats.fetch_resteer_cycles, stats.mispredicts_resolved_at_decode, stats.mispredicts_resolved_at_execute));
  lines.push_back(fmt::format("{} loads forwarded from an executed store: {} loads waiting on an older store: {}", stats.name, stats.loads_forwarded_from_store,
                              stats.loads_waiting_on_store));
  lines.push_back(fmt::format("{} retire width histogram (instrs:cycles):{}", stats.name, ::format_histogram(stats.retire_width_histogram)));
  lines.push_back(fmt::format("{} issue width histogram (instrs:cycles):{}", stats.name, ::format_histogram(stats.issue_width_histogram)));
  lines.push_back(fmt::format("{} dispatch width histogram (instrs:cycles):{}", stats.name, ::format_histogram(stats.dispatch_width_histogram)));

  return lines;
}

void champsim::modules::core_module::format_json(const stats_type& stats, champsim::json_stat_builder& b)
{
  constexpr std::array types{branch_type::BRANCH_DIRECT_JUMP, branch_type::BRANCH_INDIRECT,      branch_type::BRANCH_CONDITIONAL,
                             branch_type::BRANCH_DIRECT_CALL, branch_type::BRANCH_INDIRECT_CALL, branch_type::BRANCH_RETURN};

  auto total_mispredictions = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0LL, [btm = stats.branch_type_misses](auto acc, auto next) { return acc + btm.value_or(next, 0); }));

  b.add("instructions", stats.instrs())
   .add("cycles", stats.cycles())
   .add("Avg ROB occupancy at mispredict", std::ceil(stats.total_rob_occupancy_at_branch_mispredict) / std::ceil(total_mispredictions))
   .add("fdip wrong path cycles", stats.fdip_wp_cycles)
   .add("fdip wrong path stall cycles", stats.fdip_wp_stall_cycles);

  auto mpki = b.group("mispredict");
  for (auto type : types) {
    mpki.add(std::string{branch_type_names.at(champsim::to_underlying(type))}, stats.branch_type_misses.value_or(type, 0));
  }

  if (!std::empty(stats.stage_counts)) {
    auto stages = b.group("stage counts");
    ::for_each_stage(stats, [&stages](const auto& stage, const auto& counts) {
      stages.group(stage).add("correct path", counts.first).add("wrong path", counts.second);
    });
  }

  b.add("retired loads", stats.retired_loads)
   .add("retired load operands", stats.retired_load_ops)
   .add("retired stores", stats.retired_stores)
   .add("retired store operands", stats.retired_store_ops);
  auto retired = b.group("retired branches");
  for (auto type : types) {
    retired.group(std::string{branch_type_names.at(champsim::to_underlying(type))})
        .add("retired", stats.retired_branches.value_or(type, 0))
        .add("taken", stats.retired_taken_branches.value_or(type, 0));
  }

  b.add("occupancy cycles", stats.occupancy_cycles)
   .add("ROB occupancy sum", stats.rob_occupancy_sum)
   .add("LQ occupancy sum", stats.lq_occupancy_sum)
   .add("SQ occupancy sum", stats.sq_occupancy_sum)
   .add("ROB full cycles", stats.rob_full_cycles)
   .add("LQ full cycles", stats.lq_full_cycles)
   .add("SQ full cycles", stats.sq_full_cycles)
   .add("mispredict recovery cycles", stats.recovery_cycles)
   .add("fetch resteer cycles", stats.fetch_resteer_cycles)
   .add("mispredicts resolved at decode", stats.mispredicts_resolved_at_decode)
   .add("mispredicts resolved at execute", stats.mispredicts_resolved_at_execute)
   .add("loads forwarded from store", stats.loads_forwarded_from_store)
   .add("loads waiting on store", stats.loads_waiting_on_store)
   .add("retire width histogram", stats.retire_width_histogram)
   .add("issue width histogram", stats.issue_width_histogram)
   .add("dispatch width histogram", stats.dispatch_width_histogram);
}

// source_consumer hooks for phase messaging
int champsim::modules::core_module::entity_index() const { return static_cast<int>(get_cpu_num()); }

uint64_t champsim::modules::core_module::sim_progress() const { return sim_instr(); }

std::string champsim::modules::core_module::source_finish_message(const std::string& phase_name) const
{
  return fmt::format("{} finished CPU {} instructions: {} cycles: {} cumulative IPC: {:.4g}",
                     phase_name, get_cpu_num(), sim_instr(), sim_cycle(),
                     std::ceil(static_cast<double>(sim_instr())) / std::ceil(static_cast<double>(sim_cycle())));
}

std::string champsim::modules::core_module::phase_complete_message(const std::string& phase_name) const
{
  return fmt::format("{} complete CPU {} instructions: {} cycles: {} cumulative IPC: {:.4g}",
                     phase_name, get_cpu_num(), sim_instr(), sim_cycle(),
                     std::ceil(static_cast<double>(sim_instr())) / std::ceil(static_cast<double>(sim_cycle())));
}

// ============================================================================
// cache_module formatting
// ============================================================================

std::vector<std::string> champsim::modules::cache_module::format_plaintext(const stats_type& stats)
{
  using hits_value_type = typename decltype(stats.hits)::value_type;
  using misses_value_type = typename decltype(stats.misses)::value_type;
  using miss_merge_value_type = typename decltype(stats.miss_merge)::value_type;
  using fill_value_type = typename decltype(stats.fill)::value_type;

  // We need mutable copies to allocate missing keys
  auto hits = stats.hits;
  auto misses = stats.misses;
  auto miss_merge = stats.miss_merge;
  auto fill = stats.fill;

  std::vector<std::size_t> cpus;

  // build a vector of all existing cpus
  auto stat_keys = {hits.get_keys(), misses.get_keys(), miss_merge.get_keys(), fill.get_keys()};
  for (auto keys : stat_keys) {
    std::transform(std::begin(keys), std::end(keys), std::back_inserter(cpus), [](auto val) { return val.second; });
  }
  std::sort(std::begin(cpus), std::end(cpus));
  auto uniq_end = std::unique(std::begin(cpus), std::end(cpus));
  cpus.erase(uniq_end, std::end(cpus));

  for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
    for (auto cpu : cpus) {
      hits.allocate(std::pair{type, cpu});
      misses.allocate(std::pair{type, cpu});
      miss_merge.allocate(std::pair{type, cpu});
      fill.allocate(std::pair{type, cpu});
    }
  }

  std::vector<std::string> lines{};
  for (auto cpu : cpus) {
    hits_value_type total_hits = 0;
    misses_value_type total_misses = 0;
    miss_merge_value_type total_miss_merge = 0;
    fill_value_type total_fill = 0;
    for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
      total_hits += hits.value_or(std::pair{type, cpu}, hits_value_type{});
      total_misses += misses.value_or(std::pair{type, cpu}, misses_value_type{});
      total_miss_merge += miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{});
      total_fill += fill.value_or(std::pair{type, cpu}, miss_merge_value_type{});
    }

    fmt::format_string<std::string_view, std::string_view, int, int, int> hitmiss_fmtstr{
        "cpu{}->{} {:<12s} ACCESS: {:10d} HIT: {:10d} MISS: {:10d} MISS_MERGE: {:10d}"};
    lines.push_back(fmt::format(hitmiss_fmtstr, cpu, stats.name, "TOTAL", total_hits + total_misses, total_hits, total_misses, total_miss_merge));
    for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
      lines.push_back(
          fmt::format(hitmiss_fmtstr, cpu, stats.name, access_type_names.at(champsim::to_underlying(type)),
                      hits.value_or(std::pair{type, cpu}, hits_value_type{}) + misses.value_or(std::pair{type, cpu}, misses_value_type{}),
                      hits.value_or(std::pair{type, cpu}, hits_value_type{}), misses.value_or(std::pair{type, cpu}, misses_value_type{}),
                      miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{})));
    }

    lines.push_back(fmt::format("cpu{}->{} PREFETCH REQUESTED: {:10} ISSUED: {:10} USEFUL: {:10} USELESS: {:10}", cpu, stats.name, stats.pf_requested,
                                stats.pf_issued, stats.pf_useful, stats.pf_useless));

    uint64_t total_downstream_demands = total_fill - fill.value_or(std::pair{access_type::PREFETCH, cpu}, fill_value_type{});
    lines.push_back(
        fmt::format("cpu{}->{} AVERAGE MISS LATENCY: {} cycles", cpu, stats.name, ::print_ratio(stats.total_miss_latency_cycles, total_downstream_demands)));

    lines.push_back(fmt::format("cpu{}->{} EVICTIONS: {:10} DIRTY: {:10} LATE PREFETCHES: {:10}", cpu, stats.name, stats.evictions, stats.dirty_evictions,
                                stats.pf_late));
    lines.push_back(fmt::format("cpu{}->{} AVERAGE MSHR OCCUPANCY: {} MSHR FULL CYCLES: {} MSHR FULL REJECTS: {} LOWER QUEUE FULL REJECTS: {}", cpu,
                                stats.name, ::print_ratio(stats.mshr_occupancy_sum, stats.cycles), stats.mshr_full_cycles, stats.mshr_full_rejects,
                                stats.lower_queue_rejects));
  }

  return lines;
}

void champsim::modules::cache_module::format_json(const stats_type& stats, champsim::json_stat_builder& b)
{
  using hits_value_type = typename decltype(stats.hits)::value_type;
  using misses_value_type = typename decltype(stats.misses)::value_type;
  using miss_merge_value_type = typename decltype(stats.miss_merge)::value_type;
  using fill_value_type = typename decltype(stats.fill)::value_type;

  b.add("prefetch requested", stats.pf_requested)
   .add("prefetch issued", stats.pf_issued)
   .add("useful prefetch", stats.pf_useful)
   .add("useless prefetch", stats.pf_useless);

  // Discover CPU indices from the data itself (mirrors format_plaintext approach)
  std::vector<std::size_t> cpus;
  auto stat_keys = {stats.hits.get_keys(), stats.misses.get_keys(), stats.miss_merge.get_keys(), stats.fill.get_keys()};
  for (auto keys : stat_keys) {
    std::transform(std::begin(keys), std::end(keys), std::back_inserter(cpus), [](auto val) { return val.second; });
  }
  std::sort(std::begin(cpus), std::end(cpus));
  cpus.erase(std::unique(std::begin(cpus), std::end(cpus)), std::end(cpus));

  uint64_t total_downstream_demands = stats.fill.total();
  for (auto cpu : cpus)
    total_downstream_demands -= stats.fill.value_or(std::pair{access_type::PREFETCH, cpu}, fill_value_type{});

  b.add("miss latency", std::ceil(stats.total_miss_latency_cycles) / std::ceil(total_downstream_demands));

  b.add("cycles", stats.cycles)
   .add("evictions", stats.evictions)
   .add("dirty evictions", stats.dirty_evictions)
   .add("late prefetch", stats.pf_late)
   .add("MSHR occupancy sum", stats.mshr_occupancy_sum)
   .add("MSHR full cycles", stats.mshr_full_cycles)
   .add("MSHR full rejects", stats.mshr_full_rejects)
   .add("lower queue full rejects", stats.lower_queue_rejects);

  for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
    std::vector<hits_value_type> hit_vec;
    std::vector<misses_value_type> miss_vec;
    std::vector<miss_merge_value_type> miss_merge_vec;

    for (auto cpu : cpus) {
      hit_vec.push_back(stats.hits.value_or(std::pair{type, cpu}, hits_value_type{}));
      miss_vec.push_back(stats.misses.value_or(std::pair{type, cpu}, misses_value_type{}));
      miss_merge_vec.push_back(stats.miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{}));
    }

    auto sub = b.group(std::string{access_type_names.at(champsim::to_underlying(type))});
    sub.add("hit", hit_vec)
       .add("miss", miss_vec)
       .add("miss_merge", miss_merge_vec);
  }
}

// ============================================================================
// memory_controller_module formatting
// ============================================================================

std::vector<std::string> champsim::modules::memory_controller_module::format_plaintext(const stats_type& stats)
{
  std::vector<std::string> lines{};
  lines.push_back(fmt::format("{} RQ ROW_BUFFER_HIT: {:10}", stats.name, stats.RQ_ROW_BUFFER_HIT));
  lines.push_back(fmt::format("  ROW_BUFFER_MISS: {:10}", stats.RQ_ROW_BUFFER_MISS));
  lines.push_back(fmt::format("  AVG DBUS CONGESTED CYCLE: {}", ::print_ratio(stats.dbus_cycle_congested, stats.dbus_count_congested)));
  lines.push_back(fmt::format("{} WQ ROW_BUFFER_HIT: {:10}", stats.name, stats.WQ_ROW_BUFFER_HIT));
  lines.push_back(fmt::format("  ROW_BUFFER_MISS: {:10}", stats.WQ_ROW_BUFFER_MISS));
  lines.push_back(fmt::format("  FULL: {:10}", stats.WQ_FULL));

  if (stats.refresh_cycles > 0)
    lines.push_back(fmt::format("{} REFRESHES ISSUED: {:10}", stats.name, stats.refresh_cycles));
  else
    lines.push_back(fmt::format("{} REFRESHES ISSUED: -", stats.name));

  lines.push_back(fmt::format("{} READS: {:10} AVG READ LATENCY: {} cycles RQ FULL: {:10}", stats.name, stats.reads_completed,
                              ::print_ratio(stats.total_read_latency, stats.reads_completed), stats.RQ_FULL));
  lines.push_back(fmt::format("{} AVG RQ OCCUPANCY: {} AVG WQ OCCUPANCY: {} WRITE MODE SWITCHES: {:10}", stats.name,
                              ::print_ratio(stats.rq_occupancy_sum, stats.cycles), ::print_ratio(stats.wq_occupancy_sum, stats.cycles),
                              stats.write_mode_switches));

  return lines;
}

void champsim::modules::memory_controller_module::format_json(const stats_type& stats, champsim::json_stat_builder& b)
{
  b.add("RQ ROW_BUFFER_HIT", stats.RQ_ROW_BUFFER_HIT)
   .add("RQ ROW_BUFFER_MISS", stats.RQ_ROW_BUFFER_MISS)
   .add("WQ ROW_BUFFER_HIT", stats.WQ_ROW_BUFFER_HIT)
   .add("WQ ROW_BUFFER_MISS", stats.WQ_ROW_BUFFER_MISS)
   .add("AVG DBUS CONGESTED CYCLE", std::ceil(stats.dbus_cycle_congested) / std::ceil(stats.dbus_count_congested))
   .add("REFRESHES ISSUED", stats.refresh_cycles)
   .add("RQ FULL", stats.RQ_FULL)
   .add("READS", stats.reads_completed)
   .add("TOTAL READ LATENCY", stats.total_read_latency)
   .add("CYCLES", stats.cycles)
   .add("RQ OCCUPANCY SUM", stats.rq_occupancy_sum)
   .add("WQ OCCUPANCY SUM", stats.wq_occupancy_sum)
   .add("WRITE MODE SWITCHES", stats.write_mode_switches);
}
