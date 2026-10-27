#include <catch.hpp>

#include "tracereader.h"

namespace
{
struct mock_reader_without_eof {
  std::optional<ooo_model_instr> next_instr() { return ooo_model_instr{0, input_instr{}}; }
  bool steer(bool, uint64_t) { return false; }
  void flush() {}
};
} // namespace

TEST_CASE("A tracereader can be constructed from a type without an eof() member function")
{
  champsim::tracereader uut{mock_reader_without_eof{}};
  REQUIRE_FALSE(uut.eof());
  (void)uut.next_instr();
  REQUIRE_FALSE(uut.eof());
}
