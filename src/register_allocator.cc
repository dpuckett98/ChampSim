#include "register_allocator.h"

#include <cassert>

RegisterAllocator::RegisterAllocator(size_t num_physical_registers)
{
  assert(num_physical_registers <= std::numeric_limits<PHYSICAL_REGISTER_ID>::max());
  for (size_t i = 0; i < num_physical_registers; ++i) {
    free_registers.push(static_cast<PHYSICAL_REGISTER_ID>(i));
  }
  physical_register_file = std::vector<physical_register>(num_physical_registers, {0, 0, false, false});
  correct_path_RAT.fill(-1); // default value for no mapping
  wrong_path_RAT.fill(-1);
  backend_RAT.fill(-1);
}

PHYSICAL_REGISTER_ID RegisterAllocator::rename_dest_register(int16_t reg, champsim::program_ordered<ooo_model_instr>::id_type producer_id, bool wrong_path)
{
  assert(!free_registers.empty());

  PHYSICAL_REGISTER_ID phys_reg = free_registers.front();
  free_registers.pop();

  // wrong-path instrs. see all older renames, so correct-path renames update both frontend RATs; wrong-path renames only update the wrong-path RAT
  wrong_path_RAT[reg] = phys_reg;
  if (!wrong_path) {
    correct_path_RAT[reg] = phys_reg;
  }
  physical_register_file.at(phys_reg) = {(uint16_t)reg, producer_id, false, true}; // arch_reg_index, producing_inst_id, valid, busy

  return phys_reg;
}

PHYSICAL_REGISTER_ID RegisterAllocator::rename_src_register(int16_t reg, bool wrong_path)
{
  PHYSICAL_REGISTER_ID phys = wrong_path ? wrong_path_RAT[reg] : correct_path_RAT[reg];

  if (phys < 0) {
    // allocate the register if it hasn't yet been mapped
    // (common due to the traces being slices in the middle of a program)
    phys = free_registers.front();
    free_registers.pop();
    // we assume this register's last write has been committed, so it is visible to both paths
    // (if it were unmapped in only one frontend RAT, the wrong-path RAT would be missing an older correct-path rename)
    correct_path_RAT[reg] = phys;
    wrong_path_RAT[reg] = phys;
    backend_RAT[reg] = phys;
    physical_register_file.at(phys) = {(uint16_t)reg, 0, true, true}; // arch_reg_index, producing_inst_id, valid, busy
  }

  return phys;
}

void RegisterAllocator::complete_dest_register(PHYSICAL_REGISTER_ID physreg)
{
  // mark the physical register as valid
  physical_register_file.at(physreg).valid = true;
}

void RegisterAllocator::retire_dest_register(PHYSICAL_REGISTER_ID physreg)
{
  // grab the arch reg index, find old phys reg in backend RAT
  uint16_t arch_reg = physical_register_file.at(physreg).arch_reg_index;
  PHYSICAL_REGISTER_ID old_phys_reg = backend_RAT[arch_reg];

  // update the backend RAT with the new phys reg
  backend_RAT[arch_reg] = physreg;

  // free the old phys reg; no frontend RAT can still reference it, since a younger correct-path write to the same arch reg has now committed
  if (old_phys_reg != -1) {
    free_register(old_phys_reg);
  }
}

void RegisterAllocator::free_register(PHYSICAL_REGISTER_ID physreg)
{
  physical_register_file.at(physreg) = {255, 0, false, false}; // arch_reg_index, producing_inst_id, valid, busy
  free_registers.push(physreg);
}

bool RegisterAllocator::isValid(PHYSICAL_REGISTER_ID physreg) const
{
  if (physreg < 0 || static_cast<size_t>(physreg) >= physical_register_file.size())
    return false;
  return physical_register_file[physreg].valid;
}

bool RegisterAllocator::isAllocated(PHYSICAL_REGISTER_ID archreg, bool wrong_path) const
{
  return (wrong_path ? wrong_path_RAT[archreg] : correct_path_RAT[archreg]) != -1;
}

unsigned long RegisterAllocator::count_free_registers() const { return std::size(free_registers); }

int RegisterAllocator::count_reg_dependencies(const ooo_model_instr& instr) const
{
  return static_cast<int>(std::count_if(std::begin(instr.source_registers), std::end(instr.source_registers), [this](auto reg) { return !isValid(reg); }));
}

void RegisterAllocator::reset_wrong_path_RAT()
{
  // discard wrong-path renames; the registers they allocated are freed when the wrong-path instructions are squashed
  std::copy(std::begin(correct_path_RAT), std::end(correct_path_RAT), std::begin(wrong_path_RAT));
}

void RegisterAllocator::print_deadlock()
{
  fmt::print("Register Allocation Tables\n");
  fmt::print("Arch reg    Correct-path phys reg    Wrong-path phys reg    Backend phys reg\n");
  for (size_t i = 0; i < correct_path_RAT.size(); ++i) {
    fmt::print("{:8}    {:21}    {:19}    {:16}\n", i, correct_path_RAT[i], wrong_path_RAT[i], backend_RAT[i]);
  }

  if (count_free_registers() == 0) {
    fmt::print("\n**WARNING!! WARNING!!** THE PHYSICAL REGISTER FILE IS COMPLETELY OCCUPIED.\n");
    fmt::print("It is extremely likely your register file size is too small.\n");
  }

  fmt::print("\nPhysical Register File\n");
  for (size_t i = 0; i < physical_register_file.size(); ++i) {
    fmt::print("Phys reg: {:3}\t Arch reg: {:3}\t Producer: {}\t Valid: {}\t Busy: {}\n", static_cast<int>(i),
               static_cast<int>(physical_register_file.at(i).arch_reg_index), physical_register_file.at(i).producing_instruction_id,
               physical_register_file.at(i).valid, physical_register_file.at(i).busy);
  }
  fmt::print("\n");
}
