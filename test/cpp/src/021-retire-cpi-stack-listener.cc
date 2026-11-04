#include <catch.hpp>
#include <deque>
#include <sstream>

#include "instruction.h"
#include "listeners/retire_cpi_stack.h"
#include "modules.h"
#include "trace_instruction.h"

namespace
{
using stack = champsim::retire_cpi_stack;
using state = champsim::rob_head_state;
constexpr state none = state::NONE;
constexpr auto cache = champsim::stall_cause::WAITING_ON_CACHE;
constexpr auto other = champsim::stall_cause::OTHER;

std::deque<ooo_model_instr> make_rob(std::size_t n, bool wp = false)
{
  std::deque<ooo_model_instr> rob(n, ooo_model_instr(0, input_instr()));
  for (auto& instr : rob)
    instr.is_wp = wp;
  return rob;
}

TEST_CASE("Unused retire slots with a correct-path ROB head are stalled") {
  REQUIRE(stack::classify_unused(make_rob(4), false) == stack::STALLED);
  REQUIRE(stack::classify_unused(make_rob(4), true) == stack::STALLED);
}

TEST_CASE("Unused retire slots with an empty ROB are mis-speculating while recovering from a mispredict, and drained otherwise") {
  REQUIRE(stack::classify_unused(make_rob(0), true) == stack::MIS_SPECULATING);
  REQUIRE(stack::classify_unused(make_rob(0), false) == stack::DRAINED);
}

TEST_CASE("Unused retire slots with a wrong-path ROB head are mis-speculating") {
  REQUIRE(stack::classify_unused(make_rob(2, true), false) == stack::MIS_SPECULATING);
}

TEST_CASE("Stalled retire slots are broken down by the ROB head's state, including the unused slots of a cycle that retired some instructions") {
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "RETIRE_CPI_STACK")};
  const auto full = make_rob(4);
  uut.begin_phase(false);
  uut.retire_cycle(0, 0, 4, 0, full, false, state::LOAD_IN_FLIGHT, other);           // 4 stalled waiting on cache
  uut.retire_cycle(0, 1, 4, 0, full, false, state::LOAD_WAITING_ON_STORE, cache);    // 4 stalled waiting on dependencies
  uut.retire_cycle(0, 2, 4, 0, full, false, state::WAITING_FOR_OPERANDS, other);     // 4 stalled waiting on dependencies
  uut.retire_cycle(0, 3, 4, 0, full, false, state::LOAD_NOT_ISSUED, other);          // 4 stalled other
  uut.retire_cycle(0, 4, 4, 2, full, false, state::EXECUTING, other);                // 2 retiring, 2 stalled other
  uut.retire_cycle(0, 5, 4, 0, make_rob(2, true), false, state::EXECUTING, cache);   // mis-speculating: not broken down
  REQUIRE(uut.per_cpu.at(0).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{2, 4, 18, 0});
  REQUIRE(uut.per_cpu.at(0).sub_slots == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{4, 8, 6, 0, 0});
  REQUIRE(uut.per_cpu.at(0).detail_slots == std::array<uint64_t, stack::NUM_DETAILS>{0, 0, 4, 0, 2, 4, 4, 4, 0, 0});
}

TEST_CASE("Each ROB head state is under the stalled sub-category of its cause") {
  constexpr auto parents = stack::head_state_parents();
  REQUIRE(parents[static_cast<std::size_t>(state::LOAD_IN_FLIGHT)] == stack::STALLED_CACHE);
  REQUIRE(parents[static_cast<std::size_t>(state::WAITING_FOR_OPERANDS)] == stack::STALLED_DEPENDENCIES);
  REQUIRE(parents[static_cast<std::size_t>(state::LOAD_WAITING_ON_STORE)] == stack::STALLED_DEPENDENCIES);
  for (auto s : {state::WAITING_TO_SCHEDULE, state::SCHEDULE_BLOCKED, state::WAITING_TO_ISSUE, state::EXECUTING, state::LOAD_NOT_ISSUED,
                 state::STORE_PENDING, state::WAITING_TO_COMPLETE})
    REQUIRE(parents[static_cast<std::size_t>(s)] == stack::STALLED_OTHER);
}

TEST_CASE("Drained retire slots are waiting on cache when the front end is waiting on the L1I") {
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "RETIRE_CPI_STACK")};
  const auto empty = make_rob(0);
  uut.begin_phase(false);
  uut.retire_cycle(0, 0, 4, 0, empty, false, none, cache);
  uut.retire_cycle(0, 1, 4, 0, empty, false, none, other);
  uut.retire_cycle(0, 2, 4, 0, empty, false, none, champsim::stall_cause::WAITING_ON_DEPENDENCIES); // not a drained cause: other
  uut.retire_cycle(0, 3, 4, 0, empty, true, none, cache);                                            // mis-speculating: not broken down
  REQUIRE(uut.per_cpu.at(0).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{0, 4, 0, 12});
  REQUIRE(uut.per_cpu.at(0).sub_slots == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{0, 0, 0, 4, 8});
}

