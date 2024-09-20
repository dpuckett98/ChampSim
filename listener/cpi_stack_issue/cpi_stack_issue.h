#ifdef CHAMPSIM_MODULE
#define SET_ASIDE_CHAMPSIM_MODULE
#undef CHAMPSIM_MODULE
#endif

#ifndef CPI_STACK_ISSUE_H
#define CPI_STACK_ISSUE_H

#include "event_listener.h"
#include "util/algorithm.h"

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

class cpi_stack_issue : public EventListener {
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
  std::map<uint64_t, long> stalling_instructions; // map from instr_id to the number of times that instr. caused its dependencies to stall the issue stage
  std::map<uint64_t, long> draining_instructions; // map from instr_id to the number of times that instr. was the oldest instr in the FE (when issue stage was drained and it's not a branch misprediction)

  bool in_warmup = false;
  bool has_previous_instruction = false;
  bool previous_instruction_mispredicted_branch = false;
  uint64_t last_mispredicted_branch_id = 0; //std::numeric_limits<uint64_t>::max();

  std::deque<ooo_model_instr>* IFETCH_BUFFER;
  std::deque<ooo_model_instr>* DISPATCH_BUFFER;
  std::deque<ooo_model_instr>* DECODE_BUFFER;
  std::deque<ooo_model_instr>* ROB;
  std::deque<ooo_model_instr>* input_queue;

  void mark_drained_cache_misses(uint64_t instr_id, long count) {
    std::string name = "";
    for (auto cm : cache_misses) {
      // skip iteration unless the cache miss matches the specified instruction
      if (cm.first != instr_id) {
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
          drained_cache_miss_counts[name] += count;
        } else {
          drained_cache_miss_counts[name] = count;
        }
      }
  }

  // instr_id is the first waiting instruction in the issue queue
  void record_stall_cycle(const ooo_model_instr& instr) {
    auto matches = [& instr](const ooo_model_instr& x) {
      if (x.completed || !x.executed) {
        return false;
      }
      for (auto y : x.registers_instrs_depend_on_me) {
        if (y.get().instr_id == instr.instr_id) {
          return true;
	}
      }
      return false;
    };
    auto oldest_dependency = std::find_if(ROB->begin(), ROB->end(), matches);
    if (oldest_dependency != ROB->end()) {
      if (stalling_instructions.count(oldest_dependency->instr_id) == 0) {
        stalling_instructions[oldest_dependency->instr_id] = 1;
      } else {
        stalling_instructions[oldest_dependency->instr_id]++;
      }
    }
  }

