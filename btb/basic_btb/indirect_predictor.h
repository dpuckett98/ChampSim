#ifndef BTB_BASIC_BTB_INDIRECT_PREDICTOR_H
#define BTB_BASIC_BTB_INDIRECT_PREDICTOR_H

#include <cstdint>
#include <utility>
#include <vector>

#include "address.h"
#include "champsim.h"
#include "msl/bits.h"

struct indirect_predictor {
  static constexpr std::size_t default_size = 4096;
  static constexpr std::size_t default_history_length = champsim::msl::lg2(default_size);

  std::vector<champsim::address> predictor;
  std::size_t history_length;
  uint64_t conditional_history = 0;

  explicit indirect_predictor(std::size_t size = default_size, std::size_t history_length = default_history_length);

  std::pair<champsim::address, bool> prediction(champsim::address ip);
  void update_target(champsim::address ip, champsim::address branch_target);
  void update_direction(bool taken);

private:
  [[nodiscard]] std::size_t index(champsim::address ip) const;
};

#endif
