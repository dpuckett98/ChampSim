#ifndef DISPATCH_CPI_STACK_H
#define DISPATCH_CPI_STACK_H

#include <cstdint>
#include <deque>

#include "backend_stall.h"
#include "instruction.h"
#include "listeners/cpi_stack.h"
#include "modules.h"
#include "rob_head_state.h"

namespace champsim
{
/**
 * Listener that builds a top-level CPI stack at the dispatch stage (dispatch buffer -> ROB), in slots: each cycle has dispatch-width slots (see
 * champsim::cpi_stack). Each slot that dispatched a correct-path instruction is dispatching, and each that dispatched a wrong-path instruction is
 * mis-speculating. The unused slots of a cycle all go to one category, decided by the oldest instruction still waiting to dispatch:
 * - stalled: it (on either path) doesn't fit in the ROB, LQ, or SQ;
 * - mis-speculating: it fits but is on the wrong path, or nothing is ready to dispatch while the core is recovering from a misprediction (no correct-path
 *   instruction has been dispatched since the flush);
 * - drained: the front end had no instruction ready to dispatch.
 *
 * Stalled slots are further broken down by the ROB head, whose retirement would free room in the back end (see champsim::stall_cause_of): waiting on
 * cache (its load is in flight in the L1D), waiting on dependencies (it waits for an operand, or its load waits for an older store to forward its data), or
 * other. Drained slots are broken down by the front end: waiting on cache when the oldest instruction in the front end is waiting for its block from the L1I,
 * and other otherwise.
 *
 * Counts reset at the start of each phase and are printed for each core at the end of every non-warmup phase. The instruction count is the number of
 * correct-path instructions dispatched.
 *
 * Model name: ``DISPATCH_CPI_STACK``. No parameters.
 */
class dispatch_cpi_stack : public cpi_stack<>
{
public:
  static constexpr category DISPATCHING = USEFUL;

  explicit dispatch_cpi_stack(champsim::modules::ModuleBuilder builder);

  // The category of a dispatch-stage cycle's unused slots, given the dispatch buffer after dispatch
  [[nodiscard]] static category classify_unused(const std::deque<ooo_model_instr>& dispatch_buffer, champsim::backend_stall backend_stall,
                                                bool recovering_from_mispredict);

  void dispatch_cycle(uint32_t cpu, uint64_t cycle, std::size_t width, std::size_t num_dispatched, std::size_t num_wp_dispatched,
                      const std::deque<ooo_model_instr>& dispatch_buffer, champsim::backend_stall backend_stall, bool recovering_from_mispredict,
                      champsim::rob_head_state head_state, champsim::stall_cause fetch_cause) override;
};
} // namespace champsim

#endif
