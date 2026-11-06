#ifndef BTB_BASIC_BTB_RETURN_STACK_H
#define BTB_BASIC_BTB_RETURN_STACK_H

#include <cstdint>
#include <deque>
#include <vector>

#include "address.h"
#include "champsim.h"

struct return_stack {
  static constexpr std::size_t default_max_size = 64;
  static constexpr std::size_t default_num_call_size_trackers = 1024;
  static constexpr std::size_t default_call_size = 4;
  static constexpr std::size_t default_max_call_size = 10;

  std::size_t max_size;
  std::size_t max_call_size;

  std::deque<champsim::address> stack;

  /*
   * The following structure identifies the size of call instructions so we can
   * find the target for a call's return, since calls may have different sizes.
   */
  std::vector<typename champsim::address::difference_type> call_size_trackers;

  explicit return_stack(std::size_t max_size = default_max_size, std::size_t num_call_size_trackers = default_num_call_size_trackers,
                        std::size_t call_size = default_call_size, std::size_t max_call_size = default_max_call_size);

  std::pair<champsim::address, bool> prediction();
  void push(champsim::address ip);
  void calibrate_call_size(champsim::address branch_target);

private:
  [[nodiscard]] std::size_t tracker_index(champsim::address ip) const;
};

#endif
