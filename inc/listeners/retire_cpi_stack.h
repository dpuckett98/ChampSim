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
 * Listener that builds a top-level CPI stack at the retire stage. Every core cycle is put in exactly one category:
 * - retiring: at least one instruction retired;
 * - mis-speculating: nothing retired because the core is recovering from a misprediction, i.e. the ROB is empty and no correct-path instruction has been
 *   dispatched since the flush (or, defensively, the ROB head is a wrong-path instruction);
 * - stalled: nothing retired because the ROB head (a correct-path instruction) hasn't completed;
 * - drained: nothing retired because the ROB is empty and the core isn't recovering from a misprediction, i.e. the front end isn't supplying instructions.
 *
 * Stalled cycles are further broken down by where the ROB head is in its lifecycle (see champsim::rob_head_state), printed right after the stalled line:
 * waiting to schedule, schedule blocked (no free physical registers), waiting for operands, waiting to issue, executing, load waiting on store, load not
 * issued (to the L1D), load in flight (in the L1D), store pending, and waiting to complete (it completes after retire this cycle and retires next cycle).
 *
 * Counts reset at the start of each phase and are printed for each core at the end of every non-warmup phase.
 *
 * Model name: ``RETIRE_CPI_STACK``. No parameters.
 */
class retire_cpi_stack : public cpi_stack<4, champsim::NUM_ROB_HEAD_STATES>
{
public:
  enum category : std::size_t { RETIRING, MIS_SPECULATING, STALLED, DRAINED };
  static constexpr std::array<const char*, NUM_CATEGORIES> category_names{"retiring", "mis-speculating", "stalled", "drained"};
  // Indexed by champsim::rob_head_state
  static constexpr std::array<const char*, NUM_SUBCATEGORIES> head_state_names{
      "stalled waiting to schedule", "stalled schedule blocked",     "stalled waiting for operands", "stalled waiting to issue",
      "stalled executing",           "stalled load waiting on store", "stalled load not issued",     "stalled load in flight",
      "stalled store pending",       "stalled waiting to complete"};

  explicit retire_cpi_stack(champsim::modules::ModuleBuilder builder);

  // The category a retire-stage cycle belongs to
  [[nodiscard]] static category classify(std::size_t num_retired, const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict);

  void retire_cycle(uint32_t cpu, uint64_t cycle, std::size_t num_retired, const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict,
                    champsim::rob_head_state head_state) override;
};
} // namespace champsim

#endif
