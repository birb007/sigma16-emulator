#pragma once
#include "instructions.h"
#include "vm.h"

/* user defined trap instructions */
extern void sigma16_trap_instruction(sigma16_vm_t*,
                                     enum sigma16_instruction_fmt);
extern void sigma16_trap_beg_execution(sigma16_vm_t*);
extern void sigma16_trap_end_execution(sigma16_vm_t*);
