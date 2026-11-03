#include <catch.hpp>

#include "core_stats.h"
#include "json_stat_builder.h"
#include "modules.h"

namespace
{
// The performance counter lines format_plaintext prints after the other stats, for a cpu_stats whose performance counters are all zero
std::vector<std::string> with_zero_perf_counters(std::vector<std::string> expected, const std::string& name = "test_cpu")
{
  const std::vector<std::string> perf_counters{"Retired instruction mix",
                                               name + " retired loads: 0 (memory operands: 0) stores: 0 (memory operands: 0)",
                                               name + " retired branches: 0 taken: 0",
                                               "BRANCH_DIRECT_JUMP: retired: 0 taken: 0",
                                               "BRANCH_INDIRECT: retired: 0 taken: 0",
                                               "BRANCH_CONDITIONAL: retired: 0 taken: 0",
                                               "BRANCH_DIRECT_CALL: retired: 0 taken: 0",
                                               "BRANCH_INDIRECT_CALL: retired: 0 taken: 0",
                                               "BRANCH_RETURN: retired: 0 taken: 0",
                                               "Core activity",
                                               name + " average occupancy ROB: - LQ: - SQ: - cycles full ROB: 0 LQ: 0 SQ: 0",
                                               name + " mispredict recovery cycles: 0 fetch resteer cycles: 0 mispredicts resolved at decode: 0 at execute: 0",
                                               name + " loads forwarded from an executed store: 0 loads waiting on an older store: 0",
                                               name + " retire width histogram (instrs:cycles):",
                                               name + " issue width histogram (instrs:cycles):",
                                               name + " dispatch width histogram (instrs:cycles):"};
  expected.insert(std::end(expected), std::begin(perf_counters), std::end(perf_counters));
  return expected;
}
} // namespace

