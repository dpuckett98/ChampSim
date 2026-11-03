#ifndef ISSUE_CPI_STACK_H
#define ISSUE_CPI_STACK_H

#include <array>
#include <cstdint>
#include <deque>

#include "instruction.h"
#include "listeners/cpi_stack.h"
#include "modules.h"

namespace champsim
{
/**
 * Listener that builds a top-level CPI stack at the issue stage (ROB -> execution). Every core cycle is put in exactly one category:
 * - issuing: at least one correct-path instruction was issued;
 * - mis-speculating: only wrong-path instructions were issued, or nothing was issued and either the oldest unissued instruction is on the wrong path or,
 *   with no instruction waiting to issue, the core is recovering from a misprediction;
 * - stalled: nothing was issued because the oldest unissued (correct-path) instruction is waiting on its operands, scheduling, or a free physical
 *   register, or, with no instruction waiting to issue, because the ROB, LQ, or SQ is full and blocks dispatch;
 * - drained: nothing was issued because no instruction is waiting to issue and the front end isn't supplying any.
 *
 * Counts reset at the start of each phase and are printed for each core at the end of every non-warmup phase. The instruction count is the number of
 * correct-path instructions issued.
 *
 * Model name: ``ISSUE_CPI_STACK``. No parameters.
 */
class issue_cpi_stack : public cpi_stack<4>
{
public:
  enum category : std::size_t { ISSUING, MIS_SPECULATING, STALLED, DRAINED };
  static constexpr std::array<const char*, NUM_CATEGORIES> category_names{"issuing", "mis-speculating", "stalled", "drained"};

  explicit issue_cpi_stack(champsim::modules::ModuleBuilder builder);

  // The category an issue-stage cycle belongs to
  [[nodiscard]] static category classify(std::size_t num_issued, std::size_t num_wp_issued, const std::deque<ooo_model_instr>& rob, bool backend_full,
                                         bool recovering_from_mispredict);

  void issue_cycle(uint32_t cpu, uint64_t cycle, std::size_t num_issued, std::size_t num_wp_issued, const std::deque<ooo_model_instr>& rob, bool backend_full,
                   bool recovering_from_mispredict) override;
};
} // namespace champsim

#endif
