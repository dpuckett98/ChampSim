#ifndef CORE_STATS_H
#define CORE_STATS_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "event_counter.h"
#include "instruction.h"

struct cpu_stats {
  std::string name;
  long long begin_instrs = 0;
  long long begin_cycles = 0;
  long long end_instrs = 0;
  long long end_cycles = 0;
  uint64_t total_rob_occupancy_at_branch_mispredict = 0;

  // cycles fdip spends on the wrong path, and the subset of those cycles it is stalled because the trace ran out of WP instrs.
  uint64_t fdip_wp_cycles = 0;
  uint64_t fdip_wp_stall_cycles = 0;

  // WP episodes (one per mispredict flush), how many of them ran out of WP instrs., and a histogram of the number of WP instrs. each episode
  // pulled from the workload source: # instrs. -> {episodes that did not run out, episodes that ran out}
  uint64_t wp_episodes = 0;
  uint64_t wp_episodes_ran_out = 0;
  std::map<uint64_t, std::pair<uint64_t, uint64_t>> wp_episode_instrs = {};

  champsim::stats::event_counter<branch_type> total_branch_types = {};
  champsim::stats::event_counter<branch_type> branch_type_misses = {};

  // number of instrs. that pass through each pipeline stage, keyed by stage name: {correct path, wrong path}
  std::map<std::string, std::pair<uint64_t, uint64_t>> stage_counts = {};

  // Performance-counter style events (cf. MEM_INST_RETIRED, BR_INST_RETIRED, EXE_ACTIVITY, INT_MISC on x86)
  // Retired instruction mix
  uint64_t retired_loads = 0;      // retired instrs. with at least one memory source operand
  uint64_t retired_load_ops = 0;   // memory source operands of retired instrs.
  uint64_t retired_stores = 0;     // retired instrs. with at least one memory destination operand
  uint64_t retired_store_ops = 0;  // memory destination operands of retired instrs.
  champsim::stats::event_counter<branch_type> retired_branches = {};
  champsim::stats::event_counter<branch_type> retired_taken_branches = {};

  // Histograms indexed by the number of instrs. handled in a cycle: element k counts the cycles in which k instrs. retired / issued / dispatched.
  // Issue and dispatch include wrong-path instrs.
  std::vector<uint64_t> retire_width_histogram = {};
  std::vector<uint64_t> issue_width_histogram = {};
  std::vector<uint64_t> dispatch_width_histogram = {};

  // Back-end occupancy, sampled at the start of every cycle: the sum over cycles (divide by the cycles for the average) and the cycles it was full
  uint64_t occupancy_cycles = 0; // cycles sampled
  uint64_t rob_occupancy_sum = 0;
  uint64_t lq_occupancy_sum = 0;
  uint64_t sq_occupancy_sum = 0;
  uint64_t rob_full_cycles = 0;
  uint64_t lq_full_cycles = 0;
  uint64_t sq_full_cycles = 0;

  // Branch misprediction recovery
  uint64_t recovery_cycles = 0;                // cycles from a flush until the first correct-path instr. dispatches
  uint64_t fetch_resteer_cycles = 0;           // correct-path cycles in which fetch waits out the misprediction penalty after a flush
  uint64_t mispredicts_resolved_at_decode = 0; // flushes started at decode (direct branches whose target or direction is known there)
  uint64_t mispredicts_resolved_at_execute = 0;

  // Store-to-load forwarding at dispatch, including wrong-path loads
  uint64_t loads_forwarded_from_store = 0; // the youngest older store to the address had executed, so its data was forwarded right away
  uint64_t loads_waiting_on_store = 0;     // the youngest older store to the address hadn't executed; the load waits for it and then forwards

  [[nodiscard]] auto instrs() const { return end_instrs - begin_instrs; }
  [[nodiscard]] auto cycles() const { return end_cycles - begin_cycles; }
};

cpu_stats operator-(cpu_stats lhs, cpu_stats rhs);

#endif
