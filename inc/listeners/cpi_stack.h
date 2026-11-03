#ifndef CPI_STACK_H
#define CPI_STACK_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <optional>
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
 *
 * One category (broken_down) can be further split into M sub-categories: each cycle recorded in it also names exactly one sub-category, and the sub-category
 * lines are printed right after that category's line, in the same format, so they sum to it.
 */
template <std::size_t N, std::size_t M = 0>
class cpi_stack : public champsim::modules::listener
{
public:
  static constexpr std::size_t NUM_CATEGORIES = N;
  static constexpr std::size_t NUM_SUBCATEGORIES = M;

  struct counts {
    std::array<uint64_t, N> cycles{};
    std::array<uint64_t, M> sub_cycles{};
    uint64_t instrs = 0;

    [[nodiscard]] uint64_t total_cycles() const { return std::accumulate(std::begin(cycles), std::end(cycles), uint64_t{0}); }
  };

  std::ostream* std_out = &std::cout;
  std::vector<counts> per_cpu;

  cpi_stack(std::string stage, std::array<const char*, N> names, std::size_t broken_down = 0, std::array<const char*, M> sub_names = {})
      : stage_name(std::move(stage)), category_names(names), broken_down_category(broken_down), subcategory_names(sub_names)
  {
  }

  // Count one cycle of the given category for a core, during which the stage processed instrs correct-path instructions. sub names the sub-category for a
  // cycle of the broken-down category, and is ignored for the others.
  void record(uint32_t cpu, std::size_t cat, std::size_t instrs, std::optional<std::size_t> sub = std::nullopt)
  {
    if (cpu >= per_cpu.size())
      per_cpu.resize(cpu + 1);
    auto& c = per_cpu[cpu];
    ++c.cycles.at(cat);
    c.instrs += instrs;
    if constexpr (M > 0) {
      if (cat == broken_down_category && sub.has_value())
        ++c.sub_cycles.at(*sub);
    }
  }

  // The printed lines for one core's counts
  [[nodiscard]] std::vector<std::string> format(uint32_t cpu, const counts& c) const
  {
    const auto total = c.total_cycles();
    std::vector<std::string> lines;
    lines.push_back(fmt::format("CPU {} {} CPI stack: cycles: {} instructions: {} CPI: {}", cpu, stage_name, total, c.instrs, ratio(total, c.instrs)));
    for (std::size_t i = 0; i < N; ++i) {
      lines.push_back(format_category(cpu, category_names[i], c.cycles[i], c));
      if (M > 0 && i == broken_down_category) {
        for (std::size_t j = 0; j < M; ++j)
          lines.push_back(format_category(cpu, subcategory_names[j], c.sub_cycles[j], c));
      }
    }
    return lines;
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

  // The printed line for a category (or sub-category) of cycles out of a core's counts
  [[nodiscard]] std::string format_category(uint32_t cpu, std::string_view name, uint64_t cycles, const counts& c) const
  {
    return fmt::format("CPU {} {} {}: cycles: {} ({}%) CPI: {}", cpu, stage_name, name, cycles, ratio(100 * cycles, c.total_cycles()), ratio(cycles, c.instrs));
  }

  std::string stage_name;
  std::array<const char*, N> category_names;
  std::size_t broken_down_category;
  std::array<const char*, M> subcategory_names;
};
} // namespace champsim

#endif
