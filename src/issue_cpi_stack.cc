#include "listeners/issue_cpi_stack.h"

#include <algorithm>

champsim::issue_cpi_stack::issue_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) : cpi_stack("issue", "issuing") {}

namespace
{
auto oldest_unissued(const std::deque<ooo_model_instr>& rob)
{
  return std::find_if(std::begin(rob), std::end(rob), [](const auto& x) { return !x.executed; });
}
} // namespace

auto champsim::issue_cpi_stack::classify_unused(const std::deque<ooo_model_instr>& rob, bool backend_full, bool recovering_from_mispredict) -> category
{
  // Wrong-path instrs. are all younger than the correct-path ones, so the oldest unissued instr. tells whether any correct-path instr. is waiting
  auto oldest = oldest_unissued(rob);
  if (oldest != std::end(rob))
    return oldest->is_wp ? MIS_SPECULATING : STALLED;

  if (recovering_from_mispredict)
    return MIS_SPECULATING;
  return backend_full ? STALLED : DRAINED;
}

champsim::stall_cause champsim::issue_cpi_stack::stall_cause_for(const std::deque<ooo_model_instr>& rob, champsim::rob_head_state head_state,
                                                                 champsim::stall_cause oldest_unissued_cause)
{
  if (oldest_unissued(rob) != std::end(rob))
    return oldest_unissued_cause;
  return champsim::stall_cause_of(head_state);
}

void champsim::issue_cpi_stack::issue_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t width, std::size_t num_issued, std::size_t num_wp_issued,
                                            const std::deque<ooo_model_instr>& rob, bool backend_full, bool recovering_from_mispredict,
                                            champsim::rob_head_state head_state, champsim::stall_cause oldest_unissued_cause, champsim::stall_cause fetch_cause)
{
  const auto unused_cat = classify_unused(rob, backend_full, recovering_from_mispredict);
  record(cpu, width, num_issued - num_wp_issued, num_wp_issued, unused_cat,
         unused_cat == STALLED ? stall_cause_for(rob, head_state, oldest_unissued_cause) : fetch_cause);
}

static champsim::modules::listener::register_module<champsim::issue_cpi_stack> issue_cpi_stack_reg("ISSUE_CPI_STACK");
