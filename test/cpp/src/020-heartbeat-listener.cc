#include <catch.hpp>
#include <deque>
#include <sstream>
#include <nlohmann/json.hpp>

#include "environment.h"
#include "event_listeners.h"
#include "listeners/heartbeat.h"
#include "instruction.h"
#include "modules.h"
#include "trace_instruction.h"

namespace
{

// A heartbeat that prints into the given stream
champsim::heartbeat make_heartbeat(std::ostream& out, champsim::modules::ModuleBuilder builder = champsim::modules::ModuleBuilder("test_heartbeat", "HEARTBEAT"))
{
  champsim::heartbeat uut{builder};
  uut.std_out = &out;
  return uut;
}

void retire_n(champsim::modules::listener& uut, uint32_t cpu, std::size_t n, uint64_t cycle)
{
  std::deque<ooo_model_instr> fake_instructions(n, ooo_model_instr(0, input_instr()));
  uut.retire(cpu, std::cbegin(fake_instructions), std::cend(fake_instructions), cycle);
}

TEST_CASE("The heartbeat listener prints one line after 10M instructions retired") {
    std::ostringstream stdout{};
    auto uut = make_heartbeat(stdout);

    uut.begin_phase(false);

    for (int i = 0; i < 5000000; ++i)
        retire_n(uut, 0, 2, static_cast<uint64_t>(i));

    std::string res = stdout.str();

    std::string rest = res.substr(res.find('\n')+1);

    REQUIRE_THAT(res, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener prints cumulative and heartbeat IPC correctly after a phase change") {
    std::ostringstream stdout{};
    auto uut = make_heartbeat(stdout);

    uut.begin_phase(true);

    for (int i = 0; i <11000000; ++i) {

        // phase change to simulation
        if (i == 5000000)
          uut.begin_phase(false);

        // warmup behavior (4 IPC), then simulation behavior (2 IPC)
        retire_n(uut, 0, i < 4000000 ? 4 : 2, static_cast<uint64_t>(i));
    }

    std::string res = stdout.str();

    std::string l1 = res.substr(0, res.find('\n')+1);
    std::string rest = res.substr(res.find('\n')+1);
    std::string l2 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);
    std::string l3 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);

    REQUIRE_THAT(l1, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 2499999 heartbeat IPC: 4 cumulative IPC: 4 "));
    REQUIRE_THAT(l2, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 20000000 cycles: 5999999 heartbeat IPC: 2.857 cumulative IPC: 2 "));
    REQUIRE_THAT(l3, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 30000000 cycles: 10999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener prints correctly with multiple CPUs") {
    std::ostringstream stdout{};
    auto uut = make_heartbeat(stdout);

    uut.begin_phase(true);

    for (int i = 0; i < 5000000; ++i) {
        // simulation behavior (2 IPC)
        for (uint32_t cpu = 0; cpu < 4; ++cpu)
            retire_n(uut, cpu, 2, static_cast<uint64_t>(i));
    }

    std::string res = stdout.str();

    std::string l1 = res.substr(0, res.find('\n')+1);
    std::string rest = res.substr(res.find('\n')+1);
    std::string l2 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);
    std::string l3 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);
    std::string l4 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);

