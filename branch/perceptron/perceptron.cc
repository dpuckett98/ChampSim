/*
 * Copyright (c) 2001 University of Texas at Austin
 *
 * Daniel A. Jimenez
 * Calvin Lin
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software (the "Software"), to deal in
 * the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE UNIVERSITY OF TEXAS AT
 * AUSTIN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file implements the simulated perceptron branch predictor from:
 *
 * Jimenez, D. A. & Lin, C., Dynamic branch prediction with perceptrons,
 * Proceedings of the Seventh International Symposium on High Performance
 * Computer Architecture (HPCA), Monterrey, NL, Mexico 2001
 *
 * By default this is a perceptron predictor with a history
 * length of 24, 163 perceptrons, and  8-bit weights.  This represents
 * a hardware budget of (24+1)*8*163 = 32600 bits, or about 4K bytes,
 * which is comparable to the hardware budget of the Alpha 21264 hybrid
 * branch predictor. All of these sizes can be changed from the config;
 * see perceptron.h.
 */

#include "perceptron.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

champsim::modules::branch_predictor::register_module<perceptron> perceptron_register("perceptron");

perceptron::perceptron(champsim::modules::ModuleBuilder builder)
    : history_length(builder.get_parameter<std::size_t>("history_length", true, DEFAULT_HISTORY)),
      weight_bits(builder.get_parameter<std::size_t>("weight_bits", true, DEFAULT_BITS)),
      num_perceptrons(builder.get_parameter<std::size_t>("num_perceptrons", true, DEFAULT_NUM_PERCEPTRONS)),
      num_update_entries(builder.get_parameter<std::size_t>("update_buffer_size", true, DEFAULT_NUM_UPDATE_ENTRIES)),
      theta(builder.get_parameter<long long>("theta", true, std::lround(1.93 * static_cast<double>(history_length) + 14)))
{
  if (history_length > std::numeric_limits<uint64_t>::digits)
    throw std::range_error{"perceptron: history_length must be at most 64"};
  if (weight_bits < 2 || weight_bits > 16)
    throw std::range_error{"perceptron: weight_bits must be in [2, 16]"};
  if (num_perceptrons == 0)
    throw std::range_error{"perceptron: num_perceptrons must be positive"};

  weight_max = (1LL << (weight_bits - 1)) - 1;
  weight_min = -(1LL << (weight_bits - 1));
  weights.assign(num_perceptrons * (history_length + 1), 0);
}

long long perceptron::predict_with(std::size_t index, uint64_t history) const
{
  const auto* w = &weights[index * (history_length + 1)];
  auto output = w[0]; // bias

  // find the (rest of the) dot product of the history register and the perceptron weights.
  for (std::size_t i = 0; i < history_length; i++) {
    if ((history >> i) & 1)
      output += w[i + 1];
    else
      output -= w[i + 1];
  }

  return output;
}

void perceptron::train(std::size_t index, bool result, uint64_t history)
{
  auto* w = &weights[index * (history_length + 1)];
  auto bump = [this](long long& x, bool up) { x = std::clamp(x + (up ? 1 : -1), weight_min, weight_max); };

  // if the branch was taken, increment the bias weight, else decrement it, with saturating arithmetic
  bump(w[0], result);

  // for each weight, increment it if the corresponding history bit agrees with the outcome, else decrement it
  for (std::size_t i = 0; i < history_length; i++)
    bump(w[i + 1], ((history >> i) & 1) == (result ? 1 : 0));
}

bool perceptron::predict_branch(champsim::address ip, [[maybe_unused]] champsim::address predicted_target, [[maybe_unused]] bool always_taken,
                                [[maybe_unused]] uint8_t branch_type)
{
  // hash the address to get an index into the table of perceptrons
  const auto index = ip.to<uint64_t>() % num_perceptrons;
  const auto output = predict_with(index, spec_global_history);

  bool prediction = (output >= 0);

  // record the various values needed to update the predictor
  perceptron_state_buf.push_back({ip, prediction, output, spec_global_history});
  if (std::size(perceptron_state_buf) > num_update_entries)
    perceptron_state_buf.pop_front();

  // update the speculative global history register
  spec_global_history = ((spec_global_history << 1) | (prediction ? 1 : 0)) & history_mask();
  return prediction;
}

void perceptron::last_branch_result(champsim::address ip, [[maybe_unused]] champsim::address branch_target, bool taken, [[maybe_unused]] uint8_t branch_type)
{
  auto state = std::find_if(std::begin(perceptron_state_buf), std::end(perceptron_state_buf), [ip](auto x) { return x.ip == ip; });
  if (state == std::end(perceptron_state_buf))
    return; // Skip update because state was lost

  auto [_ip, prediction, output, history] = *state;
  perceptron_state_buf.erase(state);

  // update the real global history shift register
  global_history = ((global_history << 1) | (taken ? 1 : 0)) & history_mask();

  // if this branch was mispredicted, restore the speculative history to the
  // last known real history
  if (prediction != taken)
    spec_global_history = global_history;

  // if the output of the perceptron predictor is outside of the range
  // [-theta,theta] *and* the prediction was correct, then we don't need to
  // adjust the weights
  if ((output <= theta && output >= -theta) || (prediction != taken)) {
    const auto index = ip.to<uint64_t>() % num_perceptrons;
    train(index, taken, history);
  }
}
