#include "gshare.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "msl/bits.h"

champsim::modules::branch_predictor::register_module<gshare> gshare_register("gshare");

namespace
{
uint64_t low_bits(std::size_t n) { return n >= 64 ? ~uint64_t{0} : (uint64_t{1} << n) - 1; }
} // namespace

gshare::gshare(champsim::modules::ModuleBuilder builder)
    : history_length(builder.get_parameter<std::size_t>("history_length", true, DEFAULT_HISTORY_LENGTH)),
      table_size(builder.get_parameter<std::size_t>("table_size", true, DEFAULT_TABLE_SIZE)),
      counter_bits(builder.get_parameter<std::size_t>("counter_bits", true, DEFAULT_COUNTER_BITS)),
      counter_max(counter_bits >= 1 && counter_bits <= 16 ? (1 << counter_bits) - 1 : 0), gs_history_table(table_size, 0)
{
  if (history_length > std::numeric_limits<uint64_t>::digits)
    throw std::range_error{"gshare: history_length must be at most 64"};
  if (table_size == 0)
    throw std::range_error{"gshare: table_size must be positive"};
  if (counter_bits < 1 || counter_bits > 16)
    throw std::range_error{"gshare: counter_bits must be in [1, 16]"};
}

std::size_t gshare::gs_table_hash(champsim::address ip, uint64_t bh_vector) const
{
  // XOR the history with three consecutive lg2(table_size)-bit slices of the IP, starting at bits 0, L and 2L (L = history_length)
  const auto index_bits = static_cast<std::size_t>(champsim::msl::lg2(table_size));
  const auto ip_bits = ip.to<uint64_t>();
  auto slice = [&](std::size_t lower) { return lower >= 64 ? uint64_t{0} : (ip_bits >> lower) & low_bits(index_bits); };

  uint64_t hash = bh_vector;
  hash ^= slice(0);
  hash ^= slice(history_length);
  hash ^= slice(2 * history_length);

  return hash % table_size;
}

bool gshare::predict_branch(champsim::address ip, [[maybe_unused]] champsim::address predicted_target, [[maybe_unused]] bool always_taken,
                            [[maybe_unused]] uint8_t branch_type)
{
  return gs_history_table[gs_table_hash(ip, branch_history_vector)] >= (counter_max / 2);
}

void gshare::last_branch_result(champsim::address ip, [[maybe_unused]] champsim::address branch_target, bool taken, [[maybe_unused]] uint8_t branch_type)
{
  auto& counter = gs_history_table[gs_table_hash(ip, branch_history_vector)];
  counter = std::clamp(counter + (taken ? 1 : -1), 0, counter_max);

  // update branch history vector
  branch_history_vector = ((branch_history_vector << 1) | (taken ? 1 : 0)) & low_bits(history_length);
}