TEST_CASE("An empty core stats prints zero")
{
  cpu_stats given{};
  given.name = "test_cpu";

  std::vector<std::string> expected{"test_cpu cumulative IPC: - instructions: 0 cycles: 0",
                                    "test_cpu Branch Prediction Accuracy: -% MPKI: - Average ROB Occupancy at Mispredict: -",
                                    "test_cpu FDIP wrong path cycles: 0 stalled waiting for wrong path instrs.: 0",
                                    "test_cpu WP episodes: 0 ran out of WP instrs.: 0 (-%)",
                                    "test_cpu WP instrs. pulled per episode (instrs:not ran out/ran out):",
                                    "Branch type MPKI",
                                    "BRANCH_DIRECT_JUMP: -",
                                    "BRANCH_INDIRECT: -",
                                    "BRANCH_CONDITIONAL: -",
                                    "BRANCH_DIRECT_CALL: -",
                                    "BRANCH_INDIRECT_CALL: -",
                                    "BRANCH_RETURN: -"};

  REQUIRE_THAT(champsim::modules::core_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
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
                                    "test_cpu WP episodes: 0 ran out of WP instrs.: 0 (-%)",
                                    "test_cpu WP instrs. pulled per episode (instrs:not ran out/ran out):",
                                    "Branch type MPKI",
                                    "BRANCH_DIRECT_JUMP: 0",
                                    "BRANCH_INDIRECT: 0",
                                    "BRANCH_CONDITIONAL: 0",
                                    "BRANCH_DIRECT_CALL: 0",
                                    "BRANCH_INDIRECT_CALL: 0",
                                    "BRANCH_RETURN: 0"};

  REQUIRE_THAT(champsim::modules::core_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("The number of mispredictions modifies the MPKI")
{
  auto num_misses = 255;
  auto [line_index, miss_type, expected_line] =
      GENERATE(as<std::tuple<std::size_t, branch_type, std::string>>{}, std::tuple{6, branch_type::BRANCH_DIRECT_JUMP, "BRANCH_DIRECT_JUMP: 255"},
               std::tuple{7, branch_type::BRANCH_INDIRECT, "BRANCH_INDIRECT: 255"}, std::tuple{8, branch_type::BRANCH_CONDITIONAL, "BRANCH_CONDITIONAL: 255"},
               std::tuple{9, branch_type::BRANCH_DIRECT_CALL, "BRANCH_DIRECT_CALL: 255"},
               std::tuple{10, branch_type::BRANCH_INDIRECT_CALL, "BRANCH_INDIRECT_CALL: 255"}, std::tuple{11, branch_type::BRANCH_RETURN, "BRANCH_RETURN: 255"});

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
                                    "test_cpu WP episodes: 0 ran out of WP instrs.: 0 (-%)",
                                    "test_cpu WP instrs. pulled per episode (instrs:not ran out/ran out):",
                                    "Branch type MPKI",
                                    "BRANCH_DIRECT_JUMP: 0",
                                    "BRANCH_INDIRECT: 0",
                                    "BRANCH_CONDITIONAL: 0",
                                    "BRANCH_DIRECT_CALL: 0",
                                    "BRANCH_INDIRECT_CALL: 0",
                                    "BRANCH_RETURN: 0"};
  expected.at(line_index) = expected_line;

  REQUIRE_THAT(champsim::modules::core_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
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
                                    "test_cpu WP episodes: 0 ran out of WP instrs.: 0 (-%)",
                                    "test_cpu WP instrs. pulled per episode (instrs:not ran out/ran out):",
                                    "Branch type MPKI",
                                    "BRANCH_DIRECT_JUMP: 100",
                                    "BRANCH_INDIRECT: 0",
                                    "BRANCH_CONDITIONAL: 0",
                                    "BRANCH_DIRECT_CALL: 0",
                                    "BRANCH_INDIRECT_CALL: 0",
                                    "BRANCH_RETURN: 0"};

  REQUIRE_THAT(champsim::modules::core_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("Pipeline stage counts are printed in pipeline order")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.stage_counts["retire"] = {10, 0};
  given.stage_counts["fetch"] = {12, 5};
  given.stage_counts["decode"] = {11, 3};

  auto lines = champsim::modules::core_module::format_plaintext(given);
  std::vector<std::string> stage_lines{std::find(std::begin(lines), std::end(lines), "Pipeline stage counts"),
                                      std::find(std::begin(lines), std::end(lines), "Retired instruction mix")};

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

TEST_CASE("WP episode statistics are printed")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.wp_episodes = 4;
  given.wp_episodes_ran_out = 1;
  given.wp_episode_instrs[120] = {0, 1};
  given.wp_episode_instrs[800] = {3, 0};

  auto lines = champsim::modules::core_module::format_plaintext(given);
  REQUIRE(std::find(std::begin(lines), std::end(lines), "test_cpu WP episodes: 4 ran out of WP instrs.: 1 (25%)") != std::end(lines));
  REQUIRE(std::find(std::begin(lines), std::end(lines), "test_cpu WP instrs. pulled per episode (instrs:not ran out/ran out): 120:0/1 800:3/0")
          != std::end(lines));
}

TEST_CASE("Retired instruction mix counters are printed")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.retired_loads = 30;
  given.retired_load_ops = 32;
  given.retired_stores = 20;
  given.retired_store_ops = 21;
  given.retired_branches.set(branch_type::BRANCH_CONDITIONAL, 10);
  given.retired_taken_branches.set(branch_type::BRANCH_CONDITIONAL, 4);
  given.retired_branches.set(branch_type::BRANCH_RETURN, 3);
  given.retired_taken_branches.set(branch_type::BRANCH_RETURN, 3);

  auto lines = champsim::modules::core_module::format_plaintext(given);
  const auto has = [&lines](const std::string& line) { return std::find(std::begin(lines), std::end(lines), line) != std::end(lines); };
  REQUIRE(has("test_cpu retired loads: 30 (memory operands: 32) stores: 20 (memory operands: 21)"));
  REQUIRE(has("test_cpu retired branches: 13 taken: 7"));
  REQUIRE(has("BRANCH_CONDITIONAL: retired: 10 taken: 4"));
  REQUIRE(has("BRANCH_RETURN: retired: 3 taken: 3"));
}

TEST_CASE("Core activity counters are printed")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.occupancy_cycles = 4;
  given.rob_occupancy_sum = 10;
  given.lq_occupancy_sum = 6;
  given.sq_occupancy_sum = 2;
  given.rob_full_cycles = 1;
  given.lq_full_cycles = 2;
  given.sq_full_cycles = 3;
  given.recovery_cycles = 5;
  given.fetch_resteer_cycles = 6;
  given.mispredicts_resolved_at_decode = 7;
  given.mispredicts_resolved_at_execute = 8;
  given.loads_forwarded_from_store = 9;
  given.loads_waiting_on_store = 11;
  given.retire_width_histogram = {3, 0, 1};
  given.issue_width_histogram = {0, 4};
  given.dispatch_width_histogram = {};

  auto lines = champsim::modules::core_module::format_plaintext(given);
  const auto has = [&lines](const std::string& line) { return std::find(std::begin(lines), std::end(lines), line) != std::end(lines); };
  REQUIRE(has("test_cpu average occupancy ROB: 2.5 LQ: 1.5 SQ: 0.5 cycles full ROB: 1 LQ: 2 SQ: 3"));
  REQUIRE(has("test_cpu mispredict recovery cycles: 5 fetch resteer cycles: 6 mispredicts resolved at decode: 7 at execute: 8"));
  REQUIRE(has("test_cpu loads forwarded from an executed store: 9 loads waiting on an older store: 11"));
  REQUIRE(has("test_cpu retire width histogram (instrs:cycles): 0:3 2:1"));
  REQUIRE(has("test_cpu issue width histogram (instrs:cycles): 1:4"));
  REQUIRE(has("test_cpu dispatch width histogram (instrs:cycles):"));
}

