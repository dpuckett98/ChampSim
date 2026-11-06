#ifndef BRANCH_BIMODAL_H
#define BRANCH_BIMODAL_H

#include <cstdint>
#include <vector>

#include "modules.h"

/*
 * Optional config parameters (defaults are the original hard-coded sizes):
 *   table_size (16384)   -- number of 2-bit counters
 *   hash_modulus         -- the IP is hashed as ip % hash_modulus; defaults to table_size (plain low-order IP bits when it is a
 *                           power of 2). Original ChampSim used the prime 16381.
 *   counter_bits (2)     -- width of each saturating counter (1-16)
 */
class bimodal : public champsim::modules::branch_predictor
{
public:
  static constexpr std::size_t DEFAULT_TABLE_SIZE = 16384;
  static constexpr std::size_t DEFAULT_COUNTER_BITS = 2;

private:
  std::size_t table_size;
  std::size_t hash_modulus;
  std::size_t counter_bits;
  int counter_max;

  std::vector<int> bimodal_table;

  [[nodiscard]] std::size_t hash(champsim::address ip) const { return ip.to<unsigned long>() % hash_modulus; }

public:
  void initialize_branch_predictor() override {}
  bool predict_branch(champsim::address ip, champsim::address predicted_target, bool always_taken, uint8_t branch_type) override;
  void last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type) override;

  explicit bimodal(champsim::modules::ModuleBuilder builder);
};

#endif
