#ifdef CHAMPSIM_MODULE
#define SET_ASIDE_CHAMPSIM_MODULE
#undef CHAMPSIM_MODULE
#endif

#ifndef PERFORMANCE_COUNTER_H
#define PERFORMANCE_COUNTER_H

#include "event_listener.h"
#include "util/algorithm.h"
#include "ooo_cpu.h"

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <iostream>
#include <cstdint>
#include <unordered_map>

class performance_counter : public EventListener {
  const O3_CPU* o3_cpu;
  
  std::unordered_map<std::string, uint64_t> cache_access;
  std::unordered_map<std::string, uint64_t> cache_misses;
  std::unordered_map<std::string, uint64_t> cache_stores;
  std::unordered_map<std::string, uint64_t> cache_store_misses;
  std::unordered_map<std::string, uint64_t> cache_loads;
  std::unordered_map<std::string, uint64_t> cache_load_misses;
  std::unordered_map<std::string, uint64_t> cache_rfos;
  std::unordered_map<std::string, uint64_t> cache_rfo_misses;
  std::unordered_map<std::string, uint64_t> cache_prefetches;
  std::unordered_map<std::string, uint64_t> cache_prefetch_misses;
  std::unordered_map<std::string, uint64_t> cache_translations;
  std::unordered_map<std::string, uint64_t> cache_translation_misses;
  std::unordered_map<std::string, uint64_t> num_branches_type;
  std::unordered_map<std::string, uint64_t> num_misses_branch_type;
  std::unordered_map<std::string, uint64_t> retired_instr_type_count;
  
  std::unordered_map<std::string, uint64_t> cache_waiting_on_mshr;
  std::unordered_map<std::string, uint64_t> cache_waiting_on_mshr_prefetch;
  std::unordered_map<std::string, uint64_t> cache_hit_mshr;
  std::unordered_map<std::string, uint64_t> cache_hit_mshr_prefetch;
  
  long num_branches = 0;
  
  long total_num_dependencies = 0;
  long num_instrs_scheduled = 0;
  
  uint64_t slots_wrong_path = 0;
  
  uint64_t sum_l1d_misses = 0;
  uint64_t cycles_with_l1d_misses = 0;
  
  uint64_t num_early_resteers = 0;
  
  // buffer states
  uint64_t cycles_ifetch_buffer_full;
  uint64_t cycles_ifetch_buffer_empty;
  uint64_t cycles_dispatch_buffer_full;
  uint64_t cycles_dispatch_buffer_empty;
  uint64_t cycles_decode_buffer_full;
  uint64_t cycles_decode_buffer_empty;
  uint64_t cycles_rob_full;
  uint64_t cycles_rob_empty;
  uint64_t cycles_dib_hit_buffer_full;
  uint64_t cycles_dib_hit_buffer_empty;
  uint64_t cycles_lq_full;
  uint64_t cycles_lq_empty;
  uint64_t cycles_sq_full;
  uint64_t cycles_sq_empty;
  std::unordered_map<std::string, uint64_t> cycles_mshr_full;
  std::unordered_map<std::string, uint64_t> cycles_mshr_empty;
  
  // pipeline stage states
  uint64_t cycles_dib_idle = 0;
  uint64_t cycles_fetch_idle = 0;
  uint64_t last_fetch_cycle = 0;
  uint64_t cycles_decode_idle = 0;
  uint64_t cycles_dispatch_idle = 0;
  uint64_t cycles_schedule_idle = 0;
  uint64_t cycles_issue_idle = 0;
  uint64_t cycles_store_idle = 0;
  uint64_t cycles_load_idle = 0;
  uint64_t cycles_complete_idle = 0;
  uint64_t cycles_retire_idle_empty = 0; // no instructions retired & rob is empty
  uint64_t cycles_retire_idle_other = 0; // no instrs retired, rob isn't empty
  
  uint64_t cycles_schedule_stalled_SQ = 0;
  uint64_t cycles_schedule_stalled_LQ = 0;
  bool demand_miss_present = false;
  uint64_t cycles_issue_stalled_l1d_miss = 0;
  
  bool last_cycle_rs_empty = true;
  uint64_t periods_rs_empty = 0;
  
