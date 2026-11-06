#ifndef FOLDED_SHIFT_REGISTER_H
#define FOLDED_SHIFT_REGISTER_H

#include <cassert>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

#include "modules.h"
#include "msl/bits.h"
#include "util/bit_enum.h"

/**
 * @brief A shift register that folds words before it returns its value.
 *
 * This class maintains a history of bits that have been pushed into it.
 * When the user asks for its value, it folds the history in word_len chunks,
 * returning the XOR of the words. The word length is chosen at run time;
 * folded_shift_register<WORD_LEN> below fixes it at compile time.
 */
class dynamic_folded_shift_register
{
  /*
   * This class uses two terms: "value" and "word". A value contains one or more packed words.
   */
  using value_type = unsigned long long;
  static constexpr std::size_t VALUE_DIGITS = std::numeric_limits<value_type>::digits;

  std::size_t word_len;
  std::size_t num_words_per_value; // this many word_len-bit words are kept per value
  std::size_t value_len;

  value_type last_value_mask;    // The last word may not be the full width of the value
  std::vector<value_type> words; // The history is represented as a series of values

  static value_type mask(std::size_t n) { return champsim::msl::bitmask(champsim::data::bits{n}); }

public:
  dynamic_folded_shift_register(champsim::data::bits word_length, champsim::data::bits length)
      : word_len(champsim::to_underlying(word_length)), num_words_per_value(VALUE_DIGITS / word_len), value_len(word_len * num_words_per_value),
        last_value_mask(mask(champsim::to_underlying(length) % value_len)),
        words((champsim::to_underlying(length) / value_len) + ((champsim::to_underlying(length) % value_len != 0) ? 1 : 0))
  {
    assert(word_len >= 1 && word_len <= VALUE_DIGITS);
  }

  std::size_t value() const
  {
    // XOR all of the entries together
    auto joined_words = std::accumulate(std::begin(words), std::end(words), value_type{}, std::bit_xor<>{});

    // Unpack the words from the accumulated values
    std::size_t result = joined_words;
    if (num_words_per_value > 1) {
      for (std::size_t i = 0; i <= num_words_per_value; ++i) {
        joined_words >>= word_len;
        result ^= joined_words;
      }
    }
    return result & mask(word_len);
  }

  /**
   *  Insert this value into the shift register
   **/
  void push_back(bool ins)
  {
    /**
     * Find the MSB of the value. This MSB will be passed along to the next word in the array.
     */
    const auto msb_loc = value_len - 1;
    const auto value_mask = mask(value_len);

    // Shift each word left by one, carrying each word's old MSB into the next word's LSB. The new bit is the first word's carry-in.
    value_type carry = ins ? value_type{0x1} : value_type{0x0};
    for (auto& word : words) {
      const value_type msb = (word & (value_type{1} << msb_loc)) >> msb_loc;
      word = ((word << 1) | carry) & value_mask;
      carry = msb;
    }

    // Don't apply the mask if the last value is full-width
    if (last_value_mask != value_type{}) {
      words.back() &= last_value_mask;
    }
  }
};

template <champsim::data::bits WORD_LEN>
class folded_shift_register : public dynamic_folded_shift_register
{
  static_assert(champsim::data::bits{std::numeric_limits<unsigned long long>::digits} >= WORD_LEN);

public:
  folded_shift_register() : folded_shift_register(champsim::data::bits{}) {}
  explicit folded_shift_register(champsim::data::bits length) : dynamic_folded_shift_register(WORD_LEN, length) {}
};

#endif
