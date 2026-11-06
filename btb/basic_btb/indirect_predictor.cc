#include "indirect_predictor.h"

#include <limits>
#include <stdexcept>

indirect_predictor::indirect_predictor(std::size_t size, std::size_t history_length_) : predictor(size), history_length(history_length_)
{
  if (size == 0)
    throw std::range_error{"basic_btb: indirect_size must be positive"};
  if (history_length > std::numeric_limits<uint64_t>::digits)
    throw std::range_error{"basic_btb: indirect_history_length must be at most 64"};
}

std::size_t indirect_predictor::index(champsim::address ip) const
{
  using namespace champsim::data::data_literals;
  auto hash = ip.slice_upper<2_b>().to<unsigned long long>() ^ conditional_history;
  return hash % std::size(predictor);
}

std::pair<champsim::address, bool> indirect_predictor::prediction(champsim::address ip) { return {predictor[index(ip)], true}; }

void indirect_predictor::update_target(champsim::address ip, champsim::address branch_target) { predictor[index(ip)] = branch_target; }

void indirect_predictor::update_direction(bool taken)
{
  conditional_history = (conditional_history << 1) | (taken ? 1 : 0);
  if (history_length < std::numeric_limits<uint64_t>::digits)
    conditional_history &= (uint64_t{1} << history_length) - 1;
}