  uint64_t instrs_from_dib = 0;
  
  long long total_retired_instrs = 0;
  int num_retired_instrs = 0;
  int base_retired_instrs = 0;
  long curr_cycles = 0;
  bool in_warmup = true;
  long long interval_num = 0;
  int printout_interval = 10000;

private:
  void print(long long interval, std::string name, uint64_t val) {
    fmt::print("interval {} {} {}\n", interval, name, val);
  }
  
  void print_d(long long interval, std::string name, double val) {
    fmt::print("interval {} {} {}\n", interval, name, val);
  }

  void rst_ctrs() {
    std::vector<std::string> cache_names = {"cpu0_L1I", "cpu0_L1D", "cpu0_L2C", "LLC", "cpu0_ITLB", "cpu0_DTLB", "cpu0_STLB"};
    for (auto c : cache_names) {
      cache_access[c] = 0;
      cache_misses[c] = 0;
      cache_stores[c] = 0;
      cache_store_misses[c] = 0;
      cache_load_misses[c] = 0;
      cache_loads[c] = 0;
      cache_rfo_misses[c] = 0;
      cache_rfos[c] = 0;
      cache_prefetch_misses[c] = 0;
      cache_prefetches[c] = 0;
      cache_translation_misses[c] = 0;
      cache_translations[c] = 0;
      cache_waiting_on_mshr[c] = 0;
      cache_waiting_on_mshr_prefetch[c] = 0;
      cache_hit_mshr[c] = 0;
      cache_hit_mshr_prefetch[c] = 0;
      cycles_mshr_empty[c] = 0;
      cycles_mshr_full[c] = 0;
    }
    num_branches_type.clear();
    num_misses_branch_type.clear();
    num_misses_branch_type.clear();
    retired_instr_type_count.clear();
    num_branches = 0;
    total_num_dependencies = 0;
    num_instrs_scheduled = 0;
    slots_wrong_path = 0;
    sum_l1d_misses = 0;
    cycles_with_l1d_misses = 0;
    num_early_resteers = 0;
    cycles_ifetch_buffer_full = 0;
    cycles_ifetch_buffer_empty = 0;
    cycles_dispatch_buffer_full = 0;
    cycles_dispatch_buffer_empty = 0;
    cycles_decode_buffer_full = 0;
    cycles_decode_buffer_empty = 0;
    cycles_rob_full = 0;
    cycles_rob_empty = 0;
    cycles_dib_hit_buffer_full = 0;
    cycles_dib_hit_buffer_empty = 0;
    cycles_lq_full = 0;
    cycles_lq_empty = 0;
    cycles_sq_full = 0;
    cycles_sq_empty = 0;
    cycles_dib_idle = 0;
    cycles_fetch_idle = 0;
    cycles_decode_idle = 0;
    cycles_dispatch_idle = 0;
    cycles_schedule_idle = 0;
    cycles_issue_idle = 0;
    cycles_store_idle = 0;
    cycles_load_idle = 0;
    cycles_complete_idle = 0;
    cycles_retire_idle_empty = 0;
    cycles_retire_idle_other = 0;
    cycles_schedule_stalled_SQ = 0;
    cycles_schedule_stalled_LQ = 0;
    cycles_issue_stalled_l1d_miss = 0;
    periods_rs_empty = 0;
    instrs_from_dib = 0;
  }

public:
  performance_counter() {
    rst_ctrs();
  }

