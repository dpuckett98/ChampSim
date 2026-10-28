#include <catch.hpp>
#include <optional>

#include "cache.h"
#include "defaults.hpp"
#include "mocks.hpp"
#include "ooo_cpu.h"
#include "register_allocator.h"
#include "wp_source_mock.hpp"

using champsim::test::mock_wp_source;
using champsim::test::wp_mock_indirect_branch;
using champsim::test::wp_mock_instr;
using champsim::test::wp_mock_ip;

SCENARIO("Squashing wrong-path renames restores the wrong-path RAT and frees their physical registers")
{
  GIVEN("A correct-path write and wrong-path writes to the same and to a new architectural register")
  {
    constexpr unsigned long PHYSICALREGS = 16;
    RegisterAllocator ra{PHYSICALREGS};

    auto p_old = ra.rename_src_register(5); // value from before the trace starts
    auto p_cp = ra.rename_dest_register(5, 1);

    REQUIRE(ra.rename_src_register(5, true) == p_cp); // WP instrs. see older CP renames
    auto p_wp5 = ra.rename_dest_register(5, 2, true);
    auto p_wp7 = ra.rename_dest_register(7, 3, true);

    THEN("Wrong-path renames are visible to the wrong path only")
    {
      REQUIRE(ra.count_free_registers() == PHYSICALREGS - 4);
      REQUIRE(ra.rename_src_register(5, true) == p_wp5);
      REQUIRE(ra.rename_src_register(5) == p_cp);
      REQUIRE(ra.isAllocated(7, true));
      REQUIRE_FALSE(ra.isAllocated(7));
    }

    WHEN("The wrong-path instructions are squashed")
    {
      ra.free_register(p_wp5);
      ra.free_register(p_wp7);
      ra.reset_wrong_path_RAT();

      THEN("The wrong-path RAT matches the correct-path RAT again")
      {
        REQUIRE(ra.rename_src_register(5, true) == p_cp);
        REQUIRE_FALSE(ra.isAllocated(7, true));
      }

      THEN("The correct-path RAT is unchanged") { REQUIRE(ra.rename_src_register(5) == p_cp); }

      THEN("The wrong-path physical registers are freed")
      {
        REQUIRE(ra.count_free_registers() == PHYSICALREGS - 2);
        REQUIRE_FALSE(ra.isValid(p_wp5));
        REQUIRE_FALSE(ra.isValid(p_wp7));
      }

      AND_WHEN("The correct-path write retires")
      {
        ra.retire_dest_register(p_cp);

        THEN("Only the mapping it replaced is freed") { REQUIRE(ra.count_free_registers() == PHYSICALREGS - 1); }

        AND_WHEN("A younger correct-path write to the same register retires")
        {
          auto p_next = ra.rename_dest_register(5, 4);
          ra.retire_dest_register(p_next);

          THEN("The backend RAT held the correct-path mapping, not a wrong-path one")
          {
            REQUIRE(ra.count_free_registers() == PHYSICALREGS - 1);
            REQUIRE(ra.rename_src_register(5) == p_next);
          }
        }
      }
    }
    (void)p_old;
  }
}

