#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include <cstdint>
#include <deque>
#include <iostream>
#include <vector>

#include "instruction.h"
#include "modules.h"

namespace champsim
{
/**
 * Listener that periodically prints each core's progress: retired instructions, cycles, and the IPC since the last printout and since the start of the
 * phase.
 *
 * Model name: ``HEARTBEAT``. Parameters:
 * - ``heartbeat_frequency`` (default 10000000): the number of retired instructions between printouts.
 */
class heartbeat : public champsim::modules::listener
{
public:
  static constexpr uint64_t default_frequency = 10000000;

  std::ostream* std_out = &std::cout;

  uint64_t cycles_between_printouts = default_frequency;
  std::vector<uint64_t> num_retired_last_printout;
  std::vector<uint64_t> cycles_last_printout;
  std::vector<uint64_t> num_retired;

  std::vector<uint64_t> num_retired_start_phase;
  std::vector<uint64_t> cycles_start_phase;
  std::vector<bool> switched_phase; // true if there has been a begin_phase event since the last time this CPU retired

  explicit heartbeat(champsim::modules::ModuleBuilder builder);

  void begin_phase(bool is_warmup) override;
  void retire(uint32_t cpu, std::deque<ooo_model_instr>::const_iterator begin, std::deque<ooo_model_instr>::const_iterator end, uint64_t cycle) override;

private:
  void add_cpu(uint32_t cpu);
};
} // namespace champsim

#endif
