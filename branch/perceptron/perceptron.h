#ifndef BRANCH_PERCEPTRON_H
#define BRANCH_PERCEPTRON_H

#include <cstdint>
#include <deque>
#include <vector>

#include "modules.h"

/*
 * Optional config parameters (defaults are the original hard-coded sizes):
 *   history_length (24)      -- global history bits per perceptron (at most 64)
 *   weight_bits (8)          -- width of each signed weight (2-16)
 *   num_perceptrons (163)    -- number of perceptrons; indexed by ip % num_perceptrons
 *   update_buffer_size (100) -- in-flight predictions remembered for training
 *   theta                    -- training threshold; defaults to round(1.93 * history_length + 14)
 */
struct perceptron : champsim::modules::branch_predictor {
  static constexpr std::size_t DEFAULT_HISTORY = 24;
  static constexpr std::size_t DEFAULT_BITS = 8;
  static constexpr std::size_t DEFAULT_NUM_PERCEPTRONS = 163;
  static constexpr std::size_t DEFAULT_NUM_UPDATE_ENTRIES = 100;

  std::size_t history_length;
  std::size_t weight_bits;
  std::size_t num_perceptrons;
  std::size_t num_update_entries;
  long long theta;
  long long weight_max = 0;
  long long weight_min = 0;

  /* 'perceptron_state' - stores the branch prediction and keeps information
   * such as output and history needed for updating the perceptron predictor
   */
  struct perceptron_state {
    champsim::address ip{};
    bool prediction = false; // prediction: 1 for taken, 0 for not taken
    long long int output = 0; // perceptron output
    uint64_t history = 0;     // value of the history register yielding this prediction
  };

  // num_perceptrons rows of (history_length + 1) weights; weight 0 of each row is the bias
  std::vector<long long> weights;
  std::deque<perceptron_state> perceptron_state_buf; // state for updating perceptron predictor
  uint64_t spec_global_history = 0;                  // speculative global history - updated by predictor
  uint64_t global_history = 0;                       // real global history - updated when the predictor is updated

  void initialize_branch_predictor() override {}
  bool predict_branch(champsim::address ip, champsim::address predicted_target, bool always_taken, uint8_t branch_type) override;
  void last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type) override;

  explicit perceptron(champsim::modules::ModuleBuilder builder);

private:
  [[nodiscard]] uint64_t history_mask() const { return history_length >= 64 ? ~uint64_t{0} : (uint64_t{1} << history_length) - 1; }
  [[nodiscard]] long long predict_with(std::size_t index, uint64_t history) const;
  void train(std::size_t index, bool result, uint64_t history);
};

#endif
