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
 * Common base for the per-stage CPI stack listeners, which count slots: each core cycle has as many slots as the stage's width (the most instructions it can
 * handle in a cycle). A derived listener records each cycle with record(): the slots that handled a correct-path instruction go to category USEFUL (e.g.
 * retiring), the slots that handled a wrong-path instruction go to category MIS_SPECULATING_SLOTS, and the unused slots all go to one category that says
 * why the stage couldn't do more that cycle. The counts reset at the start of each phase and are printed for each core at the end of every non-warmup
 * phase as
 *
 *   CPU <cpu> <stage> CPI stack: slots: <total> (width <width>) cycles: <cycles> instructions: <instrs> CPI: <cycles / instrs>
 *   CPU <cpu> <stage> <category>: slots: <slots> (<percent>%) CPI: <slots / width / instrs>
 *
 * so a category's CPI is its share of the total CPI, and the categories' CPIs add up to it.
 *
 * One category (broken_down) can be further split into M sub-categories: unused slots charged to it also name exactly one sub-category, and the
 * sub-category lines are printed right after that category's line, in the same format, so they sum to it.
 */
template <std::size_t N, std::size_t M = 0>
class cpi_stack : public champsim::modules::listener
{
public:
  static constexpr std::size_t NUM_CATEGORIES = N;
  static constexpr std::size_t NUM_SUBCATEGORIES = M;
  static constexpr std::size_t USEFUL = 0;                // slots that handled a correct-path instruction
  static constexpr std::size_t MIS_SPECULATING_SLOTS = 1; // slots that handled a wrong-path instruction
  static_assert(N >= 2, "a CPI stack needs at least the useful and mis-speculating categories");

  struct counts {
    std::array<uint64_t, N> slots{};
    std::array<uint64_t, M> sub_slots{};
    uint64_t cycles = 0;
    uint64_t instrs = 0;
    std::size_t width = 0; // slots per cycle

    [[nodiscard]] uint64_t total_slots() const { return std::accumulate(std::begin(slots), std::end(slots), uint64_t{0}); }
  };

  std::ostream* std_out = &std::cout;
  std::vector<counts> per_cpu;

  cpi_stack(std::string stage, std::array<const char*, N> names, std::size_t broken_down = 0, std::array<const char*, M> sub_names = {})
      : stage_name(std::move(stage)), category_names(names), broken_down_category(broken_down), subcategory_names(sub_names)
  {
  }

  // Count one cycle of a core's stage, which has width slots. used_cp slots handled correct-path instructions and used_wp handled wrong-path ones; the
  // rest go to unused_cat. sub names the sub-category for unused slots of the broken-down category, and is ignored otherwise.
  void record(uint32_t cpu, std::size_t width, std::size_t used_cp, std::size_t used_wp, std::size_t unused_cat, std::optional<std::size_t> sub = std::nullopt)
  {
    if (cpu >= per_cpu.size())
      per_cpu.resize(cpu + 1);
    auto& c = per_cpu[cpu];
    c.width = width;
    ++c.cycles;
    c.instrs += used_cp;
    c.slots.at(USEFUL) += used_cp;
    c.slots.at(MIS_SPECULATING_SLOTS) += used_wp;

    const std::size_t used = used_cp + used_wp;
    const std::size_t unused = width > used ? width - used : 0;
    c.slots.at(unused_cat) += unused;
    if constexpr (M > 0) {
      if (unused_cat == broken_down_category && sub.has_value())
        c.sub_slots.at(*sub) += unused;
    }
  }

  // The printed lines for one core's counts
  [[nodiscard]] std::vector<std::string> format(uint32_t cpu, const counts& c) const
  {
    std::vector<std::string> lines;
    lines.push_back(fmt::format("CPU {} {} CPI stack: slots: {} (width {}) cycles: {} instructions: {} CPI: {}", cpu, stage_name, c.total_slots(), c.width,
                                c.cycles, c.instrs, ratio(c.cycles, c.instrs)));
    for (std::size_t i = 0; i < N; ++i) {
      lines.push_back(format_category(cpu, category_names[i], c.slots[i], c));
      if (M > 0 && i == broken_down_category) {
        for (std::size_t j = 0; j < M; ++j)
          lines.push_back(format_category(cpu, subcategory_names[j], c.sub_slots[j], c));
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
  [[nodiscard]] static std::string ratio(double num, double denom) { return denom > 0 ? fmt::format("{:.4g}", num / denom) : std::string{"-"}; }

  // The printed line for a category (or sub-category) of slots out of a core's counts
  [[nodiscard]] std::string format_category(uint32_t cpu, std::string_view name, uint64_t slots, const counts& c) const
  {
    return fmt::format("CPU {} {} {}: slots: {} ({}%) CPI: {}", cpu, stage_name, name, slots, ratio(100.0 * static_cast<double>(slots), static_cast<double>(c.total_slots())),
                       ratio(static_cast<double>(slots), static_cast<double>(c.width) * static_cast<double>(c.instrs)));
  }

  std::string stage_name;
  std::array<const char*, N> category_names;
  std::size_t broken_down_category;
  std::array<const char*, M> subcategory_names;
};
} // namespace champsim

#endif
