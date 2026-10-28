#include <catch.hpp>
#include <deque>
#include <optional>

#include "cache.h"
#include "defaults.hpp"
#include "mocks.hpp"
#include "ooo_cpu.h"

namespace
{
// A workload source that serves a correct-path (CP) stream, and serves a wrong-path (WP) stream after the core is told that
// the branch at mispredict_ip was mispredicted. Once the WP stream is exhausted it returns no instruction, halting fetch until the flush.
struct mock_wp_source_202 : public champsim::modules::workload_source {
  std::deque<ooo_model_instr> correct_path;
  std::deque<ooo_model_instr> wrong_path;
  uint64_t mispredict_ip = 0;

  bool on_wp = false;
  bool mispredicted = false;
  uint64_t last_ip = 0;
  uint64_t next_instr_id = 1;
  int flushes = 0;

  explicit mock_wp_source_202(champsim::modules::ModuleBuilder) {}

  std::optional<ooo_model_instr> next_instr() override
  {
    auto& stream = on_wp ? wrong_path : correct_path;
    if (std::empty(stream)) {
      return std::nullopt;
    }

    auto instr = stream.front();
    stream.pop_front();
    if (on_wp) {
      instr.set_wp_event_flags();
    }
    instr.instr_id = next_instr_id++; // the core relies on instr_ids increasing in fetch order to find instrs. younger than a mispredicted branch
    last_ip = instr.ip.to<uint64_t>();
    return instr;
  }

  bool steer(const bool, const uint64_t) override
  {
    if (!on_wp && !mispredicted && last_ip == mispredict_ip) {
      mispredicted = true;
      on_wp = true;
      return true;
    }
    return false;
  }

  void flush() override
  {
    on_wp = false;
    ++flushes;
  }

  [[nodiscard]] bool eof() const override { return std::empty(correct_path); }
};

champsim::modules::workload_source::register_module<mock_wp_source_202> mock_wp_source_202_reg("MOCK_WP_SOURCE_202");

// IPs are spaced so that no two instrs. share a DIB window
constexpr uint64_t ip_of(uint64_t index) { return 0x10000 + 0x40 * index; }

ooo_model_instr make_instr(uint64_t ip, std::vector<uint8_t> dst_reg, std::vector<uint8_t> src_reg, std::vector<uint64_t> src_mem = {})
{
  return ooo_model_instr{ip, 4, false, false, champsim::address{}, 0, branch_type::NOT_BRANCH, dst_reg, src_reg, {}, src_mem};
}
} // namespace

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
    core_builder.add_submodule("workload_source", champsim::modules::ModuleBuilder{"t202_ws", "MOCK_WP_SOURCE_202"});
    O3_CPU uut{champsim::modules::ModuleBuilder{"t202_core", "DEFAULT_CORE", core_builder}
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))
                   .add_parameter("l1i", static_cast<champsim::modules::cache_module*>(&l1i))};

    auto* source = dynamic_cast<mock_wp_source_202*>(uut.workload_source_pimpl.at(0));
    REQUIRE(source != nullptr);

    // CP: a load writing load_reg, then an indirect branch reading load_reg (resolved at execute, so only after the load returns)
    uint64_t index = 0;
    source->correct_path.push_back(make_instr(ip_of(index++), {load_reg}, {}, {0xdeadbeef}));
    source->mispredict_ip = ip_of(index);
    source->correct_path.push_back(
        ooo_model_instr{ip_of(index++), 4, true, true, champsim::address{0x80000}, 0, branch_type::BRANCH_INDIRECT, {}, {load_reg}, {}, {}});

    // WP: a dependent chain that does not touch load_reg or memory, so every WP instr. can complete before the branch resolves
    for (uint64_t i = 0; i < num_wp; ++i) {
      source->wrong_path.push_back(make_instr(ip_of(index++), {static_cast<uint8_t>(10 + i % 3)}, {static_cast<uint8_t>(10 + (i + 2) % 3)}));
    }

    // CP after the branch, fetched once the misprediction is resolved
    for (uint64_t i = 0; i < num_cp_after_branch; ++i) {
      source->correct_path.push_back(make_instr(ip_of(index++), {static_cast<uint8_t>(20 + i % 2)}, {static_cast<uint8_t>(20 + (i + 1) % 2)}));
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
    }
  }
}