  void process_event(event eventType, void* data) {
    if (eventType == event::BEGIN_PHASE) {
      BEGIN_PHASE_data* b_data = static_cast<BEGIN_PHASE_data *>(data);
      in_warmup = b_data->is_warmup;
      return;
    }
    if (in_warmup) {
      return;
    }
    if (eventType == event::INITIALIZE) {
      INITIALIZE_data* i_data = static_cast<INITIALIZE_data *>(data);
      if (i_data->fetch_stopped) {
        slots_wrong_path += (long int)o3_cpu->FETCH_WIDTH - std::distance(i_data->begin, i_data->end);
      }
    } else if (eventType == event::RETIRE) {
      RETIRE_data* r_data = static_cast<RETIRE_data *>(data);
      // pipeline state
      if (std::distance(r_data->begin, r_data->end) == 0) {
        if (o3_cpu->ROB.empty()) {
          cycles_retire_idle_empty++;
        } else {
          cycles_retire_idle_other++;
        }
      }
      // num retired instructions
      num_retired_instrs += std::distance(r_data->begin, r_data->end);
      total_retired_instrs += std::distance(r_data->begin, r_data->end);
      // retired instruction mix
      for (auto it = r_data->begin; it != r_data->end; ++it) {
        const auto& instr = *it;
        if (instr.is_branch) {
          retired_instr_type_count["branches"]++;
        }
        retired_instr_type_count["stores"] += instr.destination_memory.size();
        retired_instr_type_count["loads"] += instr.source_memory.size();
      }
      // various buffer states
      if (o3_cpu->IFETCH_BUFFER.empty()) {
        cycles_ifetch_buffer_empty++;
      } else if (o3_cpu->IFETCH_BUFFER.size() == o3_cpu->IFETCH_BUFFER_SIZE) {
        cycles_ifetch_buffer_full++;
      }
      if (o3_cpu->DISPATCH_BUFFER.empty()) {
        cycles_dispatch_buffer_empty++;
      } else if (o3_cpu->DISPATCH_BUFFER.size() == o3_cpu->DISPATCH_BUFFER_SIZE) {
        cycles_dispatch_buffer_full++;
      }
      if (o3_cpu->DECODE_BUFFER.empty()) {
        cycles_decode_buffer_empty++;
      } else if (o3_cpu->DECODE_BUFFER.size() == o3_cpu->DECODE_BUFFER_SIZE) {
        cycles_decode_buffer_full++;
      }
      if (o3_cpu->ROB.empty()) {
        cycles_rob_empty++;
      } else if (o3_cpu->ROB.size() == o3_cpu->ROB_SIZE) {
        cycles_rob_full++;
      }
      if (o3_cpu->DIB_HIT_BUFFER.empty()) {
        cycles_dib_hit_buffer_empty++;
      } else if (o3_cpu->DIB_HIT_BUFFER.size() == o3_cpu->DIB_HIT_BUFFER_SIZE) {
        cycles_dib_hit_buffer_full++;
      }
      if (o3_cpu->LQ.empty()) {
        cycles_lq_empty++;
      } else if (o3_cpu->LQ.size() == o3_cpu->SQ_SIZE) {
        cycles_lq_full++;
      }
      if (o3_cpu->SQ.empty()) {
        cycles_sq_empty++;
      } else if (o3_cpu->SQ.size() == o3_cpu->SQ_SIZE) {
        cycles_sq_full++;
      }
      // printout
      if (num_retired_instrs >= printout_interval) {
        // header
        print(interval_num, "total_instr", total_retired_instrs);
        print(interval_num, "cycles", curr_cycles);
        print(interval_num, "instrs", num_retired_instrs - base_retired_instrs);
        // caches
        for (const auto& [cache, count] : cache_access) {
          print(interval_num, "total_accesses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_misses) {
          print(interval_num, "total_misses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_loads) {
          print(interval_num, "load_accesses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_load_misses) {
          print(interval_num, "load_misses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_stores) {
          print(interval_num, "store_accesses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_store_misses) {
          print(interval_num, "store_misses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_rfos) {
          print(interval_num, "rfo_accesses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_rfo_misses) {
          print(interval_num, "rfo_misses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_prefetches) {
          print(interval_num, "prefetch_accesses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_prefetch_misses) {
          print(interval_num, "prefetch_misses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_translations) {
          print(interval_num, "translation_accesses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_translation_misses) {
          print(interval_num, "translation_misses_" + cache, count);
        }
        for (const auto& [cache, count] : cache_waiting_on_mshr) {
          print(interval_num, "demand_waiting_on_mshr_" + cache, count);
        }
        for (const auto& [cache, count] : cache_waiting_on_mshr_prefetch) {
          print(interval_num, "prefetch_waiting_on_mshr_" + cache, count);
        }
        for (const auto& [cache, count] : cache_hit_mshr) {
          print(interval_num, "demand_hit_mshr_" + cache, count);
        }
        for (const auto& [cache, count] : cache_hit_mshr_prefetch) {
          print(interval_num, "prefetch_hit_mshr_" + cache, count);
        }
        if (cycles_with_l1d_misses > 0) {
          print_d(interval_num, "avg_l1d_mlp", (double)sum_l1d_misses / (double)cycles_with_l1d_misses);
        }
        // branches
        for(const auto& [branch, count] : num_branches_type){
          print(interval_num, branch + "_count", count);
        }
        for(const auto& [branch, count] : num_misses_branch_type){
          print(interval_num, branch + "_mispredictions", count);
        }
        print(interval_num, "slots_wrong_path", slots_wrong_path);
        // mix of other (non-branch) instructions
        for(const auto& [name, count] : retired_instr_type_count){
          print(interval_num, name, count);
        }
        // pipeline stages
        print(interval_num, "early_resteers", num_early_resteers);
        print(interval_num, "cycles_ifetch_buffer_empty", cycles_ifetch_buffer_empty);
        print(interval_num, "cycles_ifetch_buffer_full", cycles_ifetch_buffer_full);
        print(interval_num, "cycles_decode_buffer_empty", cycles_decode_buffer_empty);
        print(interval_num, "cycles_decode_buffer_full", cycles_decode_buffer_full);
        print(interval_num, "cycles_dib_hit_buffer_empty", cycles_dib_hit_buffer_empty);
        print(interval_num, "cycles_dib_hit_buffer_full", cycles_dib_hit_buffer_full);
        print(interval_num, "cycles_dispatch_buffer_empty", cycles_dispatch_buffer_empty);
        print(interval_num, "cycles_dispatch_buffer_full", cycles_dispatch_buffer_full);
        print(interval_num, "cycles_rob_empty", cycles_rob_empty);
        print(interval_num, "cycles_rob_full", cycles_rob_full);
        print(interval_num, "cycles_lq_empty", cycles_lq_empty);
        print(interval_num, "cycles_lq_full", cycles_lq_full);
        print(interval_num, "cycles_sq_empty", cycles_sq_empty);
        print(interval_num, "cycles_sq_full", cycles_sq_full);
        for (const auto& [cache, count] : cycles_mshr_empty) {
          print(interval_num, "cycles_mshr_empty_" + cache, count);
        }
        for (const auto& [cache, count] : cycles_mshr_full) {
          print(interval_num, "cycles_mshr_full_" + cache, count);
        }
        // pipeline states
        if (last_fetch_cycle + 1 < r_data->cycle) {
          cycles_fetch_idle += r_data->cycle - last_fetch_cycle - 1;
        }
        last_fetch_cycle = r_data->cycle;
        print(interval_num, "cycles_fetch_idle", cycles_fetch_idle);
        print(interval_num, "cycles_decode_idle", cycles_decode_idle);
        print(interval_num, "cycles_dib_idle", cycles_dib_idle);
        print(interval_num, "cycles_dispatch_idle", cycles_dispatch_idle);
        print(interval_num, "cycles_schedule_idle", cycles_schedule_idle);
        print(interval_num, "cycles_schedule_stalled_LQ", cycles_schedule_stalled_LQ);
        print(interval_num, "cycles_schedule_stalled_SQ", cycles_schedule_stalled_SQ);
        print(interval_num, "cycles_issue_idle", cycles_issue_idle);
        print(interval_num, "cycles_issue_stalled_l1d_miss", cycles_issue_stalled_l1d_miss);
        print(interval_num, "cycles_store_idle", cycles_store_idle);
        print(interval_num, "cycles_load_idle", cycles_load_idle);
        print(interval_num, "cycles_complete_idle", cycles_complete_idle);
        print(interval_num, "cycles_retire_idle_rob_empty", cycles_retire_idle_empty);
        print(interval_num, "cycles_retire_idle_rob_not_empty", cycles_retire_idle_other);
        print(interval_num, "periods_rs_empty", periods_rs_empty);
        print(interval_num, "instrs_from_dib", instrs_from_dib);
        // misc TODO
        
        
        // prep for next interval
        num_retired_instrs = num_retired_instrs % printout_interval;
        base_retired_instrs = num_retired_instrs;
        curr_cycles = 0;
        interval_num++;
        rst_ctrs();
      }
    } else if (eventType == event::PRE_CYCLE) {
      PRE_CYCLE_data* p_data = static_cast<PRE_CYCLE_data *>(data);
      curr_cycles++;
      o3_cpu = p_data->o3_cpu;
    } else if (eventType == event::END) {
      fmt::print("##END##\n"); // this is a trigger for the post-processor to see if the trace finished running or not
    } else if (eventType == event::CACHE_TRY_HIT) {
      CACHE_TRY_HIT_data* c_data = static_cast<CACHE_TRY_HIT_data*>(data);
      cache_access[c_data->NAME]++;
      if (!c_data->hit) {
        cache_misses[c_data->NAME]++;
      }
      if (c_data->type == access_type::WRITE) {
        cache_stores[c_data->NAME]++;
        if (!c_data->hit) {
          cache_store_misses[c_data->NAME]++;
        }
      } else if (c_data->type == access_type::LOAD) {
        cache_loads[c_data->NAME]++;
        if (!c_data->hit) {
          cache_load_misses[c_data->NAME]++;
        }
      } else if (c_data->type == access_type::RFO) {
        cache_rfos[c_data->NAME]++;
        if (!c_data->hit) {
          cache_rfo_misses[c_data->NAME]++;
        }
      } else if (c_data->type == access_type::PREFETCH) {
        cache_prefetches[c_data->NAME]++;
        if (!c_data->hit) {
          cache_prefetch_misses[c_data->NAME]++;
        }
      } else if (c_data->type == access_type::TRANSLATION) {
        cache_translations[c_data->NAME]++;
        if (!c_data->hit) {
          cache_translation_misses[c_data->NAME]++;
        }
      }
    } else if (eventType == event::BRANCH) {
      num_branches++;
      BRANCH_data* b_data = static_cast<BRANCH_data*>(data);
      num_branches_type[branch_type_to_string(b_data->instr->branch)]++;
      if (b_data->instr->branch_mispredicted) {
        num_misses_branch_type[branch_type_to_string(b_data->instr->branch)]++;
      }
    } else if (eventType == event::CACHE_NO_MSHR_ON_MISS) {
      CACHE_NO_MSHR_ON_MISS_data* c_data = static_cast<CACHE_NO_MSHR_ON_MISS_data *>(data);
      if (c_data->type == access_type::PREFETCH) {
        cache_waiting_on_mshr_prefetch[c_data->NAME]++;
      } else {
        cache_waiting_on_mshr[c_data->NAME]++;
      }
    } else if (eventType == event::CACHE_HIT_MSHR) { // this event was totally ignored due to a typo --> all parts of this event were actually counted as CACHE_NO_MSHR_ON_MISS (this may actually be significant --> I'd like to re-run the performance counter data, or at least drop the relevant columns from the dataset & re-train); the actual data is less precise & less useful than it would be otherwise
      CACHE_HIT_MSHR_data* c_data = static_cast<CACHE_HIT_MSHR_data *>(data);
      if (c_data->type == access_type::PREFETCH) {
        cache_hit_mshr_prefetch[c_data->NAME]++;
      } else {
        cache_hit_mshr[c_data->NAME]++;
      }
    } else if (eventType == event::CACHE_OPERATE) {
      CACHE_OPERATE_data* c_data = static_cast<CACHE_OPERATE_data *>(data);
      if (c_data->NAME == "cpu0_L1D") {
        demand_miss_present = false;
        for (auto mshr : c_data->MSHR) {
          if (mshr.type == access_type::LOAD) {
            demand_miss_present = true;
            sum_l1d_misses++;
          }
        }
        if (demand_miss_present) {
          cycles_with_l1d_misses++;
        }
      }
      if (c_data->MSHR.empty()) {
        cycles_mshr_empty[c_data->NAME]++;
      } else if (c_data->MSHR.size() == c_data->cache->MSHR_SIZE) {
        cycles_mshr_full[c_data->NAME]++;
      }
    } else if (eventType == event::DO_DECODE) {
      DO_DECODE_data* d_data = static_cast<DO_DECODE_data *>(data);
      if (d_data->early_resteer) {
        num_early_resteers++;
      }
    } else if (eventType == event::CHECK_DIB) {
      CHECK_DIB_data* c_data = static_cast<CHECK_DIB_data *>(data);
      if (std::distance(c_data->begin, c_data->end) == 0) {
        cycles_dib_idle++;
      }
    } else if (eventType == event::START_FETCH) { // just looking at requesting data from L1I, not receiving data from L1I
      START_FETCH_data* c_data = static_cast<START_FETCH_data *>(data);
      if (c_data->cycle > last_fetch_cycle + 1) {
        cycles_fetch_idle += c_data->cycle - last_fetch_cycle - 1;
      }
      last_fetch_cycle = c_data->cycle;
    } else if (eventType == event::START_DECODE) {
      START_DECODE_data* c_data = static_cast<START_DECODE_data *>(data);
      if (std::distance(c_data->begin, c_data->end) == 0) {
        cycles_decode_idle++;
      }
    } else if (eventType == event::START_DISPATCH) {
      START_DISPATCH_data* c_data = static_cast<START_DISPATCH_data *>(data);
      if (std::distance(c_data->begin, c_data->end) == 0) {
        cycles_dispatch_idle++;
      }
      instrs_from_dib += c_data->instrs_from_dib;
    } else if (eventType == event::START_SCHEDULE) {
      START_SCHEDULE_data* c_data = static_cast<START_SCHEDULE_data *>(data);
      if (std::distance(c_data->begin, c_data->end) == 0) {
        cycles_schedule_idle++;
      }
      if (c_data->stop_cause == 1) {
        cycles_schedule_stalled_LQ++;
      } else if (c_data->stop_cause == 2) {
        cycles_schedule_stalled_SQ++;
      }
    } else if (eventType == event::START_EXECUTE) {
      START_EXECUTE_data* c_data = static_cast<START_EXECUTE_data *>(data);
      if (std::distance(c_data->begin, c_data->end) == 0) {
        cycles_issue_idle++;
        if (demand_miss_present) {
          cycles_issue_stalled_l1d_miss++;
        }
        bool rs_empty = true;
        for (auto instr : o3_cpu->ROB) {
          if (instr.scheduled && !instr.executed) {
            rs_empty = false;
            break;
          }
        }
        if (!last_cycle_rs_empty && rs_empty) { // track the start of each period
          periods_rs_empty++;
        }
        last_cycle_rs_empty = rs_empty;
      }
    } else if (eventType == event::END_EXECUTE) {
      END_EXECUTE_data* c_data = static_cast<END_EXECUTE_data *>(data);
      if (std::distance(c_data->begin, c_data->end) == 0) {
        cycles_complete_idle++;
      }
    } else if (eventType == event::OPERATE_LSQ) {
      OPERATE_LSQ_data* c_data = static_cast<OPERATE_LSQ_data *>(data);
      if (c_data->stores_issued == 0) {
        cycles_store_idle++;
      }
      if (c_data->loads_issued == 0) {
        cycles_load_idle++;
      }
    }
  }

  std::string branch_type_to_string(branch_type type) {
    switch(type){
      case BRANCH_DIRECT_JUMP: 
        return "BRANCH_DIRECT_JUMP";
      case BRANCH_INDIRECT:
        return "BRANCH_INDIRECT";
      case BRANCH_CONDITIONAL:
        return "BRANCH_CONDITIONAL";
      case BRANCH_DIRECT_CALL:
        return "BRANCH_DIRECT_CALL";
      case BRANCH_INDIRECT_CALL:
        return "BRANCH_INDIRECT_CALL";
      case BRANCH_RETURN:
        return "BRANCH_RETURN";
      case BRANCH_OTHER:
        return "BRANCH_OTHER";
      default:
        return "error";
    }
  }
};

#endif

#ifdef SET_ASIDE_CHAMPSIM_MODULE
#undef SET_ASIDE_CHAMPSIM_MODULE
#define CHAMPSIM_MODULE
#endif

