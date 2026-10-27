#include <catch.hpp>

#include "repeatable.h"

namespace
{
struct mock_repeatable {
  bool ever_repeats;

  inline static int constructor_calls = 0;

  bool eof() const { return ever_repeats; }

  std::optional<ooo_model_instr> next_instr() { return ooo_model_instr{0, input_instr{}}; }
  bool steer(bool, uint64_t) { return false; }
  void flush() {}

  explicit mock_repeatable(bool r) : ever_repeats(r) { constructor_calls++; }
};
} // namespace

TEST_CASE("A repeatable repeats")
{
  champsim::repeatable<mock_repeatable, bool> uut{true};

  auto old_calls = mock_repeatable::constructor_calls;
  (void)uut.next_instr();
  REQUIRE(mock_repeatable::constructor_calls > old_calls);
}

namespace
{
struct steerable_repeatable {
  bool steer_called = false;
  bool steer_taken = false;
  uint64_t steer_target = 0;
  bool flush_called = false;

  bool eof() const { return false; }

  std::optional<ooo_model_instr> next_instr() { return std::nullopt; }
  bool steer(bool taken, uint64_t target)
  {
    steer_called = true;
    steer_taken = taken;
    steer_target = target;
    return true;
  }
  void flush() { flush_called = true; }
};
} // namespace

TEST_CASE("A repeatable forwards steer() and flush() to the underlying reader")
{
  champsim::repeatable<steerable_repeatable> uut{};

  REQUIRE(uut.steer(true, 0x1234));
  REQUIRE(uut.intern_.steer_called);
  REQUIRE(uut.intern_.steer_taken);
  REQUIRE(uut.intern_.steer_target == 0x1234);

  REQUIRE_FALSE(uut.intern_.flush_called);
  uut.flush();
  REQUIRE(uut.intern_.flush_called);
}
