#ifndef DISPATCH_CPI_STACK_H
#define DISPATCH_CPI_STACK_H

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

#include "backend_stall.h"
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
 * Stalled cycles are further broken down by the structure that blocked the head, and each is charged to exactly one, so the three sum to the stalled count.
 * When several are full at once the cycle goes to the first full one in the order ROB, LQ, SQ:
 * - ROB full: the ROB has no free entry;
 * - LQ full: the ROB has room, but the LQ has too few free entries for the head's loads;
 * - SQ full: the ROB and LQ have room, but the SQ has too few free entries for the head's stores.
 *
 * Counts reset at the start of each phase and are printed for each core at the end of every non-warmup phase, with the breakdown lines right after the
 * stalled line. The instruction count is the number of correct-path instructions dispatched.
 *
 * Model name: ``DISPATCH_CPI_STACK``. No parameters.
 */
class dispatch_cpi_stack : public cpi_stack<4>
{
public:
  enum category : std::size_t { DISPATCHING, MIS_SPECULATING, STALLED, DRAINED };
  static constexpr std::array<const char*, NUM_CATEGORIES> category_names{"dispatching", "mis-speculating", "stalled", "drained"};

  enum stall_reason : std::size_t { ROB_FULL, LQ_FULL, SQ_FULL };
  static constexpr std::size_t NUM_STALL_REASONS = 3;
  static constexpr std::array<const char*, NUM_STALL_REASONS> stall_reason_names{"stalled ROB full", "stalled LQ full", "stalled SQ full"};

  // Per-core stalled cycles by stall reason
  std::vector<std::array<uint64_t, NUM_STALL_REASONS>> stall_cycles;

  explicit dispatch_cpi_stack(champsim::modules::ModuleBuilder builder);

  // The category a dispatch-stage cycle belongs to
  [[nodiscard]] static category classify(std::size_t num_dispatched, std::size_t num_wp_dispatched, const std::deque<ooo_model_instr>& dispatch_buffer,
                                         champsim::backend_stall backend_stall, bool recovering_from_mispredict);

  // The structure a stalled cycle is charged to; backend_stall must have at least one structure full
  [[nodiscard]] static stall_reason classify_stall(champsim::backend_stall backend_stall);

  void dispatch_cycle(uint32_t cpu, uint64_t cycle, std::size_t num_dispatched, std::size_t num_wp_dispatched, const std::deque<ooo_model_instr>& dispatch_buffer,
                      champsim::backend_stall backend_stall, bool recovering_from_mispredict) override;

  void begin_phase(bool is_warmup) override;
  void end_phase(bool is_warmup) override;
};
} // namespace champsim

#endif
