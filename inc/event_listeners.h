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

inline void retire(uint32_t cpu, std::deque<ooo_model_instr>::const_iterator begin, std::deque<ooo_model_instr>::const_iterator end, uint64_t cycle)
{
  for (auto* listener : active())
    listener->retire(cpu, begin, end, cycle);
}
} // namespace champsim::listeners

#endif
