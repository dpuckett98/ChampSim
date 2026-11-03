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

TEST_CASE("A retire cycle that retires instructions is retiring") {
  REQUIRE(stack::classify(1, make_rob(4), false) == stack::RETIRING);
  REQUIRE(stack::classify(3, make_rob(4), true) == stack::RETIRING);
}

TEST_CASE("An idle retire cycle with a correct-path ROB head is stalled") {
  REQUIRE(stack::classify(0, make_rob(4), false) == stack::STALLED);
  REQUIRE(stack::classify(0, make_rob(4), true) == stack::STALLED);
}

TEST_CASE("An idle retire cycle with an empty ROB is mis-speculating while recovering from a mispredict, and drained otherwise") {
  REQUIRE(stack::classify(0, make_rob(0), true) == stack::MIS_SPECULATING);
  REQUIRE(stack::classify(0, make_rob(0), false) == stack::DRAINED);
}

TEST_CASE("An idle retire cycle with a wrong-path ROB head is mis-speculating") {
  REQUIRE(stack::classify(0, make_rob(2, true), false) == stack::MIS_SPECULATING);
}

TEST_CASE("A stalled retire cycle is broken down by the ROB head's state, and other cycles aren't") {
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "RETIRE_CPI_STACK")};
  const auto full = make_rob(4);
  uut.begin_phase(false);
  uut.retire_cycle(0, 0, 0, full, false, state::WAITING_TO_SCHEDULE);
  uut.retire_cycle(0, 1, 0, full, false, state::WAITING_TO_COMPLETE);
  uut.retire_cycle(0, 2, 0, full, false, state::WAITING_TO_COMPLETE);
  uut.retire_cycle(0, 3, 0, full, false, state::STORE_PENDING);
  uut.retire_cycle(0, 4, 2, full, false, state::EXECUTING);           // retiring: not broken down
  uut.retire_cycle(0, 5, 0, make_rob(2, true), false, state::EXECUTING); // mis-speculating: not broken down
  REQUIRE(uut.per_cpu.at(0).cycles == std::array<uint64_t, stack::NUM_CATEGORIES>{1, 1, 4, 0});
  REQUIRE(uut.per_cpu.at(0).sub_cycles == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{1, 0, 0, 0, 0, 0, 0, 0, 1, 2});
}

TEST_CASE("The retire CPI stack counts cycles per core and prints them at the end of a non-warmup phase") {
  std::ostringstream out;
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "RETIRE_CPI_STACK")};
  uut.std_out = &out;

  const auto full = make_rob(4);
  const auto empty = make_rob(0);

  // Warmup: counted, then discarded at the next phase and never printed
  uut.begin_phase(true);
  uut.retire_cycle(0, 0, 2, full, false, none);
  uut.end_phase(true);
  REQUIRE(out.str().empty());

  uut.begin_phase(false);
  uut.retire_cycle(0, 1, 2, full, false, none);  // retiring
  uut.retire_cycle(0, 2, 2, full, false, none);  // retiring
  uut.retire_cycle(0, 3, 0, full, false, state::LOAD_IN_FLIGHT);  // stalled
  uut.retire_cycle(0, 4, 0, empty, true, none);  // mis-speculating
  uut.retire_cycle(1, 4, 0, empty, false, none); // CPU 1: drained
  uut.end_phase(false);

  REQUIRE(uut.per_cpu.at(0).cycles == std::array<uint64_t, stack::NUM_CATEGORIES>{2, 1, 1, 0});
  REQUIRE(uut.per_cpu.at(0).instrs == 4);
  REQUIRE(uut.per_cpu.at(1).cycles == std::array<uint64_t, stack::NUM_CATEGORIES>{0, 0, 0, 1});
  REQUIRE(uut.per_cpu.at(0).sub_cycles == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{0, 0, 0, 0, 0, 0, 0, 1, 0, 0});

  const std::string expected = "CPU 0 retire CPI stack: cycles: 4 instructions: 4 CPI: 1\n"
                               "CPU 0 retire retiring: cycles: 2 (50%) CPI: 0.5\n"
                               "CPU 0 retire mis-speculating: cycles: 1 (25%) CPI: 0.25\n"
                               "CPU 0 retire stalled: cycles: 1 (25%) CPI: 0.25\n"
                               "CPU 0 retire stalled waiting to schedule: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled schedule blocked: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled waiting for operands: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled waiting to issue: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled executing: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled load waiting on store: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled load not issued: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled load in flight: cycles: 1 (25%) CPI: 0.25\n"
                               "CPU 0 retire stalled store pending: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire stalled waiting to complete: cycles: 0 (0%) CPI: 0\n"
                               "CPU 0 retire drained: cycles: 0 (0%) CPI: 0\n"
                               "CPU 1 retire CPI stack: cycles: 1 instructions: 0 CPI: -\n"
                               "CPU 1 retire retiring: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire mis-speculating: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting to schedule: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled schedule blocked: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting for operands: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting to issue: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled executing: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled load waiting on store: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled load not issued: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled load in flight: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled store pending: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire stalled waiting to complete: cycles: 0 (0%) CPI: -\n"
                               "CPU 1 retire drained: cycles: 1 (100%) CPI: -\n";
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
