/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "pmm.h"
#include "vmm.h"
#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

uint64 sys_user_better_allocate(uint64 size){
    uint64 va = alloc_va(size);
    if(va == 0) {
    // 分配一个物理页面并映射到用户空间
    uint64 pa = (uint64)alloc_page();
    user_vm_map(current->pagetable, g_ufree_page, PGSIZE, pa, prot_to_type(PROT_READ | PROT_WRITE, 1));
    
    // 更新未分配列表
    current->free_MR_list[current->free_MR_cursor].va = g_ufree_page;
    current->free_MR_list[current->free_MR_cursor].sz = PGSIZE;
    current->free_MR_cursor++;
    g_ufree_page += PGSIZE;

    // 检查是否有连续的未分配空间，若有则合并
    if (current->free_MR_cursor >= 2) {
        int secondLastIndex = current->free_MR_cursor - 2;
        int lastIndex = current->free_MR_cursor - 1;
        if (current->free_MR_list[secondLastIndex].va + current->free_MR_list[secondLastIndex].sz ==
            current->free_MR_list[lastIndex].va) {
            current->free_MR_list[secondLastIndex].sz += current->free_MR_list[lastIndex].sz;
            current->free_MR_cursor--;
        }
    }
        va = alloc_va(size);
    }
    return va;
}
uint64 sys_user_better_free(uint64 va) {
    // 在已分配列表中查找要释放的虚拟地址
    int index;
    for (index = 0; index < current->allocd_MR_cursor; index++) {
        if (current->allocd_MR_list[index].va == va) {
            break;
        }
    }

    // 如果未找到要释放的虚拟地址，直接返回
    if (index == current->allocd_MR_cursor) {
        return -1;
    }

    // 获取要释放的空间大小
    uint64 size = current->allocd_MR_list[index].sz;

    // 从已分配列表中移除要释放的虚拟地址
    for (int i = index; i < current->allocd_MR_cursor - 1; i++) {
        current->allocd_MR_list[i] = current->allocd_MR_list[i + 1];
    }
    current->allocd_MR_cursor--;

    // 将释放的空间添加到未分配列表中
    add_to_free_MR_list(va, size);
    return 0;
}
//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_better_allocate(a1);
    case SYS_user_free_page:
      return sys_user_better_free(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
