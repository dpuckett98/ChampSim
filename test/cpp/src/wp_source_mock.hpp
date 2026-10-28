#ifndef TEST_WP_SOURCE_MOCK_HPP
#define TEST_WP_SOURCE_MOCK_HPP

#include <deque>
#include <optional>
#include <vector>

#include "instruction.h"
#include "modules.h"

/*
 * Test-only workload source registered as "MOCK_WP_SOURCE" (see test/cpp/src/wp_source_mock.cc).
 *
 * Serves a correct-path (CP) stream, and serves a wrong-path (WP) stream after the core is told that the branch at mispredict_ip was
 * mispredicted. Once the WP stream is exhausted it returns no instruction, halting fetch until the flush.
 */
namespace champsim::test
{
struct mock_wp_source : public champsim::modules::workload_source {
  std::deque<ooo_model_instr> correct_path;
  std::deque<ooo_model_instr> wrong_path;
  uint64_t mispredict_ip = 0;

  bool on_wp = false;
  bool mispredicted = false;
  uint64_t last_ip = 0;
  uint64_t next_instr_id = 1;
  int flushes = 0;

  explicit mock_wp_source(champsim::modules::ModuleBuilder) {}

  std::optional<ooo_model_instr> next_instr() override
  {
    auto& stream = on_wp ? wrong_path : correct_path;
    if (std::empty(stream)) {
      return std::nullopt;
    }

    auto instr = stream.front();
    stream.pop_front();
    if (on_wp) {
      instr.set_wp_event_flags();
    }
    instr.instr_id = next_instr_id++; // the core relies on instr_ids increasing in fetch order to find instrs. younger than a mispredicted branch
    last_ip = instr.ip.to<uint64_t>();
    return instr;
  }

  bool steer(const bool, const uint64_t) override
  {
    if (!on_wp && !mispredicted && last_ip == mispredict_ip) {
      mispredicted = true;
      on_wp = true;
      return true;
    }
    return false;
  }

  void flush() override
  {
    on_wp = false;
    ++flushes;
  }

  [[nodiscard]] bool eof() const override { return std::empty(correct_path); }
};

// IPs are spaced so that no two instrs. share a DIB window
constexpr uint64_t wp_mock_ip(uint64_t index) { return 0x10000 + 0x40 * index; }

inline ooo_model_instr wp_mock_instr(uint64_t ip, std::vector<uint8_t> dst_reg, std::vector<uint8_t> src_reg, std::vector<uint64_t> src_mem = {})
{
  return ooo_model_instr{ip, 4, false, false, champsim::address{}, 0, branch_type::NOT_BRANCH, dst_reg, src_reg, {}, src_mem};
}

inline ooo_model_instr wp_mock_indirect_branch(uint64_t ip, std::vector<uint8_t> src_reg)
{
  return ooo_model_instr{ip, 4, true, true, champsim::address{0x80000}, 0, branch_type::BRANCH_INDIRECT, {}, src_reg, {}, {}};
}
} // namespace champsim::test

#endif
