#include "listeners/retire_cpi_stack.h"

champsim::retire_cpi_stack::retire_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) 
    : cpi_stack("retire", "retiring", head_state_names, head_state_parents())
{
}

auto champsim::retire_cpi_stack::classify_unused(const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict) -> category
{
  if (std::empty(rob))
    return recovering_from_mispredict ? MIS_SPECULATING : DRAINED;
  if (rob.front().is_wp)
    return MIS_SPECULATING;
  return STALLED;
}

void champsim::retire_cpi_stack::retire_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t width, std::size_t num_retired,
                                              const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict, champsim::rob_head_state head_state,
                                              champsim::stall_cause fetch_cause)
{
  const auto unused_cat = classify_unused(rob, recovering_from_mispredict);
  std::optional<std::size_t> detail;
  if (head_state != champsim::rob_head_state::NONE)
    detail = static_cast<std::size_t>(head_state);
  record(cpu, width, num_retired, 0, unused_cat, unused_cat == STALLED ? champsim::stall_cause_of(head_state) : fetch_cause, detail);
}

static champsim::modules::listener::register_module<champsim::retire_cpi_stack> retire_cpi_stack_reg("RETIRE_CPI_STACK");
