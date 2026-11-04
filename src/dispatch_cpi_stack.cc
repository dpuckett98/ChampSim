#include "listeners/dispatch_cpi_stack.h"

champsim::dispatch_cpi_stack::dispatch_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) : cpi_stack("dispatch", "dispatching") {}

auto champsim::dispatch_cpi_stack::classify_unused(const std::deque<ooo_model_instr>& dispatch_buffer, champsim::backend_stall backend_stall,
                                                   bool recovering_from_mispredict) -> category
{
  // A full back end blocks dispatch whichever path the head is on, so it takes precedence over a wrong-path head
  if (backend_stall.any())
    return STALLED;
  if (!std::empty(dispatch_buffer) && dispatch_buffer.front().is_wp)
    return MIS_SPECULATING;
  return recovering_from_mispredict ? MIS_SPECULATING : DRAINED;
}

void champsim::dispatch_cpi_stack::dispatch_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t width, std::size_t num_dispatched,
                                                  std::size_t num_wp_dispatched, const std::deque<ooo_model_instr>& dispatch_buffer,
                                                  champsim::backend_stall backend_stall, bool recovering_from_mispredict, champsim::rob_head_state head_state,
                                                  champsim::stall_cause fetch_cause)
{
  const auto unused_cat = classify_unused(dispatch_buffer, backend_stall, recovering_from_mispredict);
  record(cpu, width, num_dispatched - num_wp_dispatched, num_wp_dispatched, unused_cat,
         unused_cat == STALLED ? champsim::stall_cause_of(head_state) : fetch_cause);
}

static champsim::modules::listener::register_module<champsim::dispatch_cpi_stack> dispatch_cpi_stack_reg("DISPATCH_CPI_STACK");
