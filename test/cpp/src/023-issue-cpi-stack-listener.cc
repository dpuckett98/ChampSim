#include <catch.hpp>
#include <deque>
#include <sstream>

#include "instruction.h"
#include "listeners/issue_cpi_stack.h"
#include "modules.h"
#include "trace_instruction.h"

namespace
{
using stack = champsim::issue_cpi_stack;
using state = champsim::rob_head_state;
constexpr state nostate = state::NONE;
constexpr auto cache = champsim::stall_cause::WAITING_ON_CACHE;
constexpr auto deps = champsim::stall_cause::WAITING_ON_DEPENDENCIES;
constexpr auto other = champsim::stall_cause::OTHER;

// A ROB of num_executed issued correct-path instrs., then num_cp_waiting unissued correct-path instrs., then num_wp_waiting unissued wrong-path instrs.
std::deque<ooo_model_instr> make_rob(std::size_t num_executed, std::size_t num_cp_waiting, std::size_t num_wp_waiting)
{
  std::deque<ooo_model_instr> rob(num_executed + num_cp_waiting + num_wp_waiting, ooo_model_instr(0, input_instr()));
  for (std::size_t i = 0; i < rob.size(); ++i) {
    rob[i].executed = i < num_executed;
    rob[i].is_wp = i >= num_executed + num_cp_waiting;
  }
  return rob;
}

TEST_CASE("Unused issue slots with a correct-path instruction waiting to issue are stalled") {
  REQUIRE(stack::classify_unused(make_rob(2, 1, 0), false, false) == stack::STALLED);
  REQUIRE(stack::classify_unused(make_rob(2, 1, 3), false, true) == stack::STALLED);
}

TEST_CASE("Unused issue slots with only wrong-path instructions waiting to issue are mis-speculating") {
  REQUIRE(stack::classify_unused(make_rob(2, 0, 3), true, false) == stack::MIS_SPECULATING);
}

TEST_CASE("Unused issue slots with nothing waiting to issue are mis-speculating while recovering, stalled with a full back end, and drained otherwise") {
  REQUIRE(stack::classify_unused(make_rob(0, 0, 0), false, true) == stack::MIS_SPECULATING);
  REQUIRE(stack::classify_unused(make_rob(3, 0, 0), true, false) == stack::STALLED);
  REQUIRE(stack::classify_unused(make_rob(3, 0, 0), false, false) == stack::DRAINED);
  REQUIRE(stack::classify_unused(make_rob(0, 0, 0), false, false) == stack::DRAINED);
}

TEST_CASE("Stalled issue slots take the oldest unissued instruction's cause when one waits, and the ROB head's otherwise") {
  REQUIRE(stack::stall_cause_for(make_rob(2, 1, 0), state::LOAD_IN_FLIGHT, deps) == deps);
  REQUIRE(stack::stall_cause_for(make_rob(2, 1, 0), state::EXECUTING, cache) == cache);
  REQUIRE(stack::stall_cause_for(make_rob(3, 0, 0), state::LOAD_IN_FLIGHT, other) == cache);
  REQUIRE(stack::stall_cause_for(make_rob(3, 0, 0), state::LOAD_WAITING_ON_STORE, cache) == deps);
  REQUIRE(stack::stall_cause_for(make_rob(3, 0, 0), state::WAITING_TO_COMPLETE, cache) == other);
}

TEST_CASE("The issue CPI stack counts slots and correct-path instructions and prints them at the end of a non-warmup phase") {
  std::ostringstream out;
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "ISSUE_CPI_STACK")};
  uut.std_out = &out;

  uut.begin_phase(true);
  uut.issue_cycle(0, 0, 4, 2, 0, make_rob(2, 0, 0), false, false, nostate, other, other);
  uut.end_phase(true);
  REQUIRE(out.str().empty());

  uut.begin_phase(false);
  uut.issue_cycle(0, 1, 4, 3, 1, make_rob(3, 0, 0), false, false, nostate, other, cache);             // 2 issuing, 1 mis-speculating, 1 drained on cache
  uut.issue_cycle(0, 2, 4, 0, 0, make_rob(1, 1, 0), false, false, nostate, cache, other);             // 4 stalled on cache
  uut.issue_cycle(0, 3, 4, 0, 0, make_rob(1, 1, 0), false, false, nostate, deps, other);              // 4 stalled on dependencies
  uut.issue_cycle(0, 4, 4, 0, 0, make_rob(3, 0, 0), true, false, state::LOAD_IN_FLIGHT, other, other); // 4 stalled on cache (ROB head)
  uut.issue_cycle(0, 5, 4, 0, 0, make_rob(0, 0, 0), false, false, nostate, other, other);             // 4 drained other
  uut.end_phase(false);

  REQUIRE(uut.per_cpu.at(0).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{2, 1, 12, 5});
  REQUIRE(uut.per_cpu.at(0).sub_slots == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{8, 4, 0, 1, 4});
  REQUIRE(uut.per_cpu.at(0).cycles == 5);
  REQUIRE(uut.per_cpu.at(0).instrs == 2);

  const std::string expected = "CPU 0 issue CPI stack: slots: 20 (width 4) cycles: 5 instructions: 2 CPI: 2.5\n"
                               "CPU 0 issue issuing: slots: 2 (10%) CPI: 0.25\n"
                               "CPU 0 issue mis-speculating: slots: 1 (5%) CPI: 0.125\n"
                               "CPU 0 issue stalled: slots: 12 (60%) CPI: 1.5\n"
                               "CPU 0 issue stalled waiting on cache: slots: 8 (40%) CPI: 1\n"
                               "CPU 0 issue stalled waiting on dependencies: slots: 4 (20%) CPI: 0.5\n"
                               "CPU 0 issue stalled other: slots: 0 (0%) CPI: 0\n"
                               "CPU 0 issue drained: slots: 5 (25%) CPI: 0.625\n"
                               "CPU 0 issue drained waiting on cache: slots: 1 (5%) CPI: 0.125\n"
                               "CPU 0 issue drained other: slots: 4 (20%) CPI: 0.5\n";
  REQUIRE(out.str() == expected);
}

struct empty_env : champsim::modules::environment_module {
  explicit empty_env(champsim::modules::ModuleBuilder) {}
  std::vector<std::any> view(const std::string&) const override { return {}; }
  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override { return champsim::modules::ModuleBuilder(); }
};

TEST_CASE("The issue CPI stack is available as the ISSUE_CPI_STACK listener model") {
  empty_env env{champsim::modules::ModuleBuilder()};
  auto* uut = champsim::modules::listener::create_instance(champsim::modules::ModuleBuilder("model_cpi_stack", "ISSUE_CPI_STACK"), &env);
  REQUIRE(dynamic_cast<stack*>(uut) != nullptr);
}

} // namespace
