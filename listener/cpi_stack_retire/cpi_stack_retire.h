#ifdef CHAMPSIM_MODULE
#define SET_ASIDE_CHAMPSIM_MODULE
#undef CHAMPSIM_MODULE
#endif

#ifndef CPI_STACK_H
#define CPI_STACK_H

#include "event_listener.h"
#include "util/algorithm.h"

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

class cpi_stack_retire : public EventListener {
  long print_cycles = 10000;
  long curr_cycles = 0;

  long computing_cycles = 0;
  long stalled_cycles = 0;
  long stalled_pt_miss_cycles = 0;
  long flushed_cycles = 0;
  long drained_cycles = 0;
  long drained_streak_cycles = 0; 
 
  uint64_t last_instr_id = 99999;
  std::vector<long> computing_counts;
  std::map<std::string, long> stalled_cache_miss_counts;
  std::map<std::string, long> drained_cache_miss_counts;
  std::vector<std::pair<uint64_t, std::string> > cache_misses;
  std::vector<uint64_t> itlb_cache_misses;

  bool in_warmup = false;
  bool has_previous_instruction = false;
  bool previous_instruction_mispredicted_branch = false;

  std::deque<ooo_model_instr>* ROB;

  void process_event(event eventType, void* data) {
    if (eventType == event::BEGIN_PHASE) {
      BEGIN_PHASE_data* b_data = static_cast<BEGIN_PHASE_data *>(data);
      in_warmup = b_data->is_warmup;
      return;
    }
    if (in_warmup) {
      return;
    }
    if (eventType == event::CACHE_TRY_HIT) {
      CACHE_TRY_HIT_data* c_data = static_cast<CACHE_TRY_HIT_data *>(data);
      if (!c_data->hit && c_data->instr_id > last_instr_id) {
        /*if (c_data->NAME == "cpu0_ITLB") {
		fmt::print("ITLB miss at: {}\n", c_data->instr_id);
	}*/
	cache_misses.push_back(std::make_pair(c_data->instr_id, c_data->NAME));
	if (c_data->NAME.find("ITLB") != std::string::npos) {
          itlb_cache_misses.push_back(c_data->instr_id);
	}
      }
    } else if (eventType == event::PRE_CYCLE) {
      curr_cycles++;
      PRE_CYCLE_data* p_data = static_cast<PRE_CYCLE_data *>(data);
      ROB = p_data->ROB;
    } else if (eventType == event::RETIRE) {
      RETIRE_data* r_data = static_cast<RETIRE_data *>(data);
      if (std::distance(r_data->begin, r_data->end) == 0) {
        if (ROB->empty()) {
          if (previous_instruction_mispredicted_branch) {
            // if no instructions retired, ROB is empty, and previous instruction was a mispredicted branch, then it's flushed
            flushed_cycles++;
          } else {
            // if no instructions retired, ROB is empty, and previous instruction wasn't a mispredicted branch, then we assume it's drained from an icache miss
            // this assumes the only time the ROB is flushed is from a branch misprediction, which isn't true
            // this counts some startup cycles as drained cycles
            drained_cycles++;
            drained_streak_cycles++;
          }
        } else {
          // if no instructions retired but ROB isn't empty, then it's stalled
          // this counts some startup cycles as stalled cycles
          stalled_cycles++;

          std::string name = "";
	  bool recording = false;
          for (auto cm : cache_misses) {
            // only include this cache miss if the instructions match and it's not a duplicate
            if (cm.first == ROB->front().instr_id && name.find(cm.second) == std::string::npos) {
              // only start recording if it's a dcache miss
	      if (!recording && (cm.second == "cpu0_DTLB" || cm.second == "cpu0_L1D")) {
                recording = true;
	      }
	      if (recording) {
                if (name != "") {
                  name += "-";
                }
                name += cm.second;
	      }
            }
          }
          if (name != "") {
            if (stalled_cache_miss_counts.count(name)) {
              stalled_cache_miss_counts[name]++;
            } else {
              stalled_cache_miss_counts[name] = 1;
            }
          }
        }
      } else {
        // if any instructions retired this cycle, in computing state
        computing_cycles++;

        // remove cache misses from committed instructions and set previous_instruction_cache_miss
        std::string first_name = "";
	for (auto instr = r_data->begin; instr != r_data->end; instr++) {
          int idx = 0;
          std::vector<int> to_remove = std::vector<int>();
          bool done_recording = false;
	  for (auto cm : cache_misses) {
            // only count if instr_id matches and it's not a duplicate
            if (cm.first == instr->instr_id && first_name.find(cm.second) == std::string::npos) {
              to_remove.push_back(idx);
	      if (!done_recording && (cm.second == "cpu0_DTLB" || cm.second == "cpu0_L1D")) {
                done_recording = true;
	      }
	      if (!done_recording) {
                if (instr->instr_id == r_data->begin->instr_id) {
                  if (first_name != "") {
                    first_name += "-";
                  }
                  first_name += cm.second;
                }
	      }
            }
            idx++;
          }
          for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
            cache_misses.erase(cache_misses.begin() + *it);
          }
        }
        if (drained_streak_cycles > 0 && first_name != "") {
          if (drained_cache_miss_counts.count(first_name)) {
            drained_cache_miss_counts[first_name] += drained_streak_cycles;
          } else {
            drained_cache_miss_counts[first_name] = drained_streak_cycles;
          }
        }
        drained_streak_cycles = 0;

        while (cmp_greater(std::distance(r_data->begin, r_data->end), computing_counts.size())) {
          computing_counts.push_back(0);
        }
        computing_counts[std::distance(r_data->begin, r_data->end) - 1]++;

        // update previous instruction data
        has_previous_instruction = true;
        previous_instruction_mispredicted_branch = std::prev(r_data->end)->branch_mispredicted; //r_data->instrs[std::distance(r_data->begin, r_data->end)-1].branch_mispredicted;
        last_instr_id = std::prev(r_data->end)->instr_id; //r_data->instrs[std::distance(r_data->begin, r_data->end)-1].instr_id;
      }
    } else if (curr_cycles == print_cycles or eventType == event::END) {
      curr_cycles = 0;

      fmt::print("CPI Stacks Retire:\n");
      fmt::print("Computing cycles: {}\n", computing_cycles);
      for (unsigned long i = 0; i < computing_counts.size(); i++) {
        fmt::print("  Retired {}: {}", i + 1, computing_counts[i]);
      }
      fmt::print("\nStalled cycles: {}\n", stalled_cycles);
      // sort and print cache misses
      std::vector<std::pair<std::string, int> > to_sort;
      std::copy(stalled_cache_miss_counts.begin(), stalled_cache_miss_counts.end(), std::back_inserter(to_sort));
      std::sort(to_sort.begin(), to_sort.end(), [](auto &left, auto &right) {
        return left.second > right.second;
      });
      for (const auto& [name, count] : to_sort) {
        fmt::print("  {}: {}", name, count);
      }
      fmt::print("\nFlushed cycles: {}\n", flushed_cycles);
      fmt::print("Drained cycles: {}\n", drained_cycles);
      
      // sort and print cache misses
      to_sort = std::vector<std::pair<std::string, int> >();
      std::copy(drained_cache_miss_counts.begin(), drained_cache_miss_counts.end(), std::back_inserter(to_sort));
      std::sort(to_sort.begin(), to_sort.end(), [](auto &left, auto &right) {
        return left.second > right.second;
      });
      for (const auto& [name, count] : to_sort) {
        fmt::print("  {}: {}", name, count);
      }
      fmt::print("\n\nUnaccounted cache misses: {}\n", cache_misses.size());
    }
  }
};

#endif

#ifdef SET_ASIDE_CHAMPSIM_MODULE
#undef SET_ASIDE_CHAMPSIM_MODULE
#define CHAMPSIM_MODULE
#endif
