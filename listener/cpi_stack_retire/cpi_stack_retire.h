#ifdef CHAMPSIM_MODULE
#define SET_ASIDE_CHAMPSIM_MODULE
#undef CHAMPSIM_MODULE
#endif

#ifndef CPI_STACK_RETIRE_H
#define CPI_STACK_RETIRE_H

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
  long refilling_cycles = 0; // cycles where you're stalled and were previously drained

  long last_computing_cycles = 0;
  long last_stalled_cycles = 0;
  long last_flushed_cycles = 0;
  long last_drained_cycles = 0;
  std::vector<long> last_computing_counts;
  std::map<std::string, long> last_stalled_cache_miss_counts;
  std::map<std::string, long> last_drained_cache_miss_counts;

  uint64_t last_instr_id = 99999;
  std::vector<long> computing_counts;
  std::map<std::string, long> stalled_cache_miss_counts;
  std::map<std::string, long> drained_cache_miss_counts;
  std::vector<std::pair<uint64_t, std::string> > cache_misses;

  bool in_warmup = false;
  bool has_previous_instruction = false;
  bool previous_instruction_mispredicted_branch = false;

  std::deque<ooo_model_instr>* ROB;

  // this function just prints the results (e.g., cycle counts) from the last print_cycles cycles
  void print_set_results() {
    // calculate results for this set
    std::vector<long> cc_diff = std::vector<long>(computing_counts);
    for (int i = 0; i < last_computing_counts.size(); i++) {
      cc_diff[i] -= last_computing_counts[i];
    }
    std::map<std::string, long> scmc_diff = std::map<std::string, long>(stalled_cache_miss_counts);
    for (auto const& [name, count] : last_stalled_cache_miss_counts) {
      scmc_diff[name] -= count;
    }
    std::map<std::string, long> dcmc_diff = std::map<std::string, long>(drained_cache_miss_counts);
    for (auto const& [name, count] : last_drained_cache_miss_counts) {
      dcmc_diff[name] -= count;
    }

    // print results
    print_results(curr_cycles, computing_cycles - last_computing_cycles, stalled_cycles - last_stalled_cycles, flushed_cycles - last_flushed_cycles, drained_cycles - last_drained_cycles, cc_diff, scmc_diff, dcmc_diff);

    // update last counts
    last_computing_cycles = computing_cycles;
    last_stalled_cycles = stalled_cycles;
    last_flushed_cycles = flushed_cycles;
    last_drained_cycles = drained_cycles;
    last_computing_counts = std::vector<long>(computing_counts);
    for (auto const& [name, count] : stalled_cache_miss_counts) {
      last_stalled_cache_miss_counts[name] = count;
    }
    for (auto const& [name, count] : drained_cache_miss_counts) {
      last_drained_cache_miss_counts[name] = count;
    }
  }

  void print_results(long _curr_cycles, long _computing_cycles, long _stalled_cycles, long _flushed_cycles, long _drained_cycles, const std::vector<long>& _computing_counts, const std::map<std::string, long>& _stalled_cache_miss_counts, const std::map<std::string, long>& _drained_cache_miss_counts) {
    // header
    fmt::print("\nCPI Stacks Retire @ {}\n", _curr_cycles);

    // computing
    fmt::print("Computing cycles: {}\n", _computing_cycles);
    for (unsigned long i = 0; i < _computing_counts.size(); i++) {
      fmt::print("  Retired {}: {}", i + 1, _computing_counts[i]);
    }

    // stalled
    fmt::print("\nStalled cycles: {}\n", _stalled_cycles);
    // sort and print cache misses
    std::vector<std::pair<std::string, int> > to_sort;
    std::copy(_stalled_cache_miss_counts.begin(), _stalled_cache_miss_counts.end(), std::back_inserter(to_sort));
    std::sort(to_sort.begin(), to_sort.end(), [](auto &left, auto &right) {
      return left.second > right.second;
    });
    for (const auto& [name, count] : to_sort) {
      fmt::print("  {}: {}", name, count);
    }
    fmt::print("\n");

    // flushed
    fmt::print("Flushed cycles: {}\n", _flushed_cycles);

    // drained
    fmt::print("Drained cycles: {}\n", _drained_cycles);
    // sort and print cache misses
    to_sort = std::vector<std::pair<std::string, int> >();
    std::copy(_drained_cache_miss_counts.begin(), _drained_cache_miss_counts.end(), std::back_inserter(to_sort));
    std::sort(to_sort.begin(), to_sort.end(), [](auto &left, auto &right) {
      return left.second > right.second;
    });
    for (const auto& [name, count] : to_sort) {
      fmt::print("  {}: {}", name, count);
    }
    fmt::print("\n");
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
    if (eventType == event::CACHE_TRY_HIT) {
      CACHE_TRY_HIT_data* c_data = static_cast<CACHE_TRY_HIT_data *>(data);
      if (!c_data->hit && c_data->instr_id > last_instr_id) {
	cache_misses.push_back(std::make_pair(c_data->instr_id, c_data->NAME));
      }
    } else if (eventType == event::PRE_CYCLE) {
      curr_cycles++;
      PRE_CYCLE_data* p_data = static_cast<PRE_CYCLE_data *>(data);
      ROB = p_data->ROB;

      // print results
      if (curr_cycles % print_cycles == 0) {
        print_set_results();
	//print_results(curr_cycles, computing_cycles, stalled_cycles, flushed_cycles, drained_cycles, computing_counts, stalled_cache_miss_counts, drained_cache_miss_counts);
      }
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

	  if (drained_streak_cycles > 0) {
            refilling_cycles++;
	  }

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

        // find cache miss and increase drained_cache_miss_counts if the ROB was previously drained
	if (drained_streak_cycles > 0) {
          std::string name = "";
	  for (auto cm : cache_misses) {
	    // skip iteration unless the cache miss was caused by the first instruction committed this cycle
            if (cm.first != r_data->begin->instr_id) {
              continue;
	    }
	    // skip iteration if the cache miss is a duplicate
	    if (name.find(cm.second) != std::string::npos) {
              continue;
	    }
            // stop recording the cache miss names once you get to the data cache misses
	    if (cm.second == "cpu0_DTLB" || cm.second == "cpu0_L1D") {
              break;
	    }
            // append name of cache that had a miss to name
   	    if (name != "") {
              name += "-";
	    }
	    name += cm.second;
	  }

	  if (name != "") {
            if (drained_cache_miss_counts.count(name)) {
              drained_cache_miss_counts[name] += drained_streak_cycles;
            } else {
              drained_cache_miss_counts[name] = drained_streak_cycles;
            }
          }
          drained_streak_cycles = 0;
        }

	// remove cache missses from retired instructions
	for (auto instr = r_data->begin; instr != r_data->end; instr++) {
          int idx = 0;
          std::vector<int> to_remove = std::vector<int>();
	  for (auto cm : cache_misses) {
            if (cm.first == instr->instr_id) {
	      to_remove.push_back(idx);
	    }
	    idx++;
          }
          for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
            cache_misses.erase(cache_misses.begin() + *it);
          }
        }

	// handle committing counts
        while (cmp_greater(std::distance(r_data->begin, r_data->end), computing_counts.size())) {
          computing_counts.push_back(0);
        }
        computing_counts[std::distance(r_data->begin, r_data->end) - 1]++;

        // update previous instruction data
        has_previous_instruction = true;
        previous_instruction_mispredicted_branch = std::prev(r_data->end)->branch_mispredicted;
        last_instr_id = std::prev(r_data->end)->instr_id;
      }
    } else if (eventType == event::END) {
      //curr_cycles = 0;

      print_results(curr_cycles, computing_cycles, stalled_cycles, flushed_cycles, drained_cycles, computing_counts, stalled_cache_miss_counts, drained_cache_miss_counts);

      /*
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
      fmt::print("\n  Refilling cycles: {}\n", refilling_cycles);
      fmt::print("Flushed cycles: {}\n", flushed_cycles);
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
      */
      // TODO: reset counts
    }
  }
};

#endif

#ifdef SET_ASIDE_CHAMPSIM_MODULE
#undef SET_ASIDE_CHAMPSIM_MODULE
#define CHAMPSIM_MODULE
#endif
