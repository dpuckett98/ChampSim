#ifndef CPI_STACK_H
#define CPI_STACK_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>
#include <fmt/core.h>
#include <fmt/ostream.h>

#include "modules.h"

namespace champsim
{
/**
 * Common base for the per-stage CPI stack listeners. A derived listener puts each core cycle in exactly one of N categories with record(); the counts reset
 * at the start of each phase and are printed for each core at the end of every non-warmup phase as
 *
 *   CPU <cpu> <stage> CPI stack: cycles: <total> instructions: <instrs> CPI: <total / instrs>
 *   CPU <cpu> <stage> <category>: cycles: <cycles> (<percent>%) CPI: <cycles / instrs>
 */
template <std::size_t N>
class cpi_stack : public champsim::modules::listener
{
public:
  static constexpr std::size_t NUM_CATEGORIES = N;

  struct counts {
    std::array<uint64_t, N> cycles{};
    uint64_t instrs = 0;

    [[nodiscard]] uint64_t total_cycles() const { return std::accumulate(std::begin(cycles), std::end(cycles), uint64_t{0}); }
  };

  std::ostream* std_out = &std::cout;
  std::vector<counts> per_cpu;

  cpi_stack(std::string stage, std::array<const char*, N> names) : stage_name(std::move(stage)), category_names(names) {}

  // Count one cycle of the given category for a core, during which the stage processed instrs correct-path instructions
  void record(uint32_t cpu, std::size_t cat, std::size_t instrs)
  {
    if (cpu >= per_cpu.size())
      per_cpu.resize(cpu + 1);
    auto& c = per_cpu[cpu];
    ++c.cycles.at(cat);
    c.instrs += instrs;
  }

  // The printed lines for one core's counts
  [[nodiscard]] std::vector<std::string> format(uint32_t cpu, const counts& c) const
  {
    const auto total = c.total_cycles();
    std::vector<std::string> lines;
    lines.push_back(fmt::format("CPU {} {} CPI stack: cycles: {} instructions: {} CPI: {}", cpu, stage_name, total, c.instrs, ratio(total, c.instrs)));
    for (std::size_t i = 0; i < N; ++i)
      lines.push_back(format_category(cpu, category_names[i], c.cycles[i], c));
    return lines;
  }

  // The printed line for a category (or sub-category) of cycles out of a core's counts
  [[nodiscard]] std::string format_category(uint32_t cpu, std::string_view name, uint64_t cycles, const counts& c) const
  {
    return fmt::format("CPU {} {} {}: cycles: {} ({}%) CPI: {}", cpu, stage_name, name, cycles, ratio(100 * cycles, c.total_cycles()), ratio(cycles, c.instrs));
  }

  void begin_phase([[maybe_unused]] bool is_warmup) override { std::fill(std::begin(per_cpu), std::end(per_cpu), counts{}); }

  void end_phase(bool is_warmup) override
  {
    if (is_warmup)
      return;
    for (std::size_t cpu = 0; cpu < per_cpu.size(); ++cpu) {
      for (const auto& line : format(static_cast<uint32_t>(cpu), per_cpu[cpu]))
        fmt::print(*std_out, "{}\n", line);
    }
  }

private:
  [[nodiscard]] static std::string ratio(uint64_t num, uint64_t denom)
  {
    return denom > 0 ? fmt::format("{:.4g}", static_cast<double>(num) / static_cast<double>(denom)) : std::string{"-"};
  }

  std::string stage_name;
  std::array<const char*, N> category_names;
};
} // namespace champsim

#endif
