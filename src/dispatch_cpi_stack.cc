#include "listeners/dispatch_cpi_stack.h"

#include <cassert>
#include <iterator>

champsim::dispatch_cpi_stack::dispatch_cpi_stack([[maybe_unused]] champsim::modules::ModuleBuilder builder) : cpi_stack("dispatch", category_names) {}

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
  record(cpu, cat, num_dispatched - num_wp_dispatched);

  if (cpu >= stall_cycles.size())
    stall_cycles.resize(cpu + 1);
  if (cat == STALLED)
    ++stall_cycles[cpu].at(classify_stall(backend_stall));
}

void champsim::dispatch_cpi_stack::begin_phase(bool is_warmup)
{
  cpi_stack::begin_phase(is_warmup);
  std::fill(std::begin(stall_cycles), std::end(stall_cycles), std::array<uint64_t, NUM_STALL_REASONS>{});
}

void champsim::dispatch_cpi_stack::end_phase(bool is_warmup)
{
  if (is_warmup)
    return;
  for (std::size_t cpu = 0; cpu < per_cpu.size(); ++cpu) {
    const auto cpu_id = static_cast<uint32_t>(cpu);
    auto lines = format(cpu_id, per_cpu[cpu]);

    // The breakdown goes right after the stalled line (line 0 is the header)
    std::vector<std::string> breakdown;
    for (std::size_t i = 0; i < NUM_STALL_REASONS; ++i)
      breakdown.push_back(format_category(cpu_id, stall_reason_names[i], cpu < stall_cycles.size() ? stall_cycles[cpu][i] : 0, per_cpu[cpu]));
    lines.insert(std::next(std::begin(lines), 1 + STALLED + 1), std::begin(breakdown), std::end(breakdown));

    for (const auto& line : lines)
      fmt::print(*std_out, "{}\n", line);
  }
}

static champsim::modules::listener::register_module<champsim::dispatch_cpi_stack> dispatch_cpi_stack_reg("DISPATCH_CPI_STACK");
