#include "listeners/issue_cpi_stack.h"

#include <algorithm>

champsim::issue_cpi_stack::issue_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) : cpi_stack("issue", category_names) {}

auto champsim::issue_cpi_stack::classify(std::size_t num_issued, std::size_t num_wp_issued, const std::deque<ooo_model_instr>& rob, bool backend_full,
                                         bool recovering_from_mispredict) -> category
{
  if (num_issued > num_wp_issued)
    return ISSUING;
  if (num_wp_issued > 0)
    return MIS_SPECULATING;

  // Wrong-path instrs. are all younger than the correct-path ones, so the oldest unissued instr. tells whether any correct-path instr. is waiting
  auto oldest_unissued = std::find_if(std::begin(rob), std::end(rob), [](const auto& x) { return !x.executed; });
  if (oldest_unissued != std::end(rob))
    return oldest_unissued->is_wp ? MIS_SPECULATING : STALLED;

  if (recovering_from_mispredict)
    return MIS_SPECULATING;
  return backend_full ? STALLED : DRAINED;
}

void champsim::issue_cpi_stack::issue_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t num_issued, std::size_t num_wp_issued,
                                            const std::deque<ooo_model_instr>& rob, bool backend_full, bool recovering_from_mispredict)
{
  record(cpu, classify(num_issued, num_wp_issued, rob, backend_full, recovering_from_mispredict), num_issued - num_wp_issued);
}

static champsim::modules::listener::register_module<champsim::issue_cpi_stack> issue_cpi_stack_reg("ISSUE_CPI_STACK");
