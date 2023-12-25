/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "spike_interface/spike_utils.h"
#define SYMTAB	2
#define STRTAB	3
elf_symbol funcs[100]; //each element stores the information of a function(address, type, etc.)
char func_names[100][32]; //each element stores the name of a function
int func_count = 0;

typedef struct elf_info_t {
  spike_file_t *f;
  process *p;
} elf_info;

//
// the implementation of allocater. allocates memory space for later segment loading
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  // directly returns the virtual address as we are in the Bare mode in lab1_x
  return (void *)elf_va;
}

//
// actual file reading, using the spike file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  // call spike file utility to load the content of elf file into memory.
  // spike_file_pread will read the elf file (msg->f) from offset to memory (indicated by
  // *dest) for nb bytes.
  return spike_file_pread(msg->f, dest, nb, offset);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;

  return EL_OK;
}

//
// load the elf segments to memory regions as we are in Bare mode in lab1
//
elf_status elf_load(elf_ctx *ctx) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;
  }

  return EL_OK;
}

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
static size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");

  sprint("Application: %s\n", arg_bug_msg.argv[0]);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");
  // after loading elf, parse all the symbols among which we have the function names we need.
  parse_symbols(&elfloader);
  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the host spike file
  spike_file_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

void read_shstrtab(char* shstrtab, elf_ctx *ctx)
{
  uint64 shstr_offset = ctx->ehdr.shoff + ctx->ehdr.shstrndx * ctx->ehdr.shentsize; //calculate offset
  section_header shstr_header;
  elf_fpread(ctx, (void *)&shstr_header, sizeof(shstr_header), shstr_offset);
  if(shstr_header.sh_size > 398)
  {
    panic("Need larger shstrtab string.");
  }
  elf_fpread(ctx, shstrtab, shstr_header.sh_size, shstr_header.sh_offset); //read the actual information of this section.
}

section_header read_symtab(elf_ctx *ctx)
{
  section_header symtab;
  uint16 section_amount = ctx->ehdr.shnum;//get the amount of sections from ctx
  for(int i=0; i<section_amount; i++)
  {
    uint64 current_offset = ctx->ehdr.shoff + i * ctx->ehdr.shentsize;
    elf_fpread(ctx, (void*)&symtab, sizeof(symtab), current_offset);
    if(symtab.sh_type==SYMTAB)
    {
      return symtab;
    }
  }
  panic("Can't find symtab!");
}

section_header read_strtab(char* shstrtab, elf_ctx *ctx)
{
  section_header strtab;
  uint16 section_amount = ctx->ehdr.shnum;//get the amount of sections from ctx
  for(int i=0; i<section_amount; i++)
  {
    uint64 current_offset = ctx->ehdr.shoff + i * ctx->ehdr.shentsize;
    elf_fpread(ctx, (void*)&strtab, sizeof(strtab), current_offset);
    if(strtab.sh_type==STRTAB && !(strcmp(shstrtab + strtab.sh_name,".strtab")))
    {
      return strtab;
    }
  }
  panic("Can't find strtab!");
}

void find_all_function_names_and_information(section_header symtab_h, section_header strtab_h, elf_ctx *ctx)
{
  uint64 symbol_amount = symtab_h.sh_size/sizeof(elf_symbol);
  for(int i=0; i<symbol_amount; i++)
  {
    uint64 current_offset = symtab_h.sh_offset + sizeof(elf_symbol) * i;
    elf_symbol symbol_temp;
    elf_fpread(ctx, (void*)&symbol_temp, sizeof(symbol_temp), current_offset);
    if((symbol_temp.st_info & 0xF) == STT_FUNC) //jusge whether is a function
    {
      funcs[func_count] = symbol_temp; //Now we know it is a function.
      uint64 func_name_offset = strtab_h.sh_offset + symbol_temp.st_name; //the offset of the current function's name in the file.
      elf_fpread(ctx, (void*)func_names[func_count], 32, func_name_offset);
      func_count++;
    }
  }
}

void parse_symbols(elf_ctx *ctx)
{
  char shstrtab[400]={0};
  read_shstrtab(shstrtab, ctx); //read shstrtab
  section_header symtab_h = read_symtab(ctx);
  section_header strtab_h = read_strtab(shstrtab, ctx);
  find_all_function_names_and_information(symtab_h, strtab_h, ctx);
}