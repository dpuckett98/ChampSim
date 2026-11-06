/*

This code implements a hashed perceptron branch predictor using geometric
history lengths and dynamic threshold setting.  It was written by Daniel
A. Jiménez in March 2019.


The original perceptron branch predictor is from Jiménez and Lin, "Dynamic
Branch Prediction with Perceptrons," HPCA 2001.

The idea of using multiple independently indexed tables of perceptron weights
is from Jiménez, "Fast Path-Based Neural Branch Prediction," MICRO 2003 and
later expanded in "Piecewise Linear Branch Prediction" from ISCA 2005.

The idea of using hashes of branch history to reduce the number of independent
tables is documented in three contemporaneous papers:

1. Seznec, "Revisiting the Perceptron Predictor," IRISA technical report, 2004.

2. Tarjan and Skadron, "Revisiting the Perceptron Predictor Again," UVA
technical report, 2004, expanded and published in ACM TACO 2005 as "Merging
path and gshare indexing in perceptron branch prediction"; introduces the term
"hashed perceptron."

3. Loh and Jiménez, "Reducing the Power and Complexity of Path-Based Neural
Branch Prediction," WCED 2005.

The ideas of using "geometric history lengths" i.e. hashing into tables with
histories of exponentially increasing length, as well as dynamically adjusting
the theta parameter, are from Seznec, "The O-GEHL Branch Predictor," from CBP
2004, expanded later as "Analysis of the O-GEometric History Length Branch
Predictor" in ISCA 2005.

This code uses these ideas, but prefers simplicity over absolute accuracy (I
wrote it in about an hour and later spent more time on this comment block than
I did on the code). These papers and subsequent papers by Jiménez and other
authors significantly improve the accuracy of perceptron-based predictors but
involve tricks and analysis beyond the needs of a tool like ChampSim that
targets cache optimizations. If you want accuracy at any cost, see the winners
of the latest branch prediction contest, CBP 2016 as of this writing, but
prepare to have your face melted off by the complexity of the code you find
there. If you are a student being asked to code a good branch predictor for
your computer architecture class, don't copy this code; there are much better
sources for you to plagiarize.

*/

#include "hashed_perceptron.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <numeric>
#include <stdexcept>

#include "msl/bits.h"

champsim::modules::branch_predictor::register_module<hashed_perceptron> hp_register("hashed_perceptron");

std::vector<std::size_t> hashed_perceptron::geometric_history_lengths(std::size_t num_tables, std::size_t min_history, std::size_t max_history)
{
  std::vector<std::size_t> lengths(num_tables, 0);
  for (std::size_t i = 1; i < num_tables; ++i) {
    if (num_tables == 2) {
      lengths[i] = max_history;
    } else {
      auto ratio = static_cast<double>(max_history) / static_cast<double>(min_history);
      auto exponent = static_cast<double>(i - 1) / static_cast<double>(num_tables - 2);
      lengths[i] = static_cast<std::size_t>(std::lround(static_cast<double>(min_history) * std::pow(ratio, exponent)));
    }
  }
  return lengths;
}

namespace
{
// history_lengths arrives as raw JSON from a config file, or as a std::vector<std::size_t> from code
std::vector<std::size_t> read_history_lengths(const champsim::modules::ModuleBuilder& builder)
{
  const auto& params = builder.get_parameters();
  if (auto it = params.find("history_lengths"); it != params.end()) {
    if (const auto* vec = std::any_cast<std::vector<std::size_t>>(&it->second))
      return *vec;
    if (const auto* js = std::any_cast<nlohmann::json>(&it->second); js != nullptr && js->is_array()) {
      std::vector<std::size_t> lengths;
      for (const auto& e : *js) {
        if (!e.is_number_integer() || e.get<long long>() < 0)
          throw std::invalid_argument{"hashed_perceptron: history_lengths must be non-negative integers"};
        lengths.push_back(e.get<std::size_t>());
      }
      return lengths;
    }
    throw std::invalid_argument{"hashed_perceptron: history_lengths must be an array of integers"};
  }

  return hashed_perceptron::geometric_history_lengths(
      builder.get_parameter<std::size_t>("num_tables", true, hashed_perceptron::DEFAULT_NUM_TABLES),
      builder.get_parameter<std::size_t>("min_history", true, hashed_perceptron::DEFAULT_MIN_HISTORY),
      builder.get_parameter<std::size_t>("max_history", true, hashed_perceptron::DEFAULT_MAX_HISTORY));
}
} // namespace

