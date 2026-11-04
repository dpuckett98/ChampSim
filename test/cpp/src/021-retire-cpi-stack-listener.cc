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
  uut.retire_cycle(0, 0, 4, 0, full, false, state::WAITING_TO_SCHEDULE);
  uut.retire_cycle(0, 1, 4, 0, full, false, state::WAITING_TO_COMPLETE);
  uut.retire_cycle(0, 2, 4, 0, full, false, state::WAITING_TO_COMPLETE);
  uut.retire_cycle(0, 3, 4, 0, full, false, state::STORE_PENDING);
  uut.retire_cycle(0, 4, 4, 2, full, false, state::EXECUTING);              // 2 retiring, 2 stalled executing
  uut.retire_cycle(0, 5, 4, 0, make_rob(2, true), false, state::EXECUTING); // mis-speculating: not broken down
  REQUIRE(uut.per_cpu.at(0).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{2, 4, 18, 0});
  REQUIRE(uut.per_cpu.at(0).sub_slots == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{4, 0, 0, 0, 2, 0, 0, 0, 4, 8});
}

TEST_CASE("The retire CPI stack counts slots per core and prints them at the end of a non-warmup phase") {
  std::ostringstream out;
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "RETIRE_CPI_STACK")};
  uut.std_out = &out;

  const auto full = make_rob(4);
  const auto empty = make_rob(0);

  // Warmup: counted, then discarded at the next phase and never printed
  uut.begin_phase(true);
  uut.retire_cycle(0, 0, 2, 2, full, false, none);
  uut.end_phase(true);
  REQUIRE(out.str().empty());

  uut.begin_phase(false);
  uut.retire_cycle(0, 1, 2, 2, full, false, none);                  // 2 retiring
  uut.retire_cycle(0, 2, 2, 1, full, false, state::LOAD_IN_FLIGHT); // 1 retiring, 1 stalled load in flight
  uut.retire_cycle(0, 3, 2, 0, full, false, state::LOAD_IN_FLIGHT); // 2 stalled load in flight
  uut.retire_cycle(0, 4, 2, 0, empty, true, none);                  // 2 mis-speculating
  uut.retire_cycle(1, 4, 2, 0, empty, false, none);                 // CPU 1: 2 drained
  uut.end_phase(false);

  REQUIRE(uut.per_cpu.at(0).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{3, 2, 3, 0});
  REQUIRE(uut.per_cpu.at(0).cycles == 4);
  REQUIRE(uut.per_cpu.at(0).instrs == 3);
  REQUIRE(uut.per_cpu.at(1).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{0, 0, 0, 2});
  REQUIRE(uut.per_cpu.at(0).sub_slots == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{0, 0, 0, 0, 0, 0, 0, 3, 0, 0});

  const std::string expected = "CPU 0 retire CPI stack: slots: 8 (width 2) cycles: 4 instructions: 3 CPI: 1.333\n"
                               "CPU 0 retire retiring: slots: 3 (37.5%) CPI: 0.5\n"
                               "CPU 0 retire mis-speculating: slots: 2 (25%) CPI: 0.3333\n"
                               "CPU 0 retire stalled: slots: 3 (37.5%) CPI: 0.5\n"
                               "CPU 0 retire stalled waiting to schedule: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled schedule blocked: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled waiting for operands: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled waiting to issue: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled executing: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled load waiting on store: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled load not issued: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled load in flight: slots: 3 (37.5%) CPI: 0.5\n"
                               "CPU 0 retire stalled store pending: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled waiting to complete: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 retire drained: slots: 0 (0%) CPI: 0\n"
                               "CPU 1 retire CPI stack: slots: 2 (width 2) cycles: 1 instructions: 0 CPI: -\n"
                               "CPU 1 retire retiring: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire mis-speculating: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting to schedule: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled schedule blocked: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting for operands: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting to issue: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled executing: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled load waiting on store: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled load not issued: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled load in flight: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled store pending: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting to complete: slots: 0 (0%) CPI: -\n"
                               "CPU 1 retire drained: slots: 2 (100%) CPI: -\n";
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
