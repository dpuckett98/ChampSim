#include <catch.hpp>

#include "dram_stats.h"
#include "modules.h"
#include "json_stat_builder.h"

namespace
{
// The performance counter lines format_plaintext prints after the other stats, for a dram_stats whose performance counters are all zero
std::vector<std::string> with_zero_perf_counters(std::vector<std::string> expected, const std::string& name = "test_channel")
{
  expected.push_back(name + " READS:          0 AVG READ LATENCY: - cycles RQ FULL:          0");
  expected.push_back(name + " AVG RQ OCCUPANCY: - AVG WQ OCCUPANCY: - WRITE MODE SWITCHES:          0");
  return expected;
}
} // namespace

TEST_CASE("An empty DRAM stats prints zero")
{
  dram_stats given{};
  given.name = "test_channel";

  std::vector<std::string> expected{"test_channel RQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  AVG DBUS CONGESTED CYCLE: -",
                                    "test_channel WQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  FULL:          0",
                                    "test_channel REFRESHES ISSUED: -"};

  REQUIRE_THAT(champsim::modules::memory_controller_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("The DRAM RQ row buffer hit counter increments the printed stats")
{
  dram_stats given{};
  given.name = "test_channel";
  given.RQ_ROW_BUFFER_HIT = 255;

  std::vector<std::string> expected{"test_channel RQ ROW_BUFFER_HIT:        255",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  AVG DBUS CONGESTED CYCLE: -",
                                    "test_channel WQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  FULL:          0",
                                    "test_channel REFRESHES ISSUED: -"};

  REQUIRE_THAT(champsim::modules::memory_controller_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("The DRAM RQ row buffer miss counter increments the printed stats")
{
  dram_stats given{};
  given.name = "test_channel";
  given.RQ_ROW_BUFFER_MISS = 255;

  std::vector<std::string> expected{"test_channel RQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:        255",
                                    "  AVG DBUS CONGESTED CYCLE: -",
                                    "test_channel WQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  FULL:          0",
                                    "test_channel REFRESHES ISSUED: -"};

  REQUIRE_THAT(champsim::modules::memory_controller_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("The DRAM WQ row buffer hit counter increments the printed stats")
{
  dram_stats given{};
  given.name = "test_channel";
  given.WQ_ROW_BUFFER_HIT = 255;

  std::vector<std::string> expected{"test_channel RQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  AVG DBUS CONGESTED CYCLE: -",
                                    "test_channel WQ ROW_BUFFER_HIT:        255",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  FULL:          0",
                                    "test_channel REFRESHES ISSUED: -"};

  REQUIRE_THAT(champsim::modules::memory_controller_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("The DRAM WQ row buffer miss counter increments the printed stats")
{
  dram_stats given{};
  given.name = "test_channel";
  given.WQ_ROW_BUFFER_MISS = 255;

  std::vector<std::string> expected{"test_channel RQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  AVG DBUS CONGESTED CYCLE: -",
                                    "test_channel WQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:        255",
                                    "  FULL:          0",
                                    "test_channel REFRESHES ISSUED: -"};

  REQUIRE_THAT(champsim::modules::memory_controller_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("The DRAM WQ full counter increments the printed stats")
{
  dram_stats given{};
  given.name = "test_channel";
  given.WQ_FULL = 255;

  std::vector<std::string> expected{"test_channel RQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  AVG DBUS CONGESTED CYCLE: -",
                                    "test_channel WQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  FULL:        255",
                                    "test_channel REFRESHES ISSUED: -"};

  REQUIRE_THAT(champsim::modules::memory_controller_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("The DRAM dbus congestion counters increment the printed stats")
{
  dram_stats given{};
  given.name = "test_channel";
  given.dbus_cycle_congested = 100;
  given.dbus_count_congested = 100;

  std::vector<std::string> expected{"test_channel RQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  AVG DBUS CONGESTED CYCLE: 1",
                                    "test_channel WQ ROW_BUFFER_HIT:          0",
                                    "  ROW_BUFFER_MISS:          0",
                                    "  FULL:          0",
                                    "test_channel REFRESHES ISSUED: -"};

  REQUIRE_THAT(champsim::modules::memory_controller_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("The DRAM refresh counters increment the printed stats")
{
  dram_stats given{};
  given.name = "test_channel";
  given.refresh_cycles = 100;

  std::vector<std::string> expected{"test_channel RQ ROW_BUFFER_HIT:          0", "  ROW_BUFFER_MISS:          0", "  AVG DBUS CONGESTED CYCLE: -",
                                    "test_channel WQ ROW_BUFFER_HIT:          0", "  ROW_BUFFER_MISS:          0", "  FULL:          0",
                                    "test_channel REFRESHES ISSUED:        100"};

  REQUIRE_THAT(champsim::modules::memory_controller_module::format_plaintext(given), Catch::Matchers::RangeEquals(with_zero_perf_counters(expected)));
}

TEST_CASE("DRAM performance counters are printed")
{
  dram_stats given{};
  given.name = "test_channel";
  given.reads_completed = 4;
  given.total_read_latency = 600;
  given.RQ_FULL = 3;
  given.cycles = 10;
  given.rq_occupancy_sum = 25;
  given.wq_occupancy_sum = 5;
  given.write_mode_switches = 2;

  auto lines = champsim::modules::memory_controller_module::format_plaintext(given);
  const auto has = [&lines](const std::string& line) { return std::find(std::begin(lines), std::end(lines), line) != std::end(lines); };
  REQUIRE(has("test_channel READS:          4 AVG READ LATENCY: 150 cycles RQ FULL:          3"));
  REQUIRE(has("test_channel AVG RQ OCCUPANCY: 2.5 AVG WQ OCCUPANCY: 0.5 WRITE MODE SWITCHES:          2"));

  champsim::json_stat_builder b;
  champsim::modules::memory_controller_module::format_json(given, b);
  REQUIRE(b.json().at("READS") == 4);
  REQUIRE(b.json().at("RQ FULL") == 3);
  REQUIRE(b.json().at("WRITE MODE SWITCHES") == 2);
}