hashed_perceptron::hashed_perceptron(champsim::modules::ModuleBuilder builder)
    : table_size(builder.get_parameter<std::size_t>("table_size", true, DEFAULT_TABLE_SIZE)),
      table_index_bits(static_cast<std::size_t>(champsim::msl::lg2(table_size))),
      threshold(builder.get_parameter<int>("threshold", true, DEFAULT_THRESHOLD)),
      theta_speed(builder.get_parameter<int>("theta_speed", true, DEFAULT_THETA_SPEED)), history_lengths(read_history_lengths(builder)),
      theta(builder.get_parameter<int>("initial_theta", true, DEFAULT_INITIAL_THETA))
{
  auto weight_bits = builder.get_parameter<std::size_t>("weight_bits", true, DEFAULT_WEIGHT_BITS);
  if (table_size == 0 || !champsim::msl::is_power_of_2(table_size))
    throw std::range_error{"hashed_perceptron: table_size must be a power of 2"};
  if (weight_bits < 2 || weight_bits > 16)
    throw std::range_error{"hashed_perceptron: weight_bits must be in [2, 16]"};
  if (std::empty(history_lengths))
    throw std::range_error{"hashed_perceptron: there must be at least one table"};
  if (theta_speed < 1)
    throw std::range_error{"hashed_perceptron: theta_speed must be positive"};

  weight_max = (1 << (weight_bits - 1)) - 1;
  weight_min = -(1 << (weight_bits - 1));

  tables.assign(std::size(history_lengths) * table_size, 0);
  for (auto len : history_lengths)
    ghist_words.emplace_back(champsim::data::bits{table_index_bits}, champsim::data::bits{len});
  last_result.indices.assign(std::size(history_lengths), 0);

  if (champsim::modules::ModuleBuilder::is_dump_enabled())
    fmt::print("  [{}] history_lengths = {} ({})\n", builder.get_name(), history_lengths, builder.has_parameter("history_lengths") ? "set" : "default");
}

bool hashed_perceptron::predict_branch(champsim::address pc, [[maybe_unused]] champsim::address predicted_target, [[maybe_unused]] bool always_taken,
                                       [[maybe_unused]] uint8_t branch_type)
{
  // seed in the PC to spread accesses around (like gshare) XOR in the last word
  const auto pc_slice = pc.to<uint64_t>() & champsim::msl::bitmask(champsim::data::bits{table_index_bits});

  // add the selected weights to the perceptron sum
  int yout = 0;
  for (std::size_t i = 0; i < std::size(ghist_words); ++i) {
    const auto index = ghist_words[i].value() ^ pc_slice;
    last_result.indices[i] = index;
    yout += tables.at(i * table_size + index);
  }
  last_result.yout = yout;
  return yout >= threshold;
}

void hashed_perceptron::last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type)
{
  for (auto& hist : ghist_words) {
    hist.push_back(taken);
  }

  // perceptron learning rule: train if misprediction or weak correct prediction
  bool prediction_correct = (taken == (last_result.yout >= threshold));
  bool prediction_weak = (std::abs(last_result.yout) < theta);
  if (!prediction_correct || prediction_weak) {
    for (std::size_t i = 0; i < std::size(ghist_words); i++) {
      auto& weight = tables[i * table_size + last_result.indices[i]];
      weight = std::clamp(weight + (taken ? 1 : -1), weight_min, weight_max); // update weights
    }
    adjust_threshold(prediction_correct);
  }
}

// dynamic threshold setting from Seznec's O-GEHL paper
void hashed_perceptron::adjust_threshold(bool correct)
{
  if (!correct) {
    // increase theta after enough mispredictions
    tc++;
    if (tc >= theta_speed) {
      theta++;
      tc = 0;
    }
  } else {
    // decrease theta after enough weak but correct predictions
    tc--;
    if (tc <= -theta_speed) {
      theta--;
      tc = 0;
    }
  }
}
