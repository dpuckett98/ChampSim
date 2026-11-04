#include "listeners/retire_cpi_stack.h"

champsim::retire_cpi_stack::retire_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) : cpi_stack("retire", category_names, STALLED, head_state_names)
{
}

static_assert(champsim::retire_cpi_stack::RETIRING == champsim::retire_cpi_stack::USEFUL
              && champsim::retire_cpi_stack::MIS_SPECULATING == champsim::retire_cpi_stack::MIS_SPECULATING_SLOTS);

auto champsim::retire_cpi_stack::classify_unused(const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict) -> category
{
  if (std::empty(rob))
    return recovering_from_mispredict ? MIS_SPECULATING : DRAINED;
  if (rob.front().is_wp)
    return MIS_SPECULATING;
  return STALLED;
}

void champsim::retire_cpi_stack::retire_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t width, std::size_t num_retired,
                                              const std::deque<ooo_model_instr>& rob, bool recovering_from_mispredict, champsim::rob_head_state head_state)
{
  std::optional<std::size_t> sub;
  if (head_state != champsim::rob_head_state::NONE)
    sub = static_cast<std::size_t>(head_state);
  record(cpu, width, num_retired, 0, classify_unused(rob, recovering_from_mispredict), sub);
}

static champsim::modules::listener::register_module<champsim::retire_cpi_stack> retire_cpi_stack_reg("RETIRE_CPI_STACK");
