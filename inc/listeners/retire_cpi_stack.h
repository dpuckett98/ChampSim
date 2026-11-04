#ifndef RETIRE_CPI_STACK_H
#define RETIRE_CPI_STACK_H

#include <array>
#include <cstdint>
#include <deque>

#include "instruction.h"
#include "listeners/cpi_stack.h"
#include "modules.h"
#include "rob_head_state.h"

namespace champsim
{
/**
 * Listener that builds a top-level CPI stack at the retire stage, in slots: each cycle has retire-width slots (see champsim::cpi_stack). Each slot that
 * retired an instruction is retiring; the unused slots of a cycle all go to one category, decided by the oldest instruction that didn't retire:
 * - mis-speculating: the core is recovering from a misprediction, i.e. the ROB is empty and no correct-path instruction has been dispatched since the flush
 *   (or, defensively, the ROB head is a wrong-path instruction);
 * - stalled: the ROB head (a correct-path instruction) hasn't completed;
 * - drained: the ROB is empty and the core isn't recovering from a misprediction, i.e. the front end isn't supplying instructions.
 *
 * Stalled slots are further broken down by the ROB head's state (see champsim::rob_head_state and champsim::stall_cause_of): waiting on cache (its load is
 * in flight in the L1D), waiting on dependencies (it waits for an operand, or its load waits for an older store to forward its data), or other. Drained
 * slots are broken down by the front end: waiting on cache when the oldest instruction in the front end is waiting for its block from the L1I, and other
 * otherwise.
 *
 * Each stalled sub-category is broken down further by where the ROB head is in its lifecycle (see champsim::rob_head_state), each state under the
 * sub-category its cause picks: waiting on cache into load in flight (in the L1D); waiting on dependencies into waiting for operands and load waiting on
 * store; and other into waiting to schedule, schedule blocked (no free physical registers), waiting to issue, executing, load not issued (to the L1D),
 * store pending, and waiting to complete (it completes after retire this cycle and retires next cycle).
 *
 * Counts reset at the start of each phase and are printed for each core at the end of every non-warmup phase.
 *
 * Model name: ``RETIRE_CPI_STACK``. No parameters.
 */
class retire_cpi_stack : public cpi_stack<champsim::NUM_ROB_HEAD_STATES>
{
public:
  static constexpr category RETIRING = USEFUL;
  // Indexed by champsim::rob_head_state
  static constexpr std::array<const char*, NUM_DETAILS> head_state_names{
      "waiting to schedule", "schedule blocked",      "waiting for operands", "waiting to issue", "executing",
      "load waiting on store", "load not issued",     "load in flight",       "store pending",    "waiting to complete"};

  // The stalled sub-category each head state is under
  [[nodiscard]] static constexpr std::array<subcategory, NUM_DETAILS> head_state_parents()
  {
    std::array<subcategory, NUM_DETAILS> parents{};
    for (std::size_t k = 0; k < NUM_DETAILS; ++k)
      parents[k] = subcategory_of(STALLED, champsim::stall_cause_of(static_cast<champsim::rob_head_state>(k)));
    return parents;
  }

  explicit retire_cpi_stack(champsim::modules::ModuleBuilder builder);

  // The category of a retire-stage cycle's unused slots, given the ROB after retirement
  [[nodiscard]] static category classify_unused(const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict);

  void retire_cycle(uint32_t cpu, uint64_t cycle, std::size_t width, std::size_t num_retired, const std::deque<ooo_model_instr>& rob,
                    bool recovering_from_mispredict, champsim::rob_head_state head_state, champsim::stall_cause fetch_cause) override;
};
} // namespace champsim

#endif