    REQUIRE_THAT(l1, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(l2, Catch::Matchers::StartsWith("Heartbeat CPU 1 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(l3, Catch::Matchers::StartsWith("Heartbeat CPU 2 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(l4, Catch::Matchers::StartsWith("Heartbeat CPU 3 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener honors a custom cycles_between_printouts") {
    std::ostringstream stdout{};
    auto uut = make_heartbeat(stdout);
    uut.cycles_between_printouts = 1000000; // 10x more frequent than the default

    uut.begin_phase(false);

    for (int i = 0; i < 500000; ++i)
        retire_n(uut, 0, 2, static_cast<uint64_t>(i));

    std::string res = stdout.str();
    std::string rest = res.substr(res.find('\n') + 1);

    REQUIRE_THAT(res, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 1000000 cycles: 499999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener does not drift when retire batches overshoot the threshold") {
    // Three instructions retire per cycle into a threshold of 10 instructions.
    // Each batch carries the running count past a non-multiple boundary
    // (12, 24, 36, ...).  If the listener snapped its baseline to the
    // current retired count after each printout, the overshoots would
    // compound and the second heartbeat would land at instruction 24
    // instead of 20, etc.  The fix advances the baseline by exact
    // multiples of cycles_between_printouts so heartbeats stay aligned to
    // the configured cadence.
    std::ostringstream stdout{};
    auto uut = make_heartbeat(stdout);
    uut.cycles_between_printouts = 10;

    uut.begin_phase(false);

    constexpr int batches = 20; // 60 instructions total -> 6 heartbeats expected
    for (int i = 0; i < batches; ++i)
        retire_n(uut, 0, 3, static_cast<uint64_t>(i));

    std::string res = stdout.str();
    std::vector<std::string> lines;
    for (size_t pos = 0; pos < res.size(); ) {
        auto next = res.find('\n', pos);
        if (next == std::string::npos) break;
        lines.push_back(res.substr(pos, next - pos));
        pos = next + 1;
    }

    REQUIRE(lines.size() == 6);
    REQUIRE_THAT(lines[0], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 12 "));
    REQUIRE_THAT(lines[1], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 21 "));
    REQUIRE_THAT(lines[2], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 30 "));
    REQUIRE_THAT(lines[3], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 42 "));
    REQUIRE_THAT(lines[4], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 51 "));
    REQUIRE_THAT(lines[5], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 60 "));
}

TEST_CASE("The heartbeat_frequency parameter sets cycles_between_printouts") {
    std::ostringstream stdout{};
    auto uut = make_heartbeat(stdout, champsim::modules::ModuleBuilder("test_heartbeat", "HEARTBEAT").add_parameter("heartbeat_frequency", uint64_t{1234567}));
    REQUIRE(uut.cycles_between_printouts == 1234567U);
}

TEST_CASE("The heartbeat listener falls back to its default cadence when heartbeat_frequency is absent") {
    std::ostringstream stdout{};
    auto uut = make_heartbeat(stdout);
    REQUIRE(uut.cycles_between_printouts == 10000000U);
}

// ---- Listener attachment and dispatch ----

// A listener that counts the events it receives
struct counting_listener : champsim::modules::listener {
  int begin_phases = 0;
  std::size_t retired = 0;
  explicit counting_listener(champsim::modules::ModuleBuilder) {}
  void begin_phase(bool) override { ++begin_phases; }
  void retire(uint32_t, std::deque<ooo_model_instr>::const_iterator begin, std::deque<ooo_model_instr>::const_iterator end, uint64_t) override
  {
    retired += static_cast<std::size_t>(std::distance(begin, end));
  }
};

// An environment whose only modules are the given listeners
struct listener_env : champsim::modules::environment_module {
  std::vector<champsim::modules::listener*> listeners;
  explicit listener_env(champsim::modules::ModuleBuilder) {}
  std::vector<std::any> view(const std::string& interface_type) const override
  {
    std::vector<std::any> result;
    if (interface_type == "listener")
      for (auto* l : listeners)
        result.push_back(l);
    return result;
  }
  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override { return champsim::modules::ModuleBuilder(); }
};

// Clears the active listeners when a test ends, so no other test dispatches to them
struct active_listener_guard {
  ~active_listener_guard() { champsim::listeners::active().clear(); }
};

std::vector<std::string> active_models()
{
  std::vector<std::string> models;
  for (const auto* l : champsim::listeners::active())
    models.push_back(l->MODEL);
  return models;
}

TEST_CASE("Listener init attaches a default heartbeat with the given frequency") {
    active_listener_guard guard;
    listener_env env{champsim::modules::ModuleBuilder()};
    champsim::listeners::init(env, 1234, false);

    REQUIRE(active_models() == std::vector<std::string>{"HEARTBEAT"});
    auto* hb = dynamic_cast<champsim::heartbeat*>(champsim::listeners::active().front());
    REQUIRE(hb != nullptr);
    REQUIRE(hb->cycles_between_printouts == 1234U);
}

TEST_CASE("Listener init attaches no heartbeat when the heartbeat is hidden") {
    active_listener_guard guard;
    listener_env env{champsim::modules::ModuleBuilder()};
    champsim::heartbeat config_hb{champsim::modules::ModuleBuilder("config_hb", "HEARTBEAT")};
    config_hb.MODEL = "HEARTBEAT";
    env.listeners.push_back(&config_hb);

    champsim::listeners::init(env, 1234, true);
    REQUIRE(champsim::listeners::active().empty());
}

TEST_CASE("Listener init uses a config-declared heartbeat instead of the default") {
    active_listener_guard guard;
    listener_env env{champsim::modules::ModuleBuilder()};
    champsim::heartbeat config_hb{champsim::modules::ModuleBuilder("config_hb", "HEARTBEAT")};
    config_hb.MODEL = "HEARTBEAT";
    env.listeners.push_back(&config_hb);

    champsim::listeners::init(env, 1234, false);
    REQUIRE(champsim::listeners::active() == std::vector<champsim::modules::listener*>{&config_hb});
}

TEST_CASE("Events are dispatched to every active listener") {
    active_listener_guard guard;
    listener_env env{champsim::modules::ModuleBuilder()};
    counting_listener a{champsim::modules::ModuleBuilder()};
    counting_listener b{champsim::modules::ModuleBuilder()};
    env.listeners = {&a, &b};
    champsim::listeners::init(env, 1234, true);

    champsim::listeners::begin_phase(true);
    std::deque<ooo_model_instr> fake_instructions(3, ooo_model_instr(0, input_instr()));
    champsim::listeners::retire(0, std::cbegin(fake_instructions), std::cend(fake_instructions), 7);

    REQUIRE(a.begin_phases == 1);
    REQUIRE(b.begin_phases == 1);
    REQUIRE(a.retired == 3);
    REQUIRE(b.retired == 3);
}

TEST_CASE("An explicit config can declare a listener module") {
    active_listener_guard guard;
    nlohmann::json config = {{"children", nlohmann::json::array({
        {{"name", "config_heartbeat_020"}, {"module", "listener"}, {"model", "HEARTBEAT"}, {"heartbeat_frequency", 5}}
    })}};
    auto builder = champsim::modules::ModuleBuilder{"listener_explicit_env_020", "ENVIRONMENT"};
    builder.add_parameter("config_json", config);
    auto* env = champsim::modules::environment_module::create_instance(builder, static_cast<champsim::modules::environment_module*>(nullptr));

    auto listeners = env->typed_view<champsim::modules::listener>("listener");
    REQUIRE(listeners.size() == 1);
    REQUIRE(listeners.front().get().NAME == "config_heartbeat_020");
    auto* hb = dynamic_cast<champsim::heartbeat*>(&listeners.front().get());
    REQUIRE(hb != nullptr);
    REQUIRE(hb->cycles_between_printouts == 5U);

    // The config's heartbeat replaces the default one
    champsim::listeners::init(*env, 1234, false);
    REQUIRE(champsim::listeners::active() == std::vector<champsim::modules::listener*>{hb});
}

}
