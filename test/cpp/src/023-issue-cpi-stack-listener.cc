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

TEST_CASE("The issue CPI stack counts slots and correct-path instructions and prints them at the end of a non-warmup phase") {
  std::ostringstream out;
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "ISSUE_CPI_STACK")};
  uut.std_out = &out;

  uut.begin_phase(true);
  uut.issue_cycle(0, 0, 4, 2, 0, make_rob(2, 0, 0), false, false);
  uut.end_phase(true);
  REQUIRE(out.str().empty());

  uut.begin_phase(false);
  uut.issue_cycle(0, 1, 4, 3, 1, make_rob(3, 0, 0), false, false); // 2 issuing, 1 mis-speculating, 1 drained
  uut.issue_cycle(0, 2, 4, 0, 0, make_rob(1, 1, 0), false, false); // 4 stalled
  uut.issue_cycle(0, 3, 4, 0, 0, make_rob(0, 0, 0), false, false); // 4 drained
  uut.issue_cycle(0, 4, 4, 0, 0, make_rob(0, 0, 0), false, false); // 4 drained
  uut.end_phase(false);

  REQUIRE(uut.per_cpu.at(0).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{2, 1, 4, 9});
  REQUIRE(uut.per_cpu.at(0).cycles == 4);
  REQUIRE(uut.per_cpu.at(0).instrs == 2);

  const std::string expected = "CPU 0 issue CPI stack: slots: 16 (width 4) cycles: 4 instructions: 2 CPI: 2\n"
                               "CPU 0 issue issuing: slots: 2 (12.5%) CPI: 0.25\n"
                               "CPU 0 issue mis-speculating: slots: 1 (6.25%) CPI: 0.125\n"
                               "CPU 0 issue stalled: slots: 4 (25%) CPI: 0.5\n"
                               "CPU 0 issue drained: slots: 9 (56.25%) CPI: 1.125\n";
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
