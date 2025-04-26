#ifdef CHAMPSIM_MODULE
#define SET_ASIDE_CHAMPSIM_MODULE
#undef CHAMPSIM_MODULE
#endif

#ifndef MS_CPI_ISSUE_H
#define MS_CPI_ISSUE_H

#include "event_listener.h"
#include "util/algorithm.h"
#include "instruction.h"
#include "trace_instruction.h"

#include <map>
#include <vector>
#include <deque>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

class ms_cpi_issue : public EventListener {
  
  double WIDTH = 5; // todo: calculate this automatically
  
  std::deque<bool> dcache_miss_history;
  int dmh_size = 600;
  
  uint64_t total_retired_instrs = 0;
  uint64_t num_retired_instrs = 0;
  uint64_t base_retired_instrs = 0;
  uint64_t curr_cycles = 0;
  uint64_t interval_num = 0;
  uint64_t printout_interval = 10000;

  bool last_printed = true;
  uint64_t last_interval_num = 0;
  uint64_t last_total_retired_instrs = 0;
  uint64_t last_num_retired_instrs = 0;
  uint64_t last_base_retired_instrs = 0;
  uint64_t last_curr_cycles = 0;
  double last_base_comp = 0;
  double last_icache_comp = 0;
  double last_bp_comp = 0;
  double last_drained_other_comp = 0;
  double last_d_cache_comp = 0;
  double last_depend_comp = 0;
  double last_stalled_other_comp = 0;
  std::map<uint64_t, double> last_blamed_instrs;

  double base_comp = 0;
  double icache_comp = 0;
  double bp_comp = 0;
  double drained_other_comp = 0;
  double d_cache_comp = 0;
  double depend_comp = 0;
  double stalled_other_comp = 0;

  std::map<uint64_t, double> blamed_instrs;
  std::vector<std::pair<uint64_t, std::string> > cache_misses;

  bool in_warmup = false;

  const O3_CPU* o3_cpu;
  std::deque<ooo_model_instr>* ROB;
  ooo_model_instr last_retired_instr = ooo_model_instr(0, input_instr());

  void copy_to_last_counters() {
    last_interval_num = interval_num;
    last_total_retired_instrs = total_retired_instrs;
    last_num_retired_instrs = num_retired_instrs;
    last_base_retired_instrs = base_retired_instrs;
    last_curr_cycles = curr_cycles;
    last_base_comp = base_comp;
    last_icache_comp = icache_comp;
    last_bp_comp = bp_comp;
    last_drained_other_comp = drained_other_comp;
    last_d_cache_comp = d_cache_comp;
    last_depend_comp = depend_comp;
    last_stalled_other_comp = stalled_other_comp;
    last_blamed_instrs = std::map<uint64_t, double>(blamed_instrs);
  }

  void reset_curr_counters() {
    base_comp = 0;
    icache_comp = 0;
    bp_comp = 0;
    drained_other_comp = 0;
    d_cache_comp = 0;
    depend_comp = 0;
    stalled_other_comp = 0;
    blamed_instrs = std::map<uint64_t, double>();
  }
  
  void print_counters() {
    fmt::print("ms_cpi_issue interval {} total_instr {}\n", last_interval_num, last_total_retired_instrs);
    fmt::print("ms_cpi_issue interval {} instrs {}\n", last_interval_num, last_num_retired_instrs - last_base_retired_instrs);
    fmt::print("ms_cpi_issue interval {} base_comp {}\n", last_interval_num, last_base_comp / WIDTH);
    fmt::print("ms_cpi_issue interval {} icache_comp {}\n", last_interval_num, last_icache_comp / WIDTH);
    fmt::print("ms_cpi_issue interval {} bp_comp {}\n", last_interval_num, last_bp_comp / WIDTH);
    fmt::print("ms_cpi_issue interval {} drained_other_comp {}\n", last_interval_num, last_drained_other_comp / WIDTH);
    fmt::print("ms_cpi_issue interval {} d_cache_comp {}\n", last_interval_num, last_d_cache_comp / WIDTH);
    fmt::print("ms_cpi_issue interval {} depend_comp {}\n", last_interval_num, last_depend_comp / WIDTH);
    fmt::print("ms_cpi_issue interval {} stalled_other_comp {}\n", last_interval_num, last_stalled_other_comp / WIDTH);
    fmt::print("ms_cpi_issue interval {} cycles {}\n", last_interval_num, last_curr_cycles);
    /*fmt::print("ms_cpi_issue interval {} missing_cycles {}\n", last_interval_num, last_curr_cycles - ((last_base_comp + last_icache_comp + last_bp_comp + last_drained_other_comp + last_d_cache_comp + last_depend_comp + last_stalled_other_comp) / WIDTH));
    double uncounted_blamed = 0;
    for (auto it = last_blamed_instrs.begin(); it != last_blamed_instrs.end(); it++) {
      std::cout << "missing instr " << it->first << std::endl;
      uncounted_blamed += it->second;
    }
    fmt::print("ms_cpi_issue interval {} uncounted_blamed {}\n", last_interval_num, uncounted_blamed);*/
  }

