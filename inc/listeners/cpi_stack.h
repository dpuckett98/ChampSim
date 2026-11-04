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
#include "stall_cause.h"

namespace champsim
{
/**
 * Common base for the per-stage CPI stack listeners, which count slots: each core cycle has as many slots as the stage's width (the most instructions it can
 * handle in a cycle). A derived listener records each cycle with record(): the slots that handled a correct-path instruction go to category USEFUL (e.g.
 * retiring), the slots that handled a wrong-path instruction go to MIS_SPECULATING, and the unused slots all go to one category that says why the stage
 * couldn't do more that cycle: MIS_SPECULATING, STALLED, or DRAINED. The counts reset at the start of each phase and are printed for each core at the end
 * of every non-warmup phase as
 *
 *   CPU <cpu> <stage> CPI stack: slots: <total> (width <width>) cycles: <cycles> instructions: <instrs> CPI: <cycles / instrs>
 *   CPU <cpu> <stage> <category>: slots: <slots> (<percent>%) CPI: <slots / width / instrs>
 *
 * so a category's CPI is its share of the total CPI, and the categories' CPIs add up to it.
 *
 * Stalled and drained slots are further broken down by the champsim::stall_cause the derived listener gives for the cycle: stalled into waiting on cache,
 * waiting on dependencies, and other, and drained into waiting on cache and other (a drained cycle's dependencies cause counts as other). The sub-category
 * lines are printed right after their category's line, in the same format, so they sum to it.
 *
 * A derived listener can also break sub-categories down further into D details, each under one sub-category (detail_parent): record() then also names the
 * cycle's detail, which must be under the sub-category its cause picks. A detail's line is named "<sub-category> - <detail>" and is printed right after
 * its sub-category's line, so the details under a sub-category sum to it.
 */
template <std::size_t D = 0>
class cpi_stack : public champsim::modules::listener
{
public:
  enum category : std::size_t { USEFUL, MIS_SPECULATING, STALLED, DRAINED, NUM_CATEGORIES };
  enum subcategory : std::size_t { STALLED_CACHE, STALLED_DEPENDENCIES, STALLED_OTHER, DRAINED_CACHE, DRAINED_OTHER, NUM_SUBCATEGORIES };
  static constexpr std::array<const char*, NUM_SUBCATEGORIES> subcategory_names{"stalled waiting on cache", "stalled waiting on dependencies", "stalled other",
                                                                                "drained waiting on cache", "drained other"};
  static constexpr std::array<std::size_t, NUM_SUBCATEGORIES> subcategory_parent{STALLED, STALLED, STALLED, DRAINED, DRAINED};

  static constexpr std::size_t NUM_DETAILS = D;

  struct counts {
    std::array<uint64_t, NUM_CATEGORIES> slots{};
    std::array<uint64_t, NUM_SUBCATEGORIES> sub_slots{};
    std::array<uint64_t, D> detail_slots{};
    uint64_t cycles = 0;
    uint64_t instrs = 0;
    std::size_t width = 0; // slots per cycle

    [[nodiscard]] uint64_t total_slots() const { return std::accumulate(std::begin(slots), std::end(slots), uint64_t{0}); }
  };

  std::ostream* std_out = &std::cout;
  std::vector<counts> per_cpu;

  // useful_name names the USEFUL category (e.g. "retiring"); detail_names and detail_parent name each detail and the sub-category it is under
  cpi_stack(std::string stage, const char* useful_name, std::array<const char*, D> detail_names = {}, std::array<subcategory, D> detail_parent = {})
      : stage_name(std::move(stage)), category_names{useful_name, "mis-speculating", "stalled", "drained"}, detail_parent(detail_parent)
  {
    for (std::size_t k = 0; k < D; ++k)
      this->detail_names[k] = fmt::format("{} - {}", subcategory_names.at(detail_parent[k]), detail_names[k]);
  }

  // The sub-category of stalled or drained slots with the given cause
  [[nodiscard]] static constexpr subcategory subcategory_of(category unused_cat, stall_cause cause)
  {
    if (unused_cat == STALLED) {
      switch (cause) {
      case stall_cause::WAITING_ON_CACHE:
        return STALLED_CACHE;
      case stall_cause::WAITING_ON_DEPENDENCIES:
        return STALLED_DEPENDENCIES;
      default:
        return STALLED_OTHER;
      }
    }
    return cause == stall_cause::WAITING_ON_CACHE ? DRAINED_CACHE : DRAINED_OTHER;
  }

  // Count one cycle of a core's stage, which has width slots. used_cp slots handled correct-path instructions and used_wp handled wrong-path ones; the
  // rest go to unused_cat, and, when that is STALLED or DRAINED, to the sub-category for cause and, if given, to detail (ignored unless it is under that
  // sub-category).
  void record(uint32_t cpu, std::size_t width, std::size_t used_cp, std::size_t used_wp, category unused_cat, stall_cause cause,
              [[maybe_unused]] std::optional<std::size_t> detail = std::nullopt)
  {
    if (cpu >= per_cpu.size())
      per_cpu.resize(cpu + 1);
    auto& c = per_cpu[cpu];
    c.width = width;
    ++c.cycles;
    c.instrs += used_cp;
    c.slots.at(USEFUL) += used_cp;
    c.slots.at(MIS_SPECULATING) += used_wp;

    const std::size_t used = used_cp + used_wp;
    const std::size_t unused = width > used ? width - used : 0;
    c.slots.at(unused_cat) += unused;
    if (unused_cat == STALLED || unused_cat == DRAINED) {
      const auto sub = subcategory_of(unused_cat, cause);
      c.sub_slots.at(sub) += unused;
      if constexpr (D > 0) {
        if (detail.has_value() && detail_parent.at(*detail) == sub)
          c.detail_slots.at(*detail) += unused;
      }
    }
  }

  // The printed lines for one core's counts
  [[nodiscard]] std::vector<std::string> format(uint32_t cpu, const counts& c) const
  {
    std::vector<std::string> lines;
    lines.push_back(fmt::format("CPU {} {} CPI stack: slots: {} (width {}) cycles: {} instructions: {} CPI: {}", cpu, stage_name, c.total_slots(), c.width,
                                c.cycles, c.instrs, ratio(static_cast<double>(c.cycles), static_cast<double>(c.instrs))));
    for (std::size_t i = 0; i < NUM_CATEGORIES; ++i) {
      lines.push_back(format_category(cpu, category_names[i], c.slots[i], c));
      for (std::size_t j = 0; j < NUM_SUBCATEGORIES; ++j) {
        if (subcategory_parent[j] != i)
          continue;
        lines.push_back(format_category(cpu, subcategory_names[j], c.sub_slots[j], c));
        for (std::size_t k = 0; k < D; ++k) {
          if (detail_parent[k] == j)
            lines.push_back(format_category(cpu, detail_names[k], c.detail_slots[k], c));
        }
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
    return fmt::format("CPU {} {} {}: slots: {} ({}%) CPI: {}", cpu, stage_name, name, slots,
                       ratio(100.0 * static_cast<double>(slots), static_cast<double>(c.total_slots())),
                       ratio(static_cast<double>(slots), static_cast<double>(c.width) * static_cast<double>(c.instrs)));
  }

  std::string stage_name;
  std::array<const char*, NUM_CATEGORIES> category_names;
  std::array<std::string, D> detail_names;
  std::array<subcategory, D> detail_parent;
};
} // namespace champsim

#endif
