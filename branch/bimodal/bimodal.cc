#include "bimodal.h"

#include <algorithm>
#include <stdexcept>

champsim::modules::branch_predictor::register_module<bimodal> bimodal_register("bimodal");

bimodal::bimodal(champsim::modules::ModuleBuilder builder)
    : table_size(builder.get_parameter<std::size_t>("table_size", true, DEFAULT_TABLE_SIZE)),
      hash_modulus(builder.get_parameter<std::size_t>("hash_modulus", true, table_size)),
      counter_bits(builder.get_parameter<std::size_t>("counter_bits", true, DEFAULT_COUNTER_BITS)),
      counter_max(counter_bits >= 1 && counter_bits <= 16 ? (1 << counter_bits) - 1 : 0), bimodal_table(table_size, 0)
{
  if (table_size == 0)
    throw std::range_error{"bimodal: table_size must be positive"};
  if (hash_modulus == 0 || hash_modulus > table_size)
    throw std::range_error{"bimodal: hash_modulus must be in [1, table_size]"};
  if (counter_bits < 1 || counter_bits > 16)
    throw std::range_error{"bimodal: counter_bits must be in [1, 16]"};
}

bool bimodal::predict_branch(champsim::address ip, [[maybe_unused]] champsim::address predicted_target, [[maybe_unused]] bool always_taken,
                             [[maybe_unused]] uint8_t branch_type)
{
  return bimodal_table[hash(ip)] > (counter_max / 2);
}

void bimodal::last_branch_result(champsim::address ip, [[maybe_unused]] champsim::address branch_target, bool taken, [[maybe_unused]] uint8_t branch_type)
{
  auto& counter = bimodal_table[hash(ip)];
  counter = std::clamp(counter + (taken ? 1 : -1), 0, counter_max);
}