SCENARIO("A flush in the core restores the wrong-path RAT and frees wrong-path physical registers")
{
  GIVEN("A mispredicted branch whose wrong path overwrites a register that the correct path reads after the flush")
  {
    constexpr long load_latency = 100;
    constexpr uint8_t load_reg = 1;
    constexpr uint8_t wp_reg_a = 10;
    constexpr uint8_t wp_reg_b = 11;
    constexpr uint8_t cp_reg = 20;

    do_nothing_MRC mock_L1I, mock_L1D{load_latency};
    // The core gives the L1I's prefetcher a chance to operate on every predicted branch; this cache is otherwise unused
    CACHE l1i{champsim::modules::ModuleBuilder{"t203_l1i", "DEFAULT_CACHE", champsim::defaults::default_l1i()}.add_parameter("mshr_size",
                                                                                                                             static_cast<uint32_t>(8))};

    auto core_builder = champsim::defaults::default_core();
    core_builder.add_submodule("workload_source", champsim::modules::ModuleBuilder{"t203_ws", "MOCK_WP_SOURCE"});
    O3_CPU uut{champsim::modules::ModuleBuilder{"t203_core", "DEFAULT_CORE", core_builder}
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))
                   .add_parameter("l1i", static_cast<champsim::modules::cache_module*>(&l1i))};

    auto* source = dynamic_cast<mock_wp_source*>(uut.workload_source_pimpl.at(0));
    REQUIRE(source != nullptr);

    // CP: a long-latency load writing load_reg, then an indirect branch reading it (resolved at execute, so only after the load returns)
    const auto load_ip = wp_mock_ip(0);
    const auto branch_ip = wp_mock_ip(1);
    source->correct_path.push_back(wp_mock_instr(load_ip, {load_reg}, {}, {0xdeadbeef}));
    source->correct_path.push_back(wp_mock_indirect_branch(branch_ip, {load_reg}));
    source->mispredict_ip = branch_ip;

    // WP: overwrites load_reg, and writes registers the CP never writes. Every WP source is already mapped, so no WP instr. allocates a
    // register for a pre-trace value
    const std::array wp_ips{wp_mock_ip(2), wp_mock_ip(3), wp_mock_ip(4), wp_mock_ip(5)};
    source->wrong_path.push_back(wp_mock_instr(wp_ips[0], {load_reg}, {load_reg}));
    source->wrong_path.push_back(wp_mock_instr(wp_ips[1], {wp_reg_a}, {load_reg}));
    source->wrong_path.push_back(wp_mock_instr(wp_ips[2], {wp_reg_b}, {wp_reg_a}));
    source->wrong_path.push_back(wp_mock_instr(wp_ips[3], {load_reg}, {wp_reg_b}));

    // CP after the branch: reads load_reg (which must see the load, not the WP writes), then overwrites it
    const auto cp_a_ip = wp_mock_ip(6);
    const auto cp_b_ip = wp_mock_ip(7);
    source->correct_path.push_back(wp_mock_instr(cp_a_ip, {cp_reg}, {load_reg}));
    source->correct_path.push_back(wp_mock_instr(cp_b_ip, {load_reg}, {cp_reg}));
    constexpr uint64_t num_cp = 4;

    std::array<champsim::operable*, 3> elements{{&uut, &mock_L1I, &mock_L1D}};
    for (auto elem : elements) {
      elem->initialize();
      if (auto* mp = dynamic_cast<champsim::module_phase*>(elem)) {
        mp->begin_phase(false, true);
      }
    }

    auto operate = [&elements]() {
      for (auto elem : elements) {
        elem->_operate();
      }
    };

    // A copy of the renamed ROB entry with the given IP, once it has been scheduled
    auto scheduled = [&uut](uint64_t ip) -> std::optional<ooo_model_instr> {
      auto it = std::find_if(std::begin(uut.ROB), std::end(uut.ROB), [ip](const auto& x) { return x.ip.template to<uint64_t>() == ip && x.scheduled; });
      if (it == std::end(uut.ROB)) {
        return std::nullopt;
      }
      return *it;
    };

    auto all_wp_scheduled = [&]() {
      return std::all_of(std::begin(wp_ips), std::end(wp_ips), [&](auto ip) { return scheduled(ip).has_value(); });
    };

    const auto total_regs = uut.REGISTER_FILE_SIZE;

    WHEN("All wrong-path instructions have been renamed, but the branch has not resolved")
    {
      for (int cycle = 0; cycle < 1000 && !all_wp_scheduled(); ++cycle) {
        operate();
      }
      REQUIRE(all_wp_scheduled());
      REQUIRE(source->flushes == 0);

      const auto load = scheduled(load_ip).value();
      const auto p_load = load.destination_registers.at(0);
      std::array<ooo_model_instr, 4> wp{scheduled(wp_ips[0]).value(), scheduled(wp_ips[1]).value(), scheduled(wp_ips[2]).value(),
                                        scheduled(wp_ips[3]).value()};

      THEN("Wrong-path instructions were renamed through the wrong-path RAT")
      {
        CHECK(wp[0].source_registers.at(0) == p_load); // WP sees the older CP rename
        CHECK(wp[1].source_registers.at(0) == wp[0].destination_registers.at(0));
        CHECK(wp[2].source_registers.at(0) == wp[1].destination_registers.at(0));
        CHECK(wp[3].source_registers.at(0) == wp[2].destination_registers.at(0));
        CHECK(uut.reg_allocator.rename_src_register(load_reg, true) == wp[3].destination_registers.at(0));
        CHECK(uut.reg_allocator.isAllocated(wp_reg_a, true));
      }

      THEN("The correct-path RAT does not see the wrong-path renames")
      {
        CHECK(uut.reg_allocator.rename_src_register(load_reg) == p_load);
        CHECK_FALSE(uut.reg_allocator.isAllocated(wp_reg_a));
        CHECK_FALSE(uut.reg_allocator.isAllocated(wp_reg_b));
      }

      THEN("One physical register is allocated per wrong-path destination")
      {
        CHECK(uut.reg_allocator.count_free_registers() == total_regs - 1 - std::size(wp));
      }

      AND_WHEN("The branch resolves and the wrong path is flushed")
      {
        for (int cycle = 0; cycle < 1000 && source->flushes == 0; ++cycle) {
          operate();
        }
        REQUIRE(source->flushes == 1);

        THEN("No wrong-path instructions remain in the ROB")
        {
          CHECK(std::none_of(std::begin(uut.ROB), std::end(uut.ROB), [](const auto& x) { return x.is_wp; }));
        }

        THEN("The wrong-path RAT is restored to the correct-path mappings")
        {
          CHECK(uut.reg_allocator.rename_src_register(load_reg, true) == p_load);
          CHECK_FALSE(uut.reg_allocator.isAllocated(wp_reg_a, true));
          CHECK_FALSE(uut.reg_allocator.isAllocated(wp_reg_b, true));
        }

        THEN("The correct-path RAT is unchanged") { CHECK(uut.reg_allocator.rename_src_register(load_reg) == p_load); }

        THEN("Every wrong-path physical register is freed")
        {
          CHECK(uut.reg_allocator.count_free_registers() == total_regs - 1);
          for (const auto& instr : wp) {
            CHECK_FALSE(uut.reg_allocator.isValid(instr.destination_registers.at(0)));
          }
        }

        AND_WHEN("The correct path after the branch runs to retirement")
        {
          std::optional<ooo_model_instr> cp_a, cp_b;
          for (int cycle = 0; cycle < 1000 && uut.num_retired < num_cp; ++cycle) {
            operate();
            if (!cp_a.has_value()) {
              cp_a = scheduled(cp_a_ip);
            }
            if (!cp_b.has_value()) {
              cp_b = scheduled(cp_b_ip);
            }
          }
          REQUIRE(uut.num_retired == num_cp);
          REQUIRE(cp_a.has_value());
          REQUIRE(cp_b.has_value());

          THEN("The correct path reads the load's register, not a wrong-path one")
          {
            CHECK(cp_a->source_registers.at(0) == p_load);
            CHECK(cp_b->source_registers.at(0) == cp_a->destination_registers.at(0));
          }

          THEN("Both frontend RATs hold the committed mappings")
          {
            CHECK(uut.reg_allocator.rename_src_register(load_reg) == cp_b->destination_registers.at(0));
            CHECK(uut.reg_allocator.rename_src_register(load_reg, true) == cp_b->destination_registers.at(0));
            CHECK(uut.reg_allocator.rename_src_register(cp_reg) == cp_a->destination_registers.at(0));
            CHECK(uut.reg_allocator.rename_src_register(cp_reg, true) == cp_a->destination_registers.at(0));
          }

          THEN("Only the committed mappings hold physical registers")
          {
            CHECK(uut.reg_allocator.count_free_registers() == total_regs - 2);
            CHECK_FALSE(uut.reg_allocator.isValid(p_load)); // freed when the younger write to load_reg retired
          }
        }
      }
    }
  }
}
