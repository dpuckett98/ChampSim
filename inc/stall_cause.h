#ifndef STALL_CAUSE_H
#define STALL_CAUSE_H

#include <cstddef>

#include "rob_head_state.h"

namespace champsim
{
/**
 * What holds up the instruction a pipeline stage is waiting on, for the CPI stacks' stalled and drained breakdowns.
 */
enum class stall_cause : std::size_t {
  WAITING_ON_CACHE,        // a memory access (or instruction fetch) has been sent to the L1 cache and hasn't returned; this includes its TLB lookup
  WAITING_ON_DEPENDENCIES, // a source register or an older store it depends on isn't ready
  OTHER                    // anything else (scheduling, execution latency, bandwidth, front-end latency, ...)
};

// The cause for an uncompleted ROB head in the given state: its load is in the L1D (cache), or it waits on a source register or an older store to forward to
// it (dependencies). Its producers are all older and so have retired, so waiting for operands is rare at the head.
constexpr stall_cause stall_cause_of(rob_head_state state)
{
  switch (state) {
  case rob_head_state::LOAD_IN_FLIGHT:
    return stall_cause::WAITING_ON_CACHE;
  case rob_head_state::WAITING_FOR_OPERANDS:
  case rob_head_state::LOAD_WAITING_ON_STORE:
    return stall_cause::WAITING_ON_DEPENDENCIES;
  default:
    return stall_cause::OTHER;
  }
}
} // namespace champsim

#endif
