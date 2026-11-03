#include "event_listeners.h"

#include <algorithm>

namespace
{
const std::string heartbeat_model = "HEARTBEAT";
}

void champsim::listeners::init(champsim::modules::environment_module& env, uint64_t heartbeat_frequency, bool hide_heartbeat)
{
  auto& listeners = active();
  listeners.clear();

  const auto is_active = [&listeners](const std::string& model) {
    return std::any_of(std::begin(listeners), std::end(listeners), [&model](const auto* listener) { return listener->MODEL == model; });
  };
  const auto attach = [&](champsim::modules::listener* listener) {
    if (!(hide_heartbeat && listener->MODEL == heartbeat_model))
      listeners.push_back(listener);
  };

  // Listeners declared in the config
  for (champsim::modules::listener& listener : env.typed_view<champsim::modules::listener>("listener"))
    attach(&listener);

  // The heartbeat is always enabled unless hidden
  if (!hide_heartbeat && !is_active(heartbeat_model)) {
    auto builder = champsim::modules::ModuleBuilder("heartbeat", heartbeat_model).add_parameter("heartbeat_frequency", heartbeat_frequency);
    attach(champsim::modules::listener::create_instance(builder, &env));
  }
}

static champsim::modules::listener::register_interface listener_iface_reg("listener");
