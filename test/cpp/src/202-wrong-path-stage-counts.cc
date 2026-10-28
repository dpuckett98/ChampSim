#include <catch.hpp>

#include "cache.h"
#include "defaults.hpp"
#include "mocks.hpp"
#include "ooo_cpu.h"
#include "wp_source_mock.hpp"

using champsim::test::mock_wp_source;
using champsim::test::wp_mock_indirect_branch;
using champsim::test::wp_mock_instr;
using champsim::test::wp_mock_ip;

SCENARIO("Correct-path and wrong-path instructions are counted at each pipeline stage")
{
  GIVEN("A long-latency load feeding a mispredicted indirect branch, followed by wrong-path instructions")
  {
    constexpr long load_latency = 100;
    constexpr uint64_t num_wp = 8;
    constexpr uint64_t num_cp_after_branch = 4;
    constexpr uint8_t load_reg = 1;

    do_nothing_MRC mock_L1I, mock_L1D{load_latency};
    // The core gives the L1I's prefetcher a chance to operate on every predicted branch; this cache is otherwise unused
    CACHE l1i{champsim::modules::ModuleBuilder{"t202_l1i", "DEFAULT_CACHE", champsim::defaults::default_l1i()}.add_parameter("mshr_size",
                                                                                                                             static_cast<uint32_t>(8))};

    auto core_builder = champsim::defaults::default_core();
    core_builder.add_submodule("workload_source", champsim::modules::ModuleBuilder{"t202_ws", "MOCK_WP_SOURCE"});
    O3_CPU uut{champsim::modules::ModuleBuilder{"t202_core", "DEFAULT_CORE", core_builder}
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))
                   .add_parameter("l1i", static_cast<champsim::modules::cache_module*>(&l1i))};

    auto* source = dynamic_cast<mock_wp_source*>(uut.workload_source_pimpl.at(0));
    REQUIRE(source != nullptr);

    // CP: a load writing load_reg, then an indirect branch reading load_reg (resolved at execute, so only after the load returns)
    uint64_t index = 0;
    source->correct_path.push_back(wp_mock_instr(wp_mock_ip(index++), {load_reg}, {}, {0xdeadbeef}));
    source->mispredict_ip = wp_mock_ip(index);
    source->correct_path.push_back(wp_mock_indirect_branch(wp_mock_ip(index++), {load_reg}));

    // WP: a dependent chain that does not touch load_reg or memory, so every WP instr. can complete before the branch resolves
    for (uint64_t i = 0; i < num_wp; ++i) {
      source->wrong_path.push_back(wp_mock_instr(wp_mock_ip(index++), {static_cast<uint8_t>(10 + i % 3)}, {static_cast<uint8_t>(10 + (i + 2) % 3)}));
    }

    // CP after the branch, fetched once the misprediction is resolved
    for (uint64_t i = 0; i < num_cp_after_branch; ++i) {
      source->correct_path.push_back(wp_mock_instr(wp_mock_ip(index++), {static_cast<uint8_t>(20 + i % 2)}, {static_cast<uint8_t>(20 + (i + 1) % 2)}));
    }
    const uint64_t num_cp = 2 + num_cp_after_branch;

    std::array<champsim::operable*, 3> elements{{&uut, &mock_L1I, &mock_L1D}};
    for (auto elem : elements) {
      elem->initialize();
      if (auto* mp = dynamic_cast<champsim::module_phase*>(elem)) {
        mp->begin_phase(false, true);
      }
    }

    WHEN("The core runs until every correct-path instruction retires")
    {
      for (int cycle = 0; cycle < 1000 && uut.num_retired < num_cp; ++cycle) {
        for (auto elem : elements) {
          elem->_operate();
        }
      }

      THEN("The misprediction was taken and flushed exactly once")
      {
        REQUIRE(source->mispredicted);
        REQUIRE(source->flushes == 1);
        REQUIRE(std::empty(source->wrong_path));
        REQUIRE(std::empty(source->correct_path));
        REQUIRE(uut.num_retired == num_cp);
        REQUIRE(std::empty(uut.ROB));
      }

      THEN("Every correct-path instruction is counted once at every stage")
      {
        const auto& counts = uut.sim_stats.stage_counts;
        for (auto stage : {"fetch", "decode", "dispatch", "schedule", "execute", "complete", "retire"}) {
          INFO("stage: " << stage);
          REQUIRE(counts.count(stage) == 1);
          CHECK(counts.at(stage).first == num_cp);
        }
        CHECK(counts.count("dib_hit") == 0);
      }

      THEN("Every wrong-path instruction is counted up to completion, but none retire")
      {
        const auto& counts = uut.sim_stats.stage_counts;
        for (auto stage : {"fetch", "decode", "dispatch", "schedule", "execute", "complete"}) {
          INFO("stage: " << stage);
          REQUIRE(counts.count(stage) == 1);
          CHECK(counts.at(stage).second == num_wp);
        }
        CHECK(counts.at("retire").second == 0);
      }

      THEN("FDIP spends cycles on the wrong path, and stalls once the wrong path runs out while the branch waits on the load")
      {
        CHECK(uut.sim_stats.fdip_wp_stall_cycles > 0);
        CHECK(uut.sim_stats.fdip_wp_cycles > uut.sim_stats.fdip_wp_stall_cycles);
        // the branch cannot resolve until the load returns, so FDIP is on the WP for at least the load latency
        CHECK(uut.sim_stats.fdip_wp_cycles >= static_cast<uint64_t>(load_latency));
      }
    }
  }
}
