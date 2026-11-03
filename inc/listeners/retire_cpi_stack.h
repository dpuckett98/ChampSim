#ifndef RETIRE_CPI_STACK_H
#define RETIRE_CPI_STACK_H

#include <array>
#include <cstdint>
#include <deque>

#include "instruction.h"
#include "listeners/cpi_stack.h"
#include "modules.h"

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
 * Counts reset at the start of each phase and are printed for each core at the end of every non-warmup phase.
 *
 * Model name: ``RETIRE_CPI_STACK``. No parameters.
 */
class retire_cpi_stack : public cpi_stack<4>
{
public:
  enum category : std::size_t { RETIRING, MIS_SPECULATING, STALLED, DRAINED };
  static constexpr std::array<const char*, NUM_CATEGORIES> category_names{"retiring", "mis-speculating", "stalled", "drained"};

  explicit retire_cpi_stack(champsim::modules::ModuleBuilder builder);

  // The category a retire-stage cycle belongs to
  [[nodiscard]] static category classify(std::size_t num_retired, const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict);

  void retire_cycle(uint32_t cpu, uint64_t cycle, std::size_t num_retired, const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict) override;
};
} // namespace champsim

#endif
