#include "listeners/heartbeat.h"

#include <chrono>
#include <fmt/chrono.h>
#include <fmt/ostream.h>

std::chrono::seconds elapsed_time();

champsim::heartbeat::heartbeat(champsim::modules::ModuleBuilder builder)
    : cycles_between_printouts(builder.get_parameter<uint64_t>("heartbeat_frequency", true, default_frequency))
{
}

void champsim::heartbeat::add_cpu(uint32_t cpu)
{
  while (cpu >= switched_phase.size()) {
    num_retired_last_printout.push_back(0);
    cycles_last_printout.push_back(0);
    num_retired.push_back(0);
    num_retired_start_phase.push_back(0);
    cycles_start_phase.push_back(0);
    switched_phase.push_back(false);
  }
}

void champsim::heartbeat::begin_phase([[maybe_unused]] bool is_warmup)
{
  for (std::size_t i = 0; i < switched_phase.size(); i++) {
    switched_phase[i] = true;
  }
}

void champsim::heartbeat::retire(uint32_t cpu, std::deque<ooo_model_instr>::const_iterator begin, std::deque<ooo_model_instr>::const_iterator end,
                                 uint64_t cycle)
{
  add_cpu(cpu);
  num_retired[cpu] += static_cast<uint64_t>(std::distance(begin, end));

  if (switched_phase[cpu]) {
    switched_phase[cpu] = false;
    num_retired_start_phase[cpu] = num_retired[cpu];
    cycles_start_phase[cpu] = cycle;
  }

  if (num_retired[cpu] >= num_retired_last_printout[cpu] + cycles_between_printouts) {
    auto heartbeat_instr = static_cast<double>(num_retired[cpu] - num_retired_last_printout[cpu]);
    auto heartbeat_cycle = static_cast<double>(cycle - cycles_last_printout[cpu]);

    auto phase_instr = static_cast<double>(num_retired[cpu] - num_retired_start_phase[cpu]);
    auto phase_cycle = static_cast<double>(cycle - cycles_start_phase[cpu]);

    fmt::print(*std_out, "Heartbeat CPU {} instructions: {} cycles: {} heartbeat IPC: {:.4} cumulative IPC: {:.4} (Simulation time: {:%H hr %M min %S sec})\n",
               cpu, num_retired[cpu], cycle, heartbeat_instr / heartbeat_cycle, phase_instr / phase_cycle, elapsed_time());

    // Advance the heartbeat baseline by exact multiples of
    // ``cycles_between_printouts`` rather than snapping to the current
    // retired count.  When more than one instruction retires in the same
    // cycle, ``num_retired`` can overshoot the threshold; snapping would
    // accumulate that overshoot every heartbeat and permanently displace
    // when subsequent heartbeats fire.
    auto overshoot = num_retired[cpu] - num_retired_last_printout[cpu];
    auto whole_periods = overshoot / cycles_between_printouts;
    num_retired_last_printout[cpu] += whole_periods * cycles_between_printouts;
    cycles_last_printout[cpu] = cycle;
  }
}

static champsim::modules::listener::register_module<champsim::heartbeat> heartbeat_reg("HEARTBEAT");
