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
using state = champsim::rob_head_state;
constexpr state nostate = state::NONE;
constexpr auto cache = champsim::stall_cause::WAITING_ON_CACHE;
constexpr auto other = champsim::stall_cause::OTHER;

std::deque<ooo_model_instr> make_buffer(std::size_t n, bool wp = false)
{
  std::deque<ooo_model_instr> buffer(n, ooo_model_instr(0, input_instr()));
  for (auto& instr : buffer)
    instr.is_wp = wp;
  return buffer;
}

constexpr champsim::backend_stall none{};
constexpr champsim::backend_stall rob{true, false, false};
constexpr champsim::backend_stall lq{false, true, false};
constexpr champsim::backend_stall sq{false, false, true};

TEST_CASE("Unused dispatch slots with a wrong-path head that fits in the back end are mis-speculating") {
  REQUIRE(stack::classify_unused(make_buffer(2, true), none, false) == stack::MIS_SPECULATING);
}

TEST_CASE("Unused dispatch slots whose head doesn't fit in the back end are stalled, whichever path the head is on") {
  REQUIRE(stack::classify_unused(make_buffer(2), rob, false) == stack::STALLED);
  REQUIRE(stack::classify_unused(make_buffer(2), rob, true) == stack::STALLED);
  REQUIRE(stack::classify_unused(make_buffer(2, true), rob, false) == stack::STALLED);
  REQUIRE(stack::classify_unused(make_buffer(2), lq, false) == stack::STALLED);
  REQUIRE(stack::classify_unused(make_buffer(2), sq, false) == stack::STALLED);
}

TEST_CASE("Unused dispatch slots with nothing ready are mis-speculating while recovering from a mispredict, and drained otherwise") {
  REQUIRE(stack::classify_unused(make_buffer(0), none, true) == stack::MIS_SPECULATING);
  REQUIRE(stack::classify_unused(make_buffer(2), none, true) == stack::MIS_SPECULATING);
  REQUIRE(stack::classify_unused(make_buffer(0), none, false) == stack::DRAINED);
  REQUIRE(stack::classify_unused(make_buffer(2), none, false) == stack::DRAINED);
}

TEST_CASE("The dispatch CPI stack counts slots and correct-path instructions and prints them at the end of a non-warmup phase") {
  std::ostringstream out;
  stack uut{champsim::modules::ModuleBuilder("test_cpi_stack", "DISPATCH_CPI_STACK")};
  uut.std_out = &out;

  const auto empty = make_buffer(0);

  uut.begin_phase(true);
  uut.dispatch_cycle(0, 0, 4, 2, 0, empty, none, false, nostate, other);
  uut.dispatch_cycle(0, 0, 4, 0, 0, make_buffer(1), rob, false, state::LOAD_IN_FLIGHT, other);
  uut.end_phase(true);
  REQUIRE(out.str().empty());

  uut.begin_phase(false);
  uut.dispatch_cycle(0, 1, 4, 4, 1, empty, none, false, nostate, other);                                     // 3 dispatching, 1 mis-speculating
  uut.dispatch_cycle(0, 2, 4, 2, 2, empty, none, false, nostate, cache);                                     // 2 mis-speculating, 2 drained on cache
  uut.dispatch_cycle(0, 3, 4, 0, 0, make_buffer(1), rob, false, state::LOAD_IN_FLIGHT, cache);               // 4 stalled on cache
  uut.dispatch_cycle(0, 4, 4, 0, 0, make_buffer(1), {true, true, false}, false, state::LOAD_IN_FLIGHT, other); // 4 stalled on cache
  uut.dispatch_cycle(0, 5, 4, 0, 0, make_buffer(1), sq, false, state::LOAD_WAITING_ON_STORE, other);        // 4 stalled on dependencies
  uut.dispatch_cycle(0, 6, 4, 0, 0, make_buffer(1), lq, false, state::EXECUTING, cache);                    // 4 stalled other
  uut.dispatch_cycle(0, 7, 4, 1, 0, empty, rob, false, state::WAITING_TO_COMPLETE, other);                  // 1 dispatching, 3 stalled other
  uut.dispatch_cycle(0, 8, 4, 0, 0, empty, none, false, state::LOAD_IN_FLIGHT, other);                      // 4 drained other
  uut.end_phase(false);

  REQUIRE(uut.per_cpu.at(0).slots == std::array<uint64_t, stack::NUM_CATEGORIES>{4, 3, 19, 6});
  REQUIRE(uut.per_cpu.at(0).sub_slots == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{8, 4, 7, 2, 4});
  REQUIRE(uut.per_cpu.at(0).cycles == 8);
  REQUIRE(uut.per_cpu.at(0).instrs == 4);

  const std::string expected = "CPU 0 dispatch CPI stack: slots: 32 (width 4) cycles: 8 instructions: 4 CPI: 2\n"
                               "CPU 0 dispatch dispatching: slots: 4 (12.5%) CPI: 0.25\n"
                               "CPU 0 dispatch mis-speculating: slots: 3 (9.375%) CPI: 0.1875\n"
                               "CPU 0 dispatch stalled: slots: 19 (59.38%) CPI: 1.188\n"
                               "CPU 0 dispatch stalled waiting on cache: slots: 8 (25%) CPI: 0.5\n"
                               "CPU 0 dispatch stalled waiting on dependencies: slots: 4 (12.5%) CPI: 0.25\n"
                               "CPU 0 dispatch stalled other: slots: 7 (21.88%) CPI: 0.4375\n"
                               "CPU 0 dispatch drained: slots: 6 (18.75%) CPI: 0.375\n"
                               "CPU 0 dispatch drained waiting on cache: slots: 2 (6.25%) CPI: 0.125\n"
                               "CPU 0 dispatch drained other: slots: 4 (12.5%) CPI: 0.25\n";
  REQUIRE(out.str() == expected);

  // Counts, including the breakdown, reset at the start of the next phase
  out.str("");
  uut.begin_phase(false);
  uut.dispatch_cycle(0, 9, 4, 0, 0, make_buffer(1), sq, false, state::EXECUTING, other);
  uut.end_phase(false);
  REQUIRE(uut.per_cpu.at(0).sub_slots == std::array<uint64_t, stack::NUM_SUBCATEGORIES>{0, 0, 4, 0, 0});
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
