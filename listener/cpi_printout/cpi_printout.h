#ifdef CHAMPSIM_MODULE
#define SET_ASIDE_CHAMPSIM_MODULE
#undef CHAMPSIM_MODULE
#endif

#ifndef CPI_PRINTOUT_H
#define CPI_PRINTOUT_H

#include "event_listener.h"

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

class cpi_printout : public EventListener {
  long long total_retired_instrs = 0;
  int num_retired_instrs = 0;
  int base_retired_instrs = 0;
  long curr_cycles = 0;
  bool in_warmup = true;
  long long interval_num = 0;
  
  int printout_interval = 10000;
  
public:
  void process_event(event eventType, void* data) {
    if (eventType == event::BEGIN_PHASE) {
      BEGIN_PHASE_data* b_data = static_cast<BEGIN_PHASE_data *>(data);
      in_warmup = b_data->is_warmup;
      return;
    }
    if (in_warmup) {
      return;
    }
    if (eventType == event::RETIRE) {
      RETIRE_data* r_data = static_cast<RETIRE_data *>(data);
      num_retired_instrs += std::distance(r_data->begin, r_data->end);
      total_retired_instrs += std::distance(r_data->begin, r_data->end);
      if (num_retired_instrs >= printout_interval) {
        fmt::print("interval {} total_instr {}\n", interval_num, total_retired_instrs);
        fmt::print("interval {} cycles {}\n", interval_num, curr_cycles);
        fmt::print("interval {} instrs {}\n", interval_num, num_retired_instrs - base_retired_instrs);
        num_retired_instrs = num_retired_instrs % printout_interval;
        base_retired_instrs = num_retired_instrs;
        curr_cycles = 0;
        interval_num++;
      }
    } else if (eventType == event::PRE_CYCLE) {
      curr_cycles++;
    } else if (eventType == event::END) {
      fmt::print("##END##\n"); // this is a trigger for the post-processor to see if the trace finished running or not
    }
  }
};

#endif

#ifdef SET_ASIDE_CHAMPSIM_MODULE
#undef SET_ASIDE_CHAMPSIM_MODULE
#define CHAMPSIM_MODULE
#endif
