#ifndef ROB_HEAD_STATE_H
#define ROB_HEAD_STATE_H

#include <cstddef>

namespace champsim
{
/**
 * Where the instruction at the head of the ROB is in its lifecycle, as seen at the start of a cycle's retire stage, when it hasn't completed.
 */
enum class rob_head_state : std::size_t {
  WAITING_TO_SCHEDULE, // dispatched, but the dispatch-to-schedule latency hasn't elapsed
  SCHEDULE_BLOCKED,    // the scheduling latency has elapsed, but there aren't enough free physical registers to rename it
  WAITING_FOR_OPERANDS, // scheduled, but a source register isn't ready
  WAITING_TO_ISSUE,     // scheduled with its operands ready, but not yet issued
  EXECUTING,            // issued, and the execute latency hasn't elapsed
  LOAD_WAITING_ON_STORE, // executed, and a load waits for an older store to forward its data
  LOAD_NOT_ISSUED,       // executed, and a load hasn't been sent to the L1D yet
  LOAD_IN_FLIGHT,        // executed, and a load has been sent to the L1D but its data hasn't returned
  STORE_PENDING,         // executed, and a store hasn't finished in the SQ yet
  WAITING_TO_COMPLETE,   // executed with all memory operations done; it completes later this cycle and retires next cycle
  NONE                   // the ROB is empty or its head has completed
};

inline constexpr std::size_t NUM_ROB_HEAD_STATES = static_cast<std::size_t>(rob_head_state::NONE);
} // namespace champsim

#endif
