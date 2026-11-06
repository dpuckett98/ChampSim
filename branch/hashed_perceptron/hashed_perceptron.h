#ifndef BRANCH_HASHED_PERCEPTRON_H
#define BRANCH_HASHED_PERCEPTRON_H

#include <cstdint>
#include <vector>

#include "folded_shift_register.h"
#include "modules.h"

/*
 * Optional config parameters (defaults are the original hard-coded sizes):
 *   table_size (4096)     -- weights per table; a power of 2 (the index width is lg2 of it)
 *   weight_bits (8)       -- width of each signed weight (2-16)
 *   history_lengths       -- global history length for each table, e.g. [0, 3, 4, 6, ...]; the number of entries is the number of tables
 *   num_tables (16), min_history (3), max_history (232)
 *                         -- used when history_lengths is absent: table 0 has no history (biases), tables 1..n-1 have
 *                            geometric lengths from min_history to max_history. The defaults reproduce the original list.
 *   threshold (1)         -- predict taken when the perceptron sum is >= threshold
 *   initial_theta (10), theta_speed (18)
 *                         -- starting training threshold and the speed of O-GEHL dynamic threshold setting
 */
class hashed_perceptron : public champsim::modules::branch_predictor
{
public:
  static constexpr std::size_t DEFAULT_TABLE_SIZE = 1 << 12;
  static constexpr std::size_t DEFAULT_WEIGHT_BITS = 8;
  static constexpr std::size_t DEFAULT_NUM_TABLES = 16;
  static constexpr std::size_t DEFAULT_MIN_HISTORY = 3;
  static constexpr std::size_t DEFAULT_MAX_HISTORY = 232;
  static constexpr int DEFAULT_THRESHOLD = 1;
  static constexpr int DEFAULT_INITIAL_THETA = 10;
  static constexpr int DEFAULT_THETA_SPEED = 18;

  // table 0 has no history; tables 1..num_tables-1 have geometric lengths from min_history to max_history
  static std::vector<std::size_t> geometric_history_lengths(std::size_t num_tables, std::size_t min_history, std::size_t max_history);

private:
  std::size_t table_size;
  std::size_t table_index_bits;
  int weight_max;
  int weight_min;
  int threshold;
  int theta_speed;

  std::vector<std::size_t> history_lengths;

  // tables of weights, stored contiguously: table i occupies [i * table_size, (i + 1) * table_size)
  std::vector<int> tables;

  // words that store the global history
  std::vector<dynamic_folded_shift_register> ghist_words;

  int theta;
  int tc = 0; // counter for threshold setting algorithm

  struct perceptron_result {
    std::vector<uint64_t> indices; // remember the indices into the tables from prediction to update
    int yout = 0;                  // perceptron sum
  };

  perceptron_result last_result{};

public:
  void initialize_branch_predictor() override {}
  bool predict_branch(champsim::address pc, champsim::address predicted_target, bool always_taken, uint8_t branch_type) override;
  void last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type) override;
  void adjust_threshold(bool correct);
  explicit hashed_perceptron(champsim::modules::ModuleBuilder builder);

  [[nodiscard]] const std::vector<std::size_t>& get_history_lengths() const { return history_lengths; }
};

#endif
