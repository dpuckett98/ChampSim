#ifndef BRANCH_GSHARE_H
#define BRANCH_GSHARE_H

#include <cstdint>
#include <vector>

#include "modules.h"

/*
 * Optional config parameters (defaults are the original hard-coded sizes):
 *   history_length (14)  -- bits of global history (at most 64)
 *   table_size (16384)   -- number of saturating counters
 *   counter_bits (2)     -- width of each counter (1-16)
 */
struct gshare : champsim::modules::branch_predictor {
  static constexpr std::size_t DEFAULT_HISTORY_LENGTH = 14;
  static constexpr std::size_t DEFAULT_COUNTER_BITS = 2;
  static constexpr std::size_t DEFAULT_TABLE_SIZE = 16384;

  std::size_t history_length;
  std::size_t table_size;
  std::size_t counter_bits;
  int counter_max;

  uint64_t branch_history_vector = 0;
  std::vector<int> gs_history_table;

  [[nodiscard]] std::size_t gs_table_hash(champsim::address ip, uint64_t bh_vector) const;
  void initialize_branch_predictor() override {}
  bool predict_branch(champsim::address ip, champsim::address predicted_target, bool always_taken, uint8_t branch_type) override;
  void last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type) override;

  explicit gshare(champsim::modules::ModuleBuilder builder);
};

#endif