  // instr_id has retired, so if it's in stalling_instructions and cache_misses increment the amounts specified
  void mark_stalled_cache_misses(uint64_t instr_id) {
    // if this instruction wasn't in stalling_instructions, then we're done
    if (stalling_instructions.count(instr_id) == 0) {
      return;
    }
    // check for cache misses associated with this instr
    std::string name = "";
    bool recording = false;
    for (auto cm : cache_misses) {
      // if the cache miss doesn't match this instr, then skip it
      if (cm.first != instr_id) {
	continue;
      }

      // update recording; if not recording, skip this instr
      if (cm.second == "cpu0_DTLB" || cm.second == "cpu0_L1D") {
        recording = true;
      } else if (!recording) {
        continue;
      }
     
      // ignore duplicates
      if (name.find(cm.second) != std::string::npos) {
        continue;
      }

      // add to name
      if (name != "") {
        name += "-";
      }
      name += cm.second;
    }
    
    // add ct to corresponding cache miss counter
    long ct = stalling_instructions[instr_id];
    if (name != "") {
      if (stalled_cache_miss_counts.count(name)) {
        stalled_cache_miss_counts[name] += ct;
      } else {
        stalled_cache_miss_counts[name] = ct;
      }
    }

    // clean up stalling_instructions
    stalling_instructions.erase(instr_id);
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
      DECODE_BUFFER = p_data->DECODE_BUFFER;
      DISPATCH_BUFFER = p_data->DISPATCH_BUFFER;
      IFETCH_BUFFER = p_data->IFETCH_BUFFER;
      input_queue = p_data->input_queue;
    } else if (eventType == event::BRANCH) {
      BRANCH_data* b_data = static_cast<BRANCH_data *>(data);
      if (b_data->instr->branch_mispredicted) {
        //previous_instruction_mispredicted_branch = true;
	//last_mispredicted_branch_id = b_data->instr->instr_id;
	//fmt::print("Turning on\n");
      }
    } else if (eventType == event::INITIALIZE) {
      /*INITIALIZE_data* i_data = static_cast<INITIALIZE_data *>(data);
      if (i_data->begin->instr_id > last_mispredicted_branch_id) {
        previous_instruction_mispredicted_branch = false;
      }*/
    } else if (eventType == event::START_EXECUTE) {
      START_EXECUTE_data* i_data = static_cast<START_EXECUTE_data *>(data);
      if (std::distance(i_data->begin, i_data->end) == 0) {
	// if ROB is empty or only has executed instructions, then it's a frontend fault; e.g., if there's at least one instruction waiting on dependencies, we blame the backend (similar to MSCS; different from TopDown)
	auto is_not_executed = [](const ooo_model_instr& x) {
          return !x.executed;
	};
	auto first_waiting_instr = std::find_if(ROB->begin(), ROB->end(), is_not_executed);
	// if no instrs issued, ROB is empty, and previous instruction was a mispredicted branch, then it's flushed
	//if (ROB->empty() && previous_instruction_mispredicted_branch) {
        //  flushed_cycles++;
        if (ROB->empty() || first_waiting_instr == ROB->end()) {
	  // get first instr in FE
          ooo_model_instr* instr = nullptr;
	  bool valid = false;
	  if (!DISPATCH_BUFFER->empty()) {
            instr = &DISPATCH_BUFFER->front();
	    valid = true;
	  } else if (!DECODE_BUFFER->empty()) {
            instr = &DECODE_BUFFER->front();
            valid = true;
          } else if (!IFETCH_BUFFER->empty()) {
            instr = &IFETCH_BUFFER->front();
            valid = true;
          } else if (!input_queue->empty()) {
            instr = &input_queue->front();
            valid = true;
          }

	  // if no instruction in FE, then it must be from a branch misprediction
          if (!valid) {
            flushed_cycles++;
	  } else {
            
            // if the youngest instruction that reached/passed the issue stage was mispredicted, then blame the branch misprediction from that instruction
            if (instr->instr_id - 1 == last_mispredicted_branch_id) {
              flushed_cycles++;
	    } else {
              drained_cycles++;
	      draining_instructions[instr->instr_id]++;
	    }
	  }

	  /*if (previous_instruction_mispredicted_branch) {
            // if no instructions issued, ROB is empty/only has completed instructions, and previous instruction was a mispredicted branch, then it's flushed
            flushed_cycles++;
          } else {
            // if no instructions issued, ROB is empty/only has completed instructions, and previous instruction wasn't a mispredicted branch, then we assume it's drained and check for icache misses
            // this assumes the only time the ROB is flushed is from a branch misprediction, which is true in ChampSim (I think)
            // this counts some startup cycles as drained cycles, but this shouldn't be a problem if you're using warmup instructions
            drained_cycles++;
            drained_streak_cycles++;
          }*/
        } else {
          // if no instructions issued but ROB has an incomplete instruction, then it's stalled
          // this counts some startup cycles as stalled cycles
          stalled_cycles++;

          record_stall_cycle(*first_waiting_instr);

	  // find cache miss and increase drained_cache_miss_counts if the ROB was previously drained
	  /*if (drained_streak_cycles > 0) {
	    mark_drained_cache_misses(first_waiting_instr->instr_id);
	    drained_streak_cycles = 0;
	  }*/
        }
      } else {
        // if any instructions issued this cycle, in computing state
        computing_cycles++;

        // find cache miss and increase drained_cache_miss_counts if the ROB was previously drained
	/*if (drained_streak_cycles > 0) {
          // TODO: blame the oldest instruction that wasn't there last cycle; either it was issued this cycle or it's waiting
	  mark_drained_cache_misses((*i_data->begin)->instr_id);
          drained_streak_cycles = 0;
        }*/

	// handle committing counts
        while (cmp_greater(std::distance(i_data->begin, i_data->end), computing_counts.size())) {
          computing_counts.push_back(0);
        }
        computing_counts[std::distance(i_data->begin, i_data->end) - 1]++;

        // update mispredicted branch
	for (auto iter = i_data->begin; iter < i_data->end; iter++) {
          if ((*iter)->branch_mispredicted && (*iter)->instr_id > last_mispredicted_branch_id) {
            last_mispredicted_branch_id = (*iter)->instr_id;
	  }
	}

	/*if (previous_instruction_mispredicted_branch) {
          auto is_past_mispredicted_branch = [id = last_mispredicted_branch_id](const ooo_model_instr* instr) {
	    //fmt::print("{} ", instr->instr_id);
            return instr->instr_id > id;
	  };
          auto result = std::find_if(i_data->begin, i_data->end, is_past_mispredicted_branch);
	  if (result != i_data->end) {
            //fmt::print("turning off\n");
            previous_instruction_mispredicted_branch = false;
	  } else {
            //fmt::print(" - committing while mispredicted branch\n");
	    //fmt::print("branch: {}\n", last_mispredicted_branch_id);
	  }
	}*/

      }
    } else if (eventType == event::RETIRE) {
      RETIRE_data* r_data = static_cast<RETIRE_data *>(data);
      // mark data cache misses
      for (auto instr = r_data->begin; instr != r_data->end; instr++) {
        mark_stalled_cache_misses(instr->instr_id);
      }

      // mark instr cache misses
      for (auto instr = r_data->begin; instr != r_data->end; instr++) {
	if (draining_instructions.count(instr->instr_id)) {
          mark_drained_cache_misses(instr->instr_id, draining_instructions[instr->instr_id]);
	  draining_instructions.erase(instr->instr_id);
	}
      }

      // remove cache misses from retired instructions
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

      // update previous instruction data
      if (std::distance(r_data->begin, r_data->end) > 0) {
        has_previous_instruction = true;
        //previous_instruction_mispredicted_branch = std::prev(r_data->end)->branch_mispredicted;
        last_instr_id = std::prev(r_data->end)->instr_id;
      }
    } else if (curr_cycles == print_cycles or eventType == event::END) {
      curr_cycles = 0;

      fmt::print("CPI Stacks Issue:\n");
      fmt::print("Computing cycles: {}\n", computing_cycles);
      for (unsigned long i = 0; i < computing_counts.size(); i++) {
        fmt::print("  Issued {}: {}", i + 1, computing_counts[i]);
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
      fmt::print("\n\nUnaccounted cache misses: {}\n\n", cache_misses.size());
      // TODO: reset data
    }
  }
};

#endif

#ifdef SET_ASIDE_CHAMPSIM_MODULE
#undef SET_ASIDE_CHAMPSIM_MODULE
#define CHAMPSIM_MODULE
#endif
