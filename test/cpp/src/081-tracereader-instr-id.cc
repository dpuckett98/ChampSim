#include <algorithm>
#include <catch.hpp>
#include <optional>
#include <vector>

#include "matchers.hpp"
#include "tracereader.h"

namespace
{
struct mock_reader {
  std::optional<ooo_model_instr> next_instr() { return ooo_model_instr{0, input_instr{}}; }
  bool steer(bool, uint64_t) { return false; }
  void flush() {}
};
} // namespace

TEST_CASE("A single tracereader produces monotonically increasing instruction IDs")
{
  champsim::tracereader uut{mock_reader{}};

  std::vector<std::optional<ooo_model_instr>> generated_instrs{};
  std::generate_n(std::back_inserter(generated_instrs), 10, [&uut]() { return uut.next_instr(); });
  std::vector<uint64_t> ids{};
  std::transform(std::begin(generated_instrs), std::end(generated_instrs), std::back_inserter(ids), [](const auto& x) { return x.value().instr_id; });

  REQUIRE_THAT(ids, champsim::test::MonotonicallyIncreasingMatcher{});
}

TEST_CASE("Two tracereaders produce monotonically increasing instruction IDs")
{
  champsim::tracereader uuta{mock_reader{}};
  champsim::tracereader uutb{mock_reader{}};

  std::vector<std::optional<ooo_model_instr>> generated_instrs{};
  std::generate_n(std::back_inserter(generated_instrs), 10, [&uuta]() { return uuta.next_instr(); });
  std::generate_n(std::back_inserter(generated_instrs), 10, [&uutb]() { return uutb.next_instr(); });
  std::vector<uint64_t> ids{};
  std::transform(std::begin(generated_instrs), std::end(generated_instrs), std::back_inserter(ids), [](const auto& x) { return x.value().instr_id; });

  REQUIRE_THAT(ids, champsim::test::MonotonicallyIncreasingMatcher{});
}
