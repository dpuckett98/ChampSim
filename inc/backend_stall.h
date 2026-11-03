#ifndef BACKEND_STALL_H
#define BACKEND_STALL_H

namespace champsim
{
/**
 * Which back-end structures can't accept an instruction waiting to dispatch. More than one can be full at once; an instruction fits in the back end only when
 * none is.
 */
struct backend_stall {
  bool rob_full = false; // the ROB has no free entry
  bool lq_full = false;  // the LQ has fewer free entries than the instruction has source memory operands
  bool sq_full = false;  // the SQ has fewer free entries than the instruction has destination memory operands

  [[nodiscard]] constexpr bool any() const { return rob_full || lq_full || sq_full; }
};
} // namespace champsim

#endif
