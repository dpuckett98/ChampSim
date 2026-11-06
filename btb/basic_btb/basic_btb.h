#ifndef BTB_BASIC_BTB_H
#define BTB_BASIC_BTB_H

#include "address.h"
#include "direct_predictor.h"
#include "indirect_predictor.h"
#include "modules.h"
#include "return_stack.h"

class basic_btb : public champsim::modules::btb
{
  return_stack ras{};
  indirect_predictor indirect{};
  direct_predictor direct{};

public:
  using btb::btb;

  /*
   * Every parameter is optional; the defaults reproduce the original hard-coded sizes.
   *   btb_sets (1024, power of 2), btb_ways (8)        -- set-associative target buffer
   *   indirect_size (4096), indirect_history_length (12, at most 64)
   *                                                     -- indirect target table and its conditional history
   *   ras_size (64), ras_call_size_trackers (1024), ras_default_call_size (4),
   *   ras_max_call_size (10)                            -- return address stack and call-size calibration
   */
  explicit basic_btb(champsim::modules::ModuleBuilder builder);

  void initialize_btb() override {}
  std::pair<champsim::address, bool> btb_prediction(champsim::address ip, uint8_t branch_type) override;
  void update_btb(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type) override;
};

#endif
