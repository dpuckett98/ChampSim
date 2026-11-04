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
 * Listener that builds a top-level CPI stack at the issue stage (ROB -> execution), in slots: each cycle has execute-width slots (see champsim::cpi_stack).
 * Each slot that issued a correct-path instruction is issuing, and each that issued a wrong-path instruction is mis-speculating. The unused slots of a cycle
 * all go to one category, decided by the oldest instruction left unissued:
 * - stalled: it is a correct-path instruction waiting on its operands, scheduling, or a free physical register, or, with no instruction waiting to issue,
 *   the ROB, LQ, or SQ is full and blocks dispatch;
 * - mis-speculating: it is on the wrong path or, with no instruction waiting to issue, the core is recovering from a misprediction;
 * - drained: no instruction is waiting to issue and the front end isn't supplying any.
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

  // The category of an issue-stage cycle's unused slots, given the ROB after issue
  [[nodiscard]] static category classify_unused(const std::deque<ooo_model_instr>& rob, bool backend_full, bool recovering_from_mispredict);

  void issue_cycle(uint32_t cpu, uint64_t cycle, std::size_t width, std::size_t num_issued, std::size_t num_wp_issued, const std::deque<ooo_model_instr>& rob,
                   bool backend_full, bool recovering_from_mispredict) override;
};
} // namespace champsim

#endif
