/*
 * Utility functions for process management. 
 *
 * Note: in Lab1, only one process (i.e., our user application) exists. Therefore, 
 * PKE OS at this stage will set "current" to the loaded user application, and also
 * switch to the old "current" process after trap handling.
 */

#include "riscv.h"
#include "strap.h"
#include "config.h"
#include "process.h"
#include "elf.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// current points to the currently running user-mode application.
process* current = NULL;

// points to the first free page in our simple heap. added @lab2_2
uint64 g_ufree_page = USER_FREE_ADDRESS_START;

//
// switch to a user-mode process
//
void switch_to(process* proc) {
  assert(proc);
  current = proc;

  // write the smode_trap_vector (64-bit func. address) defined in kernel/strap_vector.S
  // to the stvec privilege register, such that trap handler pointed by smode_trap_vector
  // will be triggered when an interrupt occurs in S mode.
  write_csr(stvec, (uint64)smode_trap_vector);

  // set up trapframe values (in process structure) that smode_trap_vector will need when
  // the process next re-enters the kernel.
  proc->trapframe->kernel_sp = proc->kstack;      // process's kernel stack
  proc->trapframe->kernel_satp = read_csr(satp);  // kernel page table
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;

  // SSTATUS_SPP and SSTATUS_SPIE are defined in kernel/riscv.h
  // set S Previous Privilege mode (the SSTATUS_SPP bit in sstatus register) to User mode.
  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode

  // write x back to 'sstatus' register to enable interrupts, and sret destination mode.
  write_csr(sstatus, x);

  // set S Exception Program Counter (sepc register) to the elf entry pc.
  write_csr(sepc, proc->trapframe->epc);

  // make user page table. macro MAKE_SATP is defined in kernel/riscv.h. added @lab2_1
  uint64 user_satp = MAKE_SATP(proc->pagetable);

  // return_to_user() is defined in kernel/strap_vector.S. switch to user mode with sret.
  // note, return_to_user takes two parameters @ and after lab2_1.
  return_to_user(proc->trapframe, user_satp);
}
uint64 alloc_va(uint64 size) {
  // 查找未分配列表中第一个能够容纳所需大小的项
  int index = 0;
  while (index < current->free_MR_cursor && current->free_MR_list[index].sz < size) {
      index++;
  }
  // 如果未找到合适的空间，返回0
  if (index == current->free_MR_cursor) {
      return 0;
  }
  // 获取分配的虚拟地址
  uint64 va = current->free_MR_list[index].va;
  // 更新未分配列表
  if (current->free_MR_list[index].sz > size) {
      // 如果剩余空间大于所需大小，更新未分配列表项
      current->free_MR_list[index].sz -= size;
      current->free_MR_list[index].va += size;
  }
  else {
    // 如果剩余空间等于所需大小，移除当前项
    int i = index;
    while (i < current->free_MR_cursor - 1) {
    current->free_MR_list[i] = current->free_MR_list[i + 1];
    i++; 
    }

      current->free_MR_cursor--;
  }
  // 更新已分配列表
  current->allocd_MR_list[current->allocd_MR_cursor].va = va;
  current->allocd_MR_list[current->allocd_MR_cursor].sz = size;
  current->allocd_MR_cursor++;

  return va;
}

// 将空间添加到未分配列表中
void add_to_free_MR_list(uint64 va, uint64 size) {
    int index = 0;

    // 寻找要插入的位置
    while (index < current->free_MR_cursor && va > current->free_MR_list[index].va) {
        index++;
    }

    // 如果插入位置与前一个未分配空间相邻，合并两者
    if (index > 0 && current->free_MR_list[index - 1].va + current->free_MR_list[index - 1].sz == va) {
        current->free_MR_list[index - 1].sz += size;
    } else {
        // 否则，插入新的未分配空间
        for (int i = current->free_MR_cursor; i > index; i--) {
            current->free_MR_list[i] = current->free_MR_list[i - 1];
        }
        current->free_MR_list[index].va = va;
        current->free_MR_list[index].sz = size;
        current->free_MR_cursor++;
    }

    // 如果插入位置与后一个未分配空间相邻，合并两者
    if (index < current->free_MR_cursor - 1 && va + size == current->free_MR_list[index + 1].va) {
        current->free_MR_list[index].sz += current->free_MR_list[index + 1].sz;
        for (int i = index + 1; i < current->free_MR_cursor - 1; i++) {
            current->free_MR_list[i] = current->free_MR_list[i + 1];
        }
        current->free_MR_cursor--;
    }
}