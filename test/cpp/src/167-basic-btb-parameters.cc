#include <catch.hpp>

#include "../../../btb/basic_btb/basic_btb.h"
#include "instruction.h"

namespace
{
basic_btb make_btb(std::size_t sets, std::size_t ways, std::size_t ras_size)
{
  return basic_btb{champsim::modules::ModuleBuilder("test_btb", "basic_btb")
                       .add_parameter("btb_sets", sets)
                       .add_parameter("btb_ways", ways)
                       .add_parameter("ras_size", ras_size)};
}
} // namespace

TEST_CASE("The basic_btb takes its target buffer geometry from the config")
{
  const champsim::address target{0x66b5f0};
  std::array<champsim::address, 3> ips{{champsim::address{0x110000}, champsim::address{0x111000}, champsim::address{0x112000}}};

  SECTION("A 1-set, 2-way BTB evicts the oldest of three branches")
  {
    auto uut = make_btb(1, 2, 64);
    for (auto ip : ips)
      uut.update_btb(ip, target, true, BRANCH_DIRECT_JUMP);

    CHECK(uut.btb_prediction(ips[0], 0).first == champsim::address{});
    CHECK(uut.btb_prediction(ips[1], 0).first == target);
    CHECK(uut.btb_prediction(ips[2], 0).first == target);
  }

  SECTION("A 1-set, 4-way BTB holds all three branches")
  {
    auto uut = make_btb(1, 4, 64);
    for (auto ip : ips)
      uut.update_btb(ip, target, true, BRANCH_DIRECT_JUMP);

    for (auto ip : ips)
      CHECK(uut.btb_prediction(ip, 0).first == target);
  }
}

TEST_CASE("The basic_btb takes its return address stack depth from the config")
{
  const champsim::address ret_ip{0x200000};
  const champsim::address outer_call{0x100000};
  const champsim::address inner_call{0x101000};

  auto uut = make_btb(1024, 8, 1);
  uut.update_btb(ret_ip, outer_call + 4, true, BRANCH_RETURN); // teach the BTB that ret_ip is a return
  uut.update_btb(outer_call, champsim::address{0x300000}, true, BRANCH_DIRECT_CALL);
  uut.update_btb(inner_call, champsim::address{0x400000}, true, BRANCH_DIRECT_CALL);

  // the inner call is on top of a 1-entry stack
  CHECK(uut.btb_prediction(ret_ip, BRANCH_RETURN).first == inner_call + 4);
  uut.update_btb(ret_ip, inner_call + 4, true, BRANCH_RETURN);

  // the outer call was pushed off the 1-entry stack, so there is no prediction left
  CHECK(uut.btb_prediction(ret_ip, BRANCH_RETURN).first == champsim::address{});
}
