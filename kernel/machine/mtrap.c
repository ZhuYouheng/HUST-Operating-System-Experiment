#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "string.h"

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

void print_error_info();
void print_error_code();
static void handle_illegal_instruction() { print_error_info(); panic("Illegal instruction!"); }

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      //panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );
      handle_illegal_instruction();

      break;
    case CAUSE_MISALIGNED_LOAD:
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}

void print_error_info()
{
  // "process->line" stores all relationships map instruction addresses to code line numbers
  addr_line* error_line = current->line;
  uint64 error_addr = read_csr(mepc);
  int line_id = 0;
  while(error_line->addr != error_addr)
  {
    line_id++;error_line++;
    if(line_id == current->line_ind)
    {
      panic("Can't find error line for current address.\n");
    }
  }
  code_file* error_file = current->file + error_line->file;
  char* error_dic = *(current->dir + error_file->dir); //error dic name
  char* error_file_name = error_file->file; //errror file name
  int error_line_id = error_line->line;
  sprint("Runtime error at %s/%s:%d\n",error_dic ,error_file_name, error_line_id);
  print_error_code(error_dic, error_file_name, error_line_id);
}

void print_error_code(char* error_dic, char* error_file_name, int error_line_id)
{
  char path[50] = {};
  char code_file[1000] = {};
  //char dest_code[50] = {};
  int dic_len = strlen(error_dic);
  strcpy(path, error_dic);
  strcpy(path + 1 + dic_len, error_file_name);
  path[dic_len] = '/';
  spike_file_t* spike_error_file = spike_file_open(path, O_RDONLY, 0);
  //first read code file
  spike_file_pread(spike_error_file, (void*)code_file,sizeof(code_file),0);
  int cursor = 0;
  int line_count = 1;
  while(cursor < sizeof(code_file))
  {
    if(1 == error_line_id)
    {
      break;
    }
    if(code_file[cursor++] == '\n')
    {
      line_count++;
      if(line_count == error_line_id)
      {
        int i;
        for(i = 0; code_file[cursor+i]!='\n' && code_file[cursor+i]!='\0';i++);
        code_file[cursor+i] = '\0';
        break;
      }
    }
  }
  if(cursor == sizeof(code_file))
  {
    panic("target error code not found!\n");
  }
  //print error code
  sprint("%s\n",code_file + cursor);
  spike_file_close(spike_error_file);
}