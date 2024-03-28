#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"

typedef struct trapframe_t {
  // space to store context (all common registers)
  /* offset:0   */ riscv_regs regs;

  // process's "user kernel" stack
  /* offset:248 */ uint64 kernel_sp;
  // pointer to smode_trap_handler
  /* offset:256 */ uint64 kernel_trap;
  // saved user process counter
  /* offset:264 */ uint64 epc;

  // kernel page table. added @lab2_1
  /* offset:272 */ uint64 kernel_satp;
}trapframe;
typedef struct MemoryRegion_t{
    uint64 va;
    uint64 sz;
}MemoryRegion;
// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // user page table
  pagetable_t pagetable;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;
  MemoryRegion allocd_MR_list[30];
  int allocd_MR_cursor;
  MemoryRegion free_MR_list[30];
  int free_MR_cursor;
}process;

// switch to run user app
void switch_to(process*);

uint64 alloc_va(uint64 size);
void add_to_free_MR_list(uint64 va, uint64 size);
// current running process
extern process* current;

// address of the first free page in our simple heap. added @lab2_2
extern uint64 g_ufree_page;

#endif
