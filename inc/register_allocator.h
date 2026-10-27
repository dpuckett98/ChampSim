#include <array>
#include <cstdint>
#include <list>
#include <optional>
#include <queue>

#ifndef REG_ALLOC_H
#define REG_ALLOC_H

#include "instruction.h"

struct physical_register {
  uint16_t arch_reg_index;
  uint64_t producing_instruction_id;
  bool valid; // has the producing instruction committed yet?
  bool busy;  // is this register in use anywhere in the pipeline?
};

class RegisterAllocator
{
private:
  // correct_path_RAT holds the most recent correct-path mapping and is used to rename correct-path instrs.
  // wrong_path_RAT holds the most recent mapping including wrong-path renames and is used to rename wrong-path instrs.;
  //   recovering from a branch misprediction only requires copying correct_path_RAT into wrong_path_RAT
  // backend_RAT holds the committed mapping; it is updated on retire and used to find physical registers to free
  std::array<PHYSICAL_REGISTER_ID, std::numeric_limits<uint8_t>::max() + 1> correct_path_RAT, wrong_path_RAT, backend_RAT;
  std::queue<PHYSICAL_REGISTER_ID> free_registers;
  std::vector<physical_register> physical_register_file;

public:
  RegisterAllocator(size_t num_physical_registers);
  PHYSICAL_REGISTER_ID rename_dest_register(int16_t reg, champsim::program_ordered<ooo_model_instr>::id_type producer_id, bool wrong_path = false);
  PHYSICAL_REGISTER_ID rename_src_register(int16_t reg, bool wrong_path = false);
  void complete_dest_register(PHYSICAL_REGISTER_ID physreg);
  void retire_dest_register(PHYSICAL_REGISTER_ID physreg);
  void free_register(PHYSICAL_REGISTER_ID physreg);
  bool isValid(PHYSICAL_REGISTER_ID physreg) const;
  bool isAllocated(PHYSICAL_REGISTER_ID archreg, bool wrong_path = false) const;
  unsigned long count_free_registers() const;
  int count_reg_dependencies(const ooo_model_instr& instr) const;
  void reset_wrong_path_RAT();
  void print_deadlock();
};
#endif
