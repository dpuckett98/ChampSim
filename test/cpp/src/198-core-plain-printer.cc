#include <catch.hpp>

#include "core_stats.h"
#include "json_stat_builder.h"
#include "modules.h"

TEST_CASE("An empty core stats prints zero")
{
  cpu_stats given{};
  given.name = "test_cpu";

  std::vector<std::string> expected{"test_cpu cumulative IPC: - instructions: 0 cycles: 0",
                                    "test_cpu Branch Prediction Accuracy: -% MPKI: - Average ROB Occupancy at Mispredict: -",
                                    "test_cpu FDIP wrong path cycles: 0 stalled waiting for wrong path instrs.: 0",
                                    "Branch type MPKI",
                                    "BRANCH_DIRECT_JUMP: -",
                                    "BRANCH_INDIRECT: -",
                                    "BRANCH_CONDITIONAL: -",
                                    "BRANCH_DIRECT_CALL: -",
                                    "BRANCH_INDIRECT_CALL: -",
                                    "BRANCH_RETURN: -"};

  REQUIRE_THAT(champsim::modules::core_module::format_plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("The number of instructions and cycles modifies the IPC")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.begin_instrs = 0;
  given.begin_cycles = 0;
  given.end_instrs = 100;
  given.end_cycles = 50;

  std::vector<std::string> expected{"test_cpu cumulative IPC: 2 instructions: 100 cycles: 50",
                                    "test_cpu Branch Prediction Accuracy: -% MPKI: 0 Average ROB Occupancy at Mispredict: -",
                                    "test_cpu FDIP wrong path cycles: 0 stalled waiting for wrong path instrs.: 0",
                                    "Branch type MPKI",
                                    "BRANCH_DIRECT_JUMP: 0",
                                    "BRANCH_INDIRECT: 0",
                                    "BRANCH_CONDITIONAL: 0",
                                    "BRANCH_DIRECT_CALL: 0",
                                    "BRANCH_INDIRECT_CALL: 0",
                                    "BRANCH_RETURN: 0"};

  REQUIRE_THAT(champsim::modules::core_module::format_plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("The number of mispredictions modifies the MPKI")
{
  auto num_misses = 255;
  auto [line_index, miss_type, expected_line] =
      GENERATE(as<std::tuple<std::size_t, branch_type, std::string>>{}, std::tuple{4, branch_type::BRANCH_DIRECT_JUMP, "BRANCH_DIRECT_JUMP: 255"},
               std::tuple{5, branch_type::BRANCH_INDIRECT, "BRANCH_INDIRECT: 255"}, std::tuple{6, branch_type::BRANCH_CONDITIONAL, "BRANCH_CONDITIONAL: 255"},
               std::tuple{7, branch_type::BRANCH_DIRECT_CALL, "BRANCH_DIRECT_CALL: 255"},
               std::tuple{8, branch_type::BRANCH_INDIRECT_CALL, "BRANCH_INDIRECT_CALL: 255"}, std::tuple{9, branch_type::BRANCH_RETURN, "BRANCH_RETURN: 255"});

  cpu_stats given{};
  given.name = "test_cpu";
  given.begin_instrs = 0;
  given.begin_cycles = 0;
  given.end_instrs = 1000;
  given.end_cycles = 500;
  given.total_branch_types.set(miss_type, 2 * num_misses);
  given.branch_type_misses.set(miss_type, num_misses);

  std::vector<std::string> expected{"test_cpu cumulative IPC: 2 instructions: 1000 cycles: 500",
                                    "test_cpu Branch Prediction Accuracy: 50% MPKI: 255 Average ROB Occupancy at Mispredict: 0",
                                    "test_cpu FDIP wrong path cycles: 0 stalled waiting for wrong path instrs.: 0",
                                    "Branch type MPKI",
                                    "BRANCH_DIRECT_JUMP: 0",
                                    "BRANCH_INDIRECT: 0",
                                    "BRANCH_CONDITIONAL: 0",
                                    "BRANCH_DIRECT_CALL: 0",
                                    "BRANCH_INDIRECT_CALL: 0",
                                    "BRANCH_RETURN: 0"};
  expected.at(line_index) = expected_line;

  REQUIRE_THAT(champsim::modules::core_module::format_plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("The ROB occupancy modifies the flush penalty")
{
  auto num_misses = 100;

  cpu_stats given{};
  given.name = "test_cpu";
  given.begin_instrs = 0;
  given.begin_cycles = 0;
  given.end_instrs = 1000;
  given.end_cycles = 500;
  given.total_branch_types.set(branch_type::BRANCH_DIRECT_JUMP, 2 * num_misses);
  given.branch_type_misses.set(branch_type::BRANCH_DIRECT_JUMP, num_misses);
  given.total_rob_occupancy_at_branch_mispredict = (uint64_t)(10 * num_misses);

  std::vector<std::string> expected{"test_cpu cumulative IPC: 2 instructions: 1000 cycles: 500",
                                    "test_cpu Branch Prediction Accuracy: 50% MPKI: 100 Average ROB Occupancy at Mispredict: 10",
                                    "test_cpu FDIP wrong path cycles: 0 stalled waiting for wrong path instrs.: 0",
                                    "Branch type MPKI",
                                    "BRANCH_DIRECT_JUMP: 100",
                                    "BRANCH_INDIRECT: 0",
                                    "BRANCH_CONDITIONAL: 0",
                                    "BRANCH_DIRECT_CALL: 0",
                                    "BRANCH_INDIRECT_CALL: 0",
                                    "BRANCH_RETURN: 0"};

  REQUIRE_THAT(champsim::modules::core_module::format_plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("Pipeline stage counts are printed in pipeline order")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.stage_counts["retire"] = {10, 0};
  given.stage_counts["fetch"] = {12, 5};
  given.stage_counts["decode"] = {11, 3};

  auto lines = champsim::modules::core_module::format_plaintext(given);
  std::vector<std::string> stage_lines{std::find(std::begin(lines), std::end(lines), "Pipeline stage counts"), std::end(lines)};

  std::vector<std::string> expected{"Pipeline stage counts", "fetch: correct path: 12 wrong path: 5", "decode: correct path: 11 wrong path: 3",
                                    "retire: correct path: 10 wrong path: 0"};
  REQUIRE_THAT(stage_lines, Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("Pipeline stage counts are written to JSON")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.stage_counts["fetch"] = {12, 5};

  champsim::json_stat_builder b;
  champsim::modules::core_module::format_json(given, b);

  REQUIRE(b.json().at("stage counts").at("fetch").at("correct path") == 12);
  REQUIRE(b.json().at("stage counts").at("fetch").at("wrong path") == 5);
}

TEST_CASE("FDIP wrong path cycles are printed")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.fdip_wp_cycles = 20;
  given.fdip_wp_stall_cycles = 15;

  auto lines = champsim::modules::core_module::format_plaintext(given);
  REQUIRE(std::find(std::begin(lines), std::end(lines), "test_cpu FDIP wrong path cycles: 20 stalled waiting for wrong path instrs.: 15") != std::end(lines));
}

TEST_CASE("FDIP wrong path cycles are written to JSON")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.fdip_wp_cycles = 20;
  given.fdip_wp_stall_cycles = 15;

  champsim::json_stat_builder b;
  champsim::modules::core_module::format_json(given, b);

  REQUIRE(b.json().at("fdip wrong path cycles") == 20);
  REQUIRE(b.json().at("fdip wrong path stall cycles") == 15);
}
