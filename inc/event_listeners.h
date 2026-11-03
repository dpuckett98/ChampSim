#ifndef EVENT_LISTENERS_H
#define EVENT_LISTENERS_H

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "instruction.h"
#include "modules.h"

namespace champsim::listeners
{
// The listeners that receive simulation events
inline std::vector<champsim::modules::listener*>& active()
{
  static std::vector<champsim::modules::listener*> listeners;
  return listeners;
}

/**
 * Collect the listeners for this run into active():
 * - the environment's ``"listener"`` modules (explicit config children),
 * - a default ``HEARTBEAT`` (printing every ``heartbeat_frequency`` instructions) if no heartbeat is active yet.
 * If ``hide_heartbeat`` is set, no ``HEARTBEAT`` listener is attached.
 */
void init(champsim::modules::environment_module& env, uint64_t heartbeat_frequency, bool hide_heartbeat);

inline void begin_phase(bool is_warmup)
{
  for (auto* listener : active())
    listener->begin_phase(is_warmup);
}

inline void end_phase(bool is_warmup)
{
  for (auto* listener : active())
    listener->end_phase(is_warmup);
}

inline void retire_cycle(uint32_t cpu, uint64_t cycle, std::size_t num_retired, const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict)
{
  for (auto* listener : active())
    listener->retire_cycle(cpu, cycle, num_retired, rob, recovering_from_mispredict);
}

inline void dispatch_cycle(uint32_t cpu, uint64_t cycle, std::size_t num_dispatched, std::size_t num_wp_dispatched, const std::deque<ooo_model_instr>& dispatch_buffer,
                           bool backend_full, bool recovering_from_mispredict)
{
  for (auto* listener : active())
    listener->dispatch_cycle(cpu, cycle, num_dispatched, num_wp_dispatched, dispatch_buffer, backend_full, recovering_from_mispredict);
}

inline void issue_cycle(uint32_t cpu, uint64_t cycle, std::size_t num_issued, std::size_t num_wp_issued, const std::deque<ooo_model_instr>& rob, bool backend_full,
                        bool recovering_from_mispredict)
{
  for (auto* listener : active())
    listener->issue_cycle(cpu, cycle, num_issued, num_wp_issued, rob, backend_full, recovering_from_mispredict);
}

inline void retire(uint32_t cpu, std::deque<ooo_model_instr>::const_iterator begin, std::deque<ooo_model_instr>::const_iterator end, uint64_t cycle)
{
  for (auto* listener : active())
    listener->retire(cpu, begin, end, cycle);
}
} // namespace champsim::listeners

#endif