TEST_CASE("Performance counters are written to JSON")
{
  cpu_stats given{};
  given.name = "test_cpu";
  given.retired_loads = 30;
  given.retired_branches.set(branch_type::BRANCH_CONDITIONAL, 10);
  given.retired_taken_branches.set(branch_type::BRANCH_CONDITIONAL, 4);
  given.rob_full_cycles = 1;
  given.mispredicts_resolved_at_decode = 7;
  given.retire_width_histogram = {3, 0, 1};

  champsim::json_stat_builder b;
  champsim::modules::core_module::format_json(given, b);

  REQUIRE(b.json().at("retired loads") == 30);
  REQUIRE(b.json().at("retired branches").at("BRANCH_CONDITIONAL").at("retired") == 10);
  REQUIRE(b.json().at("retired branches").at("BRANCH_CONDITIONAL").at("taken") == 4);
  REQUIRE(b.json().at("ROB full cycles") == 1);
  REQUIRE(b.json().at("mispredicts resolved at decode") == 7);
  REQUIRE(b.json().at("retire width histogram") == std::vector<uint64_t>{3, 0, 1});
}

TEST_CASE("Performance counters are subtracted")
{
  cpu_stats lhs{};
  lhs.retired_loads = 30;
  lhs.retired_branches.set(branch_type::BRANCH_CONDITIONAL, 10);
  lhs.retire_width_histogram = {3, 2};
  lhs.rob_full_cycles = 9;
  cpu_stats rhs{};
  rhs.retired_loads = 10;
  rhs.retired_branches.set(branch_type::BRANCH_CONDITIONAL, 4);
  rhs.retire_width_histogram = {1, 2};
  rhs.rob_full_cycles = 4;

  const auto diff = lhs - rhs;
  REQUIRE(diff.retired_loads == 20);
  REQUIRE(diff.retired_branches.value_or(branch_type::BRANCH_CONDITIONAL, 0) == 6);
  REQUIRE(diff.retire_width_histogram == std::vector<uint64_t>{2, 0});
  REQUIRE(diff.rob_full_cycles == 5);
}
