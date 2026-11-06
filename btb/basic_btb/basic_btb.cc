
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 */

#include "basic_btb.h"

#include "instruction.h"

champsim::modules::btb::register_module<basic_btb> basic_btb_register("basic_btb");

basic_btb::basic_btb(champsim::modules::ModuleBuilder builder)
    : ras(builder.get_parameter<std::size_t>("ras_size", true, return_stack::default_max_size),
          builder.get_parameter<std::size_t>("ras_call_size_trackers", true, return_stack::default_num_call_size_trackers),
          builder.get_parameter<std::size_t>("ras_default_call_size", true, return_stack::default_call_size),
          builder.get_parameter<std::size_t>("ras_max_call_size", true, return_stack::default_max_call_size)),
      indirect(builder.get_parameter<std::size_t>("indirect_size", true, indirect_predictor::default_size),
               builder.get_parameter<std::size_t>("indirect_history_length", true, indirect_predictor::default_history_length)),
      direct(builder.get_parameter<std::size_t>("btb_sets", true, direct_predictor::default_sets),
             builder.get_parameter<std::size_t>("btb_ways", true, direct_predictor::default_ways))
{
}
std::pair<champsim::address, bool> basic_btb::btb_prediction(champsim::address ip, [[maybe_unused]] uint8_t branch_type)
{
  // use BTB for all other branches + direct calls
  auto btb_entry = direct.check_hit(ip);

  // no prediction for this IP
  if (!btb_entry.has_value())
    return {champsim::address{}, false};

  if (btb_entry->type == direct_predictor::branch_info::RETURN)
    return ras.prediction();

  if (btb_entry->type == direct_predictor::branch_info::INDIRECT)
    return indirect.prediction(ip);

  return {btb_entry->target, btb_entry->type != direct_predictor::branch_info::CONDITIONAL};
}

void basic_btb::update_btb(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type)
{
  // add something to the RAS
  if (branch_type == BRANCH_DIRECT_CALL || branch_type == BRANCH_INDIRECT_CALL)
    ras.push(ip);

  // updates for indirect branches
  if ((branch_type == BRANCH_INDIRECT) || (branch_type == BRANCH_INDIRECT_CALL))
    indirect.update_target(ip, branch_target);

  if (branch_type == BRANCH_CONDITIONAL)
    indirect.update_direction(taken);

  if (branch_type == BRANCH_RETURN)
    ras.calibrate_call_size(branch_target);

  direct.update(ip, branch_target, branch_type);
}
