#ifndef DISPATCH_CPI_STACK_H
#define DISPATCH_CPI_STACK_H

#include <array>
#include <cstdint>
#include <deque>

#include "instruction.h"
#include "listeners/cpi_stack.h"
#include "modules.h"

namespace champsim
{
/**
 * Listener that builds a top-level CPI stack at the dispatch stage (dispatch buffer -> ROB). Every core cycle is put in exactly one category:
 * - dispatching: at least one correct-path instruction was dispatched;
 * - mis-speculating: only wrong-path instructions were dispatched, or nothing was dispatched (and the back end isn't full) and either the oldest waiting
 *   instruction is on the wrong path or the core is recovering from a misprediction (no correct-path instruction has been dispatched since the flush);
 * - stalled: nothing was dispatched because the oldest waiting instruction (on either path) doesn't fit in the ROB, LQ, or SQ;
 * - drained: nothing was dispatched because the front end had no instruction ready to dispatch.
 *
 * Counts reset at the start of each phase and are printed for each core at the end of every non-warmup phase. The instruction count is the number of
 * correct-path instructions dispatched.
 *
 * Model name: ``DISPATCH_CPI_STACK``. No parameters.
 */
class dispatch_cpi_stack : public cpi_stack<4>
{
public:
  enum category : std::size_t { DISPATCHING, MIS_SPECULATING, STALLED, DRAINED };
  static constexpr std::array<const char*, NUM_CATEGORIES> category_names{"dispatching", "mis-speculating", "stalled", "drained"};

  explicit dispatch_cpi_stack(champsim::modules::ModuleBuilder builder);

  // The category a dispatch-stage cycle belongs to
  [[nodiscard]] static category classify(std::size_t num_dispatched, std::size_t num_wp_dispatched, const std::deque<ooo_model_instr>& dispatch_buffer,
                                         bool backend_full, bool recovering_from_mispredict);

  void dispatch_cycle(uint32_t cpu, uint64_t cycle, std::size_t num_dispatched, std::size_t num_wp_dispatched, const std::deque<ooo_model_instr>& dispatch_buffer,
                      bool backend_full, bool recovering_from_mispredict) override;
};
} // namespace champsim

#endif
