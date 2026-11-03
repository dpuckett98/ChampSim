#include "listeners/retire_cpi_stack.h"

champsim::retire_cpi_stack::retire_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) : cpi_stack("retire", category_names, STALLED, head_state_names)
{
}

auto champsim::retire_cpi_stack::classify(std::size_t num_retired, const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict) -> category
{
  if (num_retired > 0)
    return RETIRING;
  if (std::empty(rob))
    return recovering_from_mispredict ? MIS_SPECULATING : DRAINED;
  if (rob.front().is_wp)
    return MIS_SPECULATING;
  return STALLED;
}

void champsim::retire_cpi_stack::retire_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t num_retired, const std::deque<ooo_model_instr>& rob,
                                              bool recovering_from_mispredict, champsim::rob_head_state head_state)
{
  std::optional<std::size_t> sub;
  if (head_state != champsim::rob_head_state::NONE)
    sub = static_cast<std::size_t>(head_state);
  record(cpu, classify(num_retired, rob, recovering_from_mispredict), num_retired, sub);
}

static champsim::modules::listener::register_module<champsim::retire_cpi_stack> retire_cpi_stack_reg("RETIRE_CPI_STACK");
