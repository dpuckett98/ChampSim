#include "listeners/dispatch_cpi_stack.h"

#include <cassert>

champsim::dispatch_cpi_stack::dispatch_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) : cpi_stack("dispatch", category_names, STALLED, stall_reason_names)
{
}

auto champsim::dispatch_cpi_stack::classify(std::size_t num_dispatched, std::size_t num_wp_dispatched, const std::deque<ooo_model_instr>& dispatch_buffer,
                                            champsim::backend_stall backend_stall, bool recovering_from_mispredict) -> category
{
  if (num_dispatched > num_wp_dispatched)
    return DISPATCHING;
  if (num_wp_dispatched > 0)
    return MIS_SPECULATING;
  // A full back end blocks dispatch whichever path the head is on, so it takes precedence over a wrong-path head
  if (backend_stall.any())
    return STALLED;
  if (!std::empty(dispatch_buffer) && dispatch_buffer.front().is_wp)
    return MIS_SPECULATING;
  return recovering_from_mispredict ? MIS_SPECULATING : DRAINED;
}

auto champsim::dispatch_cpi_stack::classify_stall(champsim::backend_stall backend_stall) -> stall_reason
{
  assert(backend_stall.any());
  if (backend_stall.rob_full)
    return ROB_FULL;
  if (backend_stall.lq_full)
    return LQ_FULL;
  return SQ_FULL;
}

void champsim::dispatch_cpi_stack::dispatch_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t num_dispatched, std::size_t num_wp_dispatched,
                                                  const std::deque<ooo_model_instr>& dispatch_buffer, champsim::backend_stall backend_stall,
                                                  bool recovering_from_mispredict)
{
  const auto cat = classify(num_dispatched, num_wp_dispatched, dispatch_buffer, backend_stall, recovering_from_mispredict);
  record(cpu, cat, num_dispatched - num_wp_dispatched, cat == STALLED ? std::optional<std::size_t>{classify_stall(backend_stall)} : std::nullopt);
}

static champsim::modules::listener::register_module<champsim::dispatch_cpi_stack> dispatch_cpi_stack_reg("DISPATCH_CPI_STACK");
