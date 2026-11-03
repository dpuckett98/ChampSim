#include "listeners/dispatch_cpi_stack.h"

champsim::dispatch_cpi_stack::dispatch_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) : cpi_stack("dispatch", category_names) {}

auto champsim::dispatch_cpi_stack::classify(std::size_t num_dispatched, std::size_t num_wp_dispatched, const std::deque<ooo_model_instr>& dispatch_buffer,
                                            bool backend_full, bool recovering_from_mispredict) -> category
{
  if (num_dispatched > num_wp_dispatched)
    return DISPATCHING;
  if (num_wp_dispatched > 0)
    return MIS_SPECULATING;
  // A full back end blocks dispatch whichever path the head is on, so it takes precedence over a wrong-path head
  if (backend_full)
    return STALLED;
  if (!std::empty(dispatch_buffer) && dispatch_buffer.front().is_wp)
    return MIS_SPECULATING;
  return recovering_from_mispredict ? MIS_SPECULATING : DRAINED;
}

void champsim::dispatch_cpi_stack::dispatch_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t num_dispatched, std::size_t num_wp_dispatched,
                                                  const std::deque<ooo_model_instr>& dispatch_buffer, bool backend_full, bool recovering_from_mispredict)
{
  record(cpu, classify(num_dispatched, num_wp_dispatched, dispatch_buffer, backend_full, recovering_from_mispredict), num_dispatched - num_wp_dispatched);
}

static champsim::modules::listener::register_module<champsim::dispatch_cpi_stack> dispatch_cpi_stack_reg("DISPATCH_CPI_STACK");
