#include "listeners/retire_cpi_stack.h"

#include <numeric>
#include <fmt/core.h>
#include <fmt/ostream.h>

champsim::retire_cpi_stack::retire_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) {}

uint64_t champsim::retire_cpi_stack::counts::total_cycles() const { return std::accumulate(std::begin(cycles), std::end(cycles), uint64_t{0}); }

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

std::vector<std::string> champsim::retire_cpi_stack::format(uint32_t cpu, const counts& c)
{
  const auto total = c.total_cycles();
  const auto ratio = [](uint64_t num, uint64_t denom) { return denom > 0 ? fmt::format("{:.4g}", static_cast<double>(num) / static_cast<double>(denom)) : std::string{"-"}; };

  std::vector<std::string> lines;
  lines.push_back(fmt::format("CPU {} retire CPI stack: cycles: {} instructions: {} CPI: {}", cpu, total, c.instrs, ratio(total, c.instrs)));
  for (std::size_t i = 0; i < NUM_CATEGORIES; ++i) {
    lines.push_back(fmt::format("CPU {} retire {}: cycles: {} ({}%) CPI: {}", cpu, category_names[i], c.cycles[i], ratio(100 * c.cycles[i], total),
                                ratio(c.cycles[i], c.instrs)));
  }
  return lines;
}

void champsim::retire_cpi_stack::begin_phase([[maybe_unused]] bool is_warmup) { std::fill(std::begin(per_cpu), std::end(per_cpu), counts{}); }

void champsim::retire_cpi_stack::end_phase(bool is_warmup)
{
  if (is_warmup)
    return;
  for (std::size_t cpu = 0; cpu < per_cpu.size(); ++cpu) {
    for (const auto& line : format(static_cast<uint32_t>(cpu), per_cpu[cpu]))
      fmt::print(*std_out, "{}\n", line);
  }
}

void champsim::retire_cpi_stack::retire_cycle(uint32_t cpu, [[maybe_unused]] uint64_t cycle, std::size_t num_retired, const std::deque<ooo_model_instr>& rob,
                                              bool recovering_from_mispredict)
{
  if (cpu >= per_cpu.size())
    per_cpu.resize(cpu + 1);
  auto& c = per_cpu[cpu];
  ++c.cycles[classify(num_retired, rob, recovering_from_mispredict)];
  c.instrs += num_retired;
}

static champsim::modules::listener::register_module<champsim::retire_cpi_stack> retire_cpi_stack_reg("RETIRE_CPI_STACK");