  void process_event(event eventType, void* data) {
    // check for warmup
    if (eventType == event::BEGIN_PHASE) {
      BEGIN_PHASE_data* b_data = static_cast<BEGIN_PHASE_data *>(data);
      in_warmup = b_data->is_warmup;
      return;
    }
    if (in_warmup) {
      return;
    }
    
    // not in warmup
    if (eventType == event::START_EXECUTE) {
      START_EXECUTE_data* s_data = static_cast<START_EXECUTE_data*>(data);
      float n = std::distance(s_data->begin, s_data->end);
      double f = n;
      base_comp += f;
      double omf = WIDTH - n;
      
      if (omf > 0) {
        // if there are no instructions in the RoB that are scheduled but not executed
        bool rs_empty = true;
        ooo_model_instr oldest_ready_instr = ooo_model_instr(0, input_instr());
        for (auto instr : *ROB) {
          if (instr.scheduled && !instr.executed) {
            if (rs_empty || instr.instr_id < oldest_ready_instr.instr_id) {
              rs_empty = false;
              oldest_ready_instr = instr;
            }
            //break;
          }
        }
        if (rs_empty) { // FRONTEND STALL
          // find the youngest executed instruction; this is either the last retired instruction, or the youngest executed instruction in the ROB
          auto youngest_executed_instr = last_retired_instr;
          for (auto instr : *ROB) {
            if (instr.executed && instr.instr_id > youngest_executed_instr.instr_id) {
              youngest_executed_instr = instr;
            }
          }
          // check if the oldest instruction in the FE pipeline (which is the instruction following the youngest issued instruction) had an i-cache miss
          bool i_cache_miss = false;
          for (auto cm : cache_misses) {
            if (cm.first == youngest_executed_instr.instr_id + 1 && cm.second == "cpu0_L1I") {
              i_cache_miss = true;
            }
          }
          bool bp_miss = youngest_executed_instr.branch_mispredicted; // check if the youngest executed instruction had a BP miss
          if (i_cache_miss) {
            icache_comp += omf;
          } else if (bp_miss) {
            bp_comp += omf;
          } else {
            drained_other_comp += omf;
          }
        } else { // BACKEND STALL
          // find all the producing instrs
          std::vector<uint64_t> producing_instr_ids = o3_cpu->reg_allocator.get_producing_instructions(oldest_ready_instr);
          // pick youngest producing instr that we have cache miss information on
          uint64_t producing_instr_id = 0;
          bool found_producing_instr_id = false;
          for (auto instr_id : producing_instr_ids) {
            if ((instr_id > producing_instr_id || !found_producing_instr_id) && (instr_id > last_retired_instr.instr_id - dcache_miss_history.size())) {
              producing_instr_id = instr_id;
              found_producing_instr_id = true;
            }
          }
          if (producing_instr_id <= last_retired_instr.instr_id - dcache_miss_history.size()) {
            found_producing_instr_id = false;
          }
          
          if (found_producing_instr_id) {
            // if producing instruction already retired...
            if (producing_instr_id <= last_retired_instr.instr_id) {
              int idx = dcache_miss_history.size() - (last_retired_instr.instr_id - producing_instr_id) - 1;
              if (dcache_miss_history[idx]) {
                d_cache_comp += omf;
              } else {
                depend_comp += omf;
              }
              // otherwise associate time with the producing instruction (it'll be added to the given components when it retires)
            } else if (blamed_instrs.count(producing_instr_id) > 0) {
              blamed_instrs[producing_instr_id] += omf;
            } else {
              blamed_instrs[producing_instr_id] = omf;
            }
          } else { // if all the producing instructions have completed, then blame dependency (TODO: this may be wrong)
            stalled_other_comp += omf;
          }
        }
      }
    } else if (eventType == event::CACHE_TRY_HIT) {
      CACHE_TRY_HIT_data* c_data = static_cast<CACHE_TRY_HIT_data *>(data);
      if (!c_data->hit && c_data->instr_id > last_retired_instr.instr_id) {
        cache_misses.push_back(std::make_pair(c_data->instr_id, c_data->NAME));
      }
    } else if (eventType == event::PRE_CYCLE) {
      PRE_CYCLE_data* p_data = static_cast<PRE_CYCLE_data *>(data);
      ROB = p_data->ROB;
      o3_cpu = p_data->o3_cpu;
      
      if (num_retired_instrs >= printout_interval) {
        if (!last_printed) {
          print_counters();
        }
        copy_to_last_counters();
        reset_curr_counters();
        num_retired_instrs = num_retired_instrs % printout_interval;
        base_retired_instrs = num_retired_instrs;
        curr_cycles = 0;
        interval_num++;
        last_printed = false;
      }
      // only print out the last set of counters when the map is empty
      if (!last_printed && last_blamed_instrs.empty()) {
        print_counters();
        last_printed = true;
      }
      
      curr_cycles++;
    } else if (eventType == event::RETIRE) {
      RETIRE_data* r_data = static_cast<RETIRE_data *>(data);
      if (std::distance(r_data->begin, r_data->end) > 0) {
        last_retired_instr = *r_data->end;
      }
      
      // remove cache missses from retired instructions
      for (auto instr = r_data->begin; instr != r_data->end; instr++) {
        int idx = 0;
        std::vector<int> to_remove = std::vector<int>();
        bool has_d_cache_miss = false;
        for (auto cm : cache_misses) {
          if (cm.first == instr->instr_id) {
            to_remove.push_back(idx);
            if (cm.second == "cpu0_L1D") {
              has_d_cache_miss = true;
            }
          }
          idx++;
        }
        for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
          cache_misses.erase(cache_misses.begin() + *it);
        }
        
        // count cycles if it's in blamed instrs
        if (last_blamed_instrs.find(instr->instr_id) != last_blamed_instrs.end()) {
        //if (last_blamed_instrs.count(instr->instr_id) > 0) {
          if (has_d_cache_miss) {
            last_d_cache_comp += last_blamed_instrs[instr->instr_id];
          } else {
            last_depend_comp += last_blamed_instrs[instr->instr_id];
          }
          last_blamed_instrs.erase(instr->instr_id);
        }
        if (blamed_instrs.find(instr->instr_id) != blamed_instrs.end()) {
        //if (blamed_instrs.count(instr->instr_id) > 0) {
          if (has_d_cache_miss) {
            d_cache_comp += blamed_instrs[instr->instr_id];
          } else {
            depend_comp += blamed_instrs[instr->instr_id];
          }
          blamed_instrs.erase(instr->instr_id);
        }
        
        // update dcache miss history
        dcache_miss_history.push_front(has_d_cache_miss);
        //fmt::print("Adding {}\n", instr->instr_id);
      }
      while (dcache_miss_history.size() > dmh_size) {
        dcache_miss_history.pop_back();
        //fmt::print("Removing {}\n", last_retired_instr.instr_id - dcache_miss_history.size() + 1);
      }
      
      // do printout
      num_retired_instrs += std::distance(r_data->begin, r_data->end);
      total_retired_instrs += std::distance(r_data->begin, r_data->end);
    }
  }
};

#endif

#ifdef SET_ASIDE_CHAMPSIM_MODULE
#undef SET_ASIDE_CHAMPSIM_MODULE
#define CHAMPSIM_MODULE
#endif
