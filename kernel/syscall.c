/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>
#include "elf.h"
#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"

#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
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

ssize_t sys_user_print_backtrace(uint64 depth)
{
  //sprint("Test:%s\n",func_names[1]);
  uint64* fp = (uint64*)(*(uint64*)(current->trapframe->regs.s0 - 8)); //point to the fp before entering this function, this is done by *(current_fp+8).
  //uint64 return_addr = (*(uint64*)(current->trapframe->regs.s0 + 16));
  // uint64 return_addr = current->trapframe->regs.ra; //This is "Print backtrace"
  for(int i =0; i<depth; i++)
  {
    uint64 return_addr = *(fp - 1);
    fp = (uint64*)(*(fp - 2));
    for(int j=0; j<func_count; j++)
    {
      if( return_addr <= funcs[j].st_value+funcs[j].st_size && return_addr >= funcs[j].st_value)
      {
        sprint("%s\n",func_names[j]);
        if(!strcmp(func_names[j],"main"))
        {
          depth = 0;
          break;
        }
      }
    }
    

  }
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
    case SYS_user_print_backtrace:
      return sys_user_print_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
