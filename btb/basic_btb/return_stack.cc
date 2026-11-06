#include "return_stack.h"

#include <stdexcept>

return_stack::return_stack(std::size_t max_size_, std::size_t num_call_size_trackers, std::size_t call_size, std::size_t max_call_size_)
    : max_size(max_size_), max_call_size(max_call_size_),
      call_size_trackers(num_call_size_trackers, static_cast<typename champsim::address::difference_type>(call_size))
{
  if (num_call_size_trackers == 0)
    throw std::range_error{"basic_btb: ras_call_size_trackers must be positive"};
}

std::size_t return_stack::tracker_index(champsim::address ip) const { return ip.to<std::size_t>() % std::size(call_size_trackers); }

std::pair<champsim::address, bool> return_stack::prediction()
{
  if (std::empty(stack))
    return {champsim::address{}, true};

  // peek at the top of the RAS and adjust for the size of the call instr
  auto target = stack.back();
  auto size = call_size_trackers[tracker_index(target)];

  return {target + size, true};
}

void return_stack::push(champsim::address ip)
{
  stack.push_back(ip);
  if (std::size(stack) > max_size)
    stack.pop_front();
}

void return_stack::calibrate_call_size(champsim::address branch_target)
{
  if (!std::empty(stack)) {
    // recalibrate call-return offset if our return prediction got us close, but not exact
    auto call_ip = stack.back();
    stack.pop_back();

    static int num_times_returned_backwards = 0;
    if (call_ip > branch_target && num_times_returned_backwards < 10) {
      ++num_times_returned_backwards;
      fmt::print("[BTB] WARNING: target of return is a lower address than the corresponding call. This is usually a problem with your trace.\n");
    }

    auto estimated_call_instr_size = call_ip > branch_target ? champsim::uoffset(branch_target, call_ip) : champsim::uoffset(call_ip, branch_target);
    if (estimated_call_instr_size <= max_call_size) {
      call_size_trackers[tracker_index(call_ip)] = estimated_call_instr_size;
    }
  }
}