TEST_CASE("The retire CPI stack counts slots per core and prints them at the end of a non-warmup phase") {
  std::ostringstream out;
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "RETIRE_CPI_STACK")};
  uut.std_out = &out;

  const auto full = make_rob(4);
  const auto empty = make_rob(0);

  // Warmup: counted, then discarded at the next phase and never printed
  uut.begin_phase(true);
  uut.retire_cycle(0, 0, 2, 2, full, false, none, other);
  uut.end_phase(true);
  REQUIRE(out.str().empty());

  uut.begin_phase(false);
  uut.retire_cycle(0, 1, 2, 2, full, false, none, other);                  // 2 retiring
  uut.retire_cycle(0, 2, 2, 1, full, false, state::LOAD_IN_FLIGHT, other); // 1 retiring, 1 stalled waiting on cache
  uut.retire_cycle(0, 3, 2, 0, full, false, state::LOAD_IN_FLIGHT, other); // 2 stalled waiting on cache
  uut.retire_cycle(0, 4, 2, 0, empty, true, none, other);                  // 2 mis-speculating
  uut.retire_cycle(1, 4, 2, 0, empty, false, none, cache);                 // CPU 1: 2 drained waiting on cache
  uut.end_phase(false);

  REQUIRE(uut.per_cpu.at(0).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{3, 2, 3, 0});
  REQUIRE(uut.per_cpu.at(0).cycles == 4);
  REQUIRE(uut.per_cpu.at(0).instrs == 3);
  REQUIRE(uut.per_cpu.at(1).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{0, 0, 0, 2});
  REQUIRE(uut.per_cpu.at(0).sub_slots == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{3, 0, 0, 0, 0});
  REQUIRE(uut.per_cpu.at(0).detail_slots == std::array<uint64_t, stack::NUM_DETAILS>{0, 0, 0, 0, 0, 0, 0, 3, 0, 0});

  const std::string expected = "CPU 0 retire CPI stack: slots: 8 (width 2) cycles: 4 instructions: 3 CPI: 1.333\n"
                               "CPU 0 retire retiring: slots: 3 (37.5%) CPI: 0.5\n"
                               "CPU 0 retire mis-speculating: slots: 2 (25%) CPI: 0.3333\n"
                               "CPU 0 retire stalled: slots: 3 (37.5%) CPI: 0.5\n"
                               "CPU 0 retire stalled waiting on cache: slots: 3 (37.5%) CPI: 0.5\n"
                               "CPU 0 retire stalled waiting on cache - load in flight: slots: 3 (37.5%) CPI: 0.5\n"
                               "CPU 0 retire stalled waiting on dependencies: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled waiting on dependencies - waiting for operands: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled waiting on dependencies - load waiting on store: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled other: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled other - waiting to schedule: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled other - schedule blocked: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled other - waiting to issue: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled other - executing: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled other - load not issued: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled other - store pending: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled other - waiting to complete: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire drained: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire drained waiting on cache: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire drained other: slots: 0 (0%) CPI: 0\n"
                               "CPU 1 retire CPI stack: slots: 2 (width 2) cycles: 1 instructions: 0 CPI: -\n"
                               "CPU 1 retire retiring: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire mis-speculating: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting on cache: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting on cache - load in flight: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting on dependencies: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting on dependencies - waiting for operands: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting on dependencies - load waiting on store: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled other: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled other - waiting to schedule: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled other - schedule blocked: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled other - waiting to issue: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled other - executing: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled other - load not issued: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled other - store pending: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled other - waiting to complete: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire drained: slots: 2 (100%) CPI: -\n"
                               "CPU 1 retire drained waiting on cache: slots: 2 (100%) CPI: -\n"
                               "CPU 1 retire drained other: slots: 0 (0%) CPI: -\n";
  REQUIRE(out.str() == expected);
}

// An environment with no modules, to parent listeners created through the module system
struct empty_env : champsim::modules::environment_module {
  explicit empty_env(champsim::modules::ModuleBuilder) {}
  std::vector<std::any> view(const std::string&) const override { return {}; }
  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override { return champsim::modules::ModuleBuilder(); }
};

TEST_CASE("The retire CPI stack is available as the RETIRE_CPI_STACK listener model") {
  empty_env env{champsim::modules::ModuleBuilder()};
  auto* uut = champsim::modules::listener::create_instance(champsim::modules::ModuleBuilder("model_cpi_stack", "RETIRE_CPI_STACK"), &env);
  REQUIRE(dynamic_cast<stack*>(uut) != nullptr);
}

} // namespace
