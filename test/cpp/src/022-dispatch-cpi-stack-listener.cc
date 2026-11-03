#include <catch.hpp>
#include <deque>
#include <sstream>

#include "instruction.h"
#include "listeners/dispatch_cpi_stack.h"
#include "modules.h"
#include "trace_instruction.h"

namespace
{
using stack = champsim::dispatch_cpi_stack;

std::deque<ooo_model_instr> make_buffer(std::size_t n, bool wp = false)
{
  std::deque<ooo_model_instr> buffer(n, ooo_model_instr(0, input_instr()));
  for (auto& instr : buffer)
    instr.is_wp = wp;
  return buffer;
}

TEST_CASE("A dispatch cycle that dispatches a correct-path instruction is dispatching") {
  REQUIRE(stack::classify(1, 0, make_buffer(4), false, false) == stack::DISPATCHING);
  REQUIRE(stack::classify(3, 2, make_buffer(4, true), true, true) == stack::DISPATCHING);
}

TEST_CASE("A dispatch cycle that dispatches only wrong-path instructions is mis-speculating") {
  REQUIRE(stack::classify(2, 2, make_buffer(4, true), false, false) == stack::MIS_SPECULATING);
}

TEST_CASE("An idle dispatch cycle with a wrong-path head that fits in the back end is mis-speculating") {
  REQUIRE(stack::classify(0, 0, make_buffer(2, true), false, false) == stack::MIS_SPECULATING);
}

TEST_CASE("An idle dispatch cycle whose head doesn't fit in the back end is stalled, whichever path the head is on") {
  REQUIRE(stack::classify(0, 0, make_buffer(2), true, false) == stack::STALLED);
  REQUIRE(stack::classify(0, 0, make_buffer(2), true, true) == stack::STALLED);
  REQUIRE(stack::classify(0, 0, make_buffer(2, true), true, false) == stack::STALLED);
}

TEST_CASE("An idle dispatch cycle with nothing ready is mis-speculating while recovering from a mispredict, and drained otherwise") {
  REQUIRE(stack::classify(0, 0, make_buffer(0), false, true) == stack::MIS_SPECULATING);
  REQUIRE(stack::classify(0, 0, make_buffer(2), false, true) == stack::MIS_SPECULATING);
  REQUIRE(stack::classify(0, 0, make_buffer(0), false, false) == stack::DRAINED);
  REQUIRE(stack::classify(0, 0, make_buffer(2), false, false) == stack::DRAINED);
}

TEST_CASE("The dispatch CPI stack counts cycles and correct-path instructions and prints them at the end of a non-warmup phase") {
  std::ostringstream out;
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "DISPATCH_CPI_STACK")};
  uut.std_out = &out;

  const auto empty = make_buffer(0);

  uut.begin_phase(true);
  uut.dispatch_cycle(0, 0, 2, 0, empty, false, false);
  uut.end_phase(true);
  REQUIRE(out.str().empty());

  uut.begin_phase(false);
  uut.dispatch_cycle(0, 1, 4, 1, empty, false, false);          // dispatching, 3 correct-path instrs.
  uut.dispatch_cycle(0, 2, 2, 2, empty, false, false);          // mis-speculating
  uut.dispatch_cycle(0, 3, 0, 0, make_buffer(1), true, false);  // stalled
  uut.dispatch_cycle(0, 4, 0, 0, empty, false, false);          // drained
  uut.end_phase(false);

  REQUIRE(uut.per_cpu.at(0).cycles == std::array<uint64_t, stack::NUM_CATEGORIES>{1, 1, 1, 1});
  REQUIRE(uut.per_cpu.at(0).instrs == 3);

  const std::string expected = "CPU 0 dispatch CPI stack: cycles: 4 instructions: 3 CPI: 1.333\n"
                               "CPU 0 dispatch dispatching: cycles: 1 (25%) CPI: 0.3333\n"
                               "CPU 0 dispatch mis-speculating: cycles: 1 (25%) CPI: 0.3333\n"
                               "CPU 0 dispatch stalled: cycles: 1 (25%) CPI: 0.3333\n"
                               "CPU 0 dispatch drained: cycles: 1 (25%) CPI: 0.3333\n";
  REQUIRE(out.str() == expected);
}

struct empty_env : champsim::modules::environment_module {
  explicit empty_env(champsim::modules::ModuleBuilder) {}
  std::vector<std::any> view(const std::string&) const override { return {}; }
  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override { return champsim::modules::ModuleBuilder(); }
};

TEST_CASE("The dispatch CPI stack is available as the DISPATCH_CPI_STACK listener model") {
  empty_env env{champsim::modules::ModuleBuilder()};
  auto* uut = champsim::modules::listener::create_instance(champsim::modules::ModuleBuilder("model_cpi_stack", "DISPATCH_CPI_STACK"), &env);
  REQUIRE(dynamic_cast<stack*>(uut) != nullptr);
}

} // namespace
