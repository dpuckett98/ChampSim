#ifndef ISSUE_CPI_STACK_H
#define ISSUE_CPI_STACK_H

#include <cstdint>
#include <deque>

#include "instruction.h"
#include "listeners/cpi_stack.h"
#include "modules.h"
#include "rob_head_state.h"

namespace champsim
{
/**
 * Listener that builds a top-level CPI stack at the issue stage (ROB -> execution), in slots: each cycle has execute-width slots (see champsim::cpi_stack).
 * Each slot that issued a correct-path instruction is issuing, and each that issued a wrong-path instruction is mis-speculating. The unused slots of a cycle
 * all go to one category, decided by the oldest instruction left unissued:
 * - stalled: it is a correct-path instruction waiting on its operands, scheduling, or a free physical register, or, with no instruction waiting to issue,
 *   the ROB, LQ, or SQ is full and blocks dispatch;
 * - mis-speculating: it is on the wrong path or, with no instruction waiting to issue, the core is recovering from a misprediction;
 * - drained: no instruction is waiting to issue and the front end isn't supplying any.
 *
 * Stalled slots are further broken down by what holds the stage up. When a correct-path instruction waits to issue, it is the oldest one's
 * oldest_unissued_cause: waiting on cache when an operand it waits for comes from a load in flight in the L1D, waiting on dependencies when it waits for
 * any other operand, and other when it waits on scheduling or a physical register. When nothing waits and the back end is full, it is the ROB head's
 * (see champsim::stall_cause_of), as in the dispatch stack. Drained slots are broken down by the front end: waiting on cache when the oldest instruction in
 * the front end is waiting for its block from the L1I, and other otherwise.
 *
 * Counts reset at the start of each phase and are printed for each core at the end of every non-warmup phase. The instruction count is the number of
 * correct-path instructions issued.
 *
 * Model name: ``ISSUE_CPI_STACK``. No parameters.
 */
class issue_cpi_stack : public cpi_stack<>
{
public:
  static constexpr category ISSUING = USEFUL;

  explicit issue_cpi_stack(champsim::modules::ModuleBuilder builder);

  // The category of an issue-stage cycle's unused slots, given the ROB after issue
  [[nodiscard]] static category classify_unused(const std::deque<ooo_model_instr>& rob, bool backend_full, bool recovering_from_mispredict);

  // The cause for a cycle's stalled slots: the oldest unissued instr.'s cause when a correct-path instr. waits to issue, and the ROB head's otherwise
  [[nodiscard]] static champsim::stall_cause stall_cause_for(const std::deque<ooo_model_instr>& rob, champsim::rob_head_state head_state,
                                                             champsim::stall_cause oldest_unissued_cause);

  void issue_cycle(uint32_t cpu, uint64_t cycle, std::size_t width, std::size_t num_issued, std::size_t num_wp_issued, const std::deque<ooo_model_instr>& rob,
                   bool backend_full, bool recovering_from_mispredict, champsim::rob_head_state head_state, champsim::stall_cause oldest_unissued_cause,
                   champsim::stall_cause fetch_cause) override;
};
} // namespace champsim

#endif
