/*
 * Interface functions between file system and kernel/processes. added @lab4_1
 */

#include "proc_file.h"

#include "hostfs.h"
#include "pmm.h"
#include "process.h"
#include "ramdev.h"
#include "rfs.h"
#include "riscv.h"
#include "spike_interface/spike_file.h"
#include "spike_interface/spike_utils.h"
#include "util/functions.h"
#include "util/string.h"

//
// initialize file system
//
void fs_init(void) {
  // initialize the vfs
  vfs_init();

  // register hostfs and mount it as the root
  if( register_hostfs() < 0 ) panic( "fs_init: cannot register hostfs.\n" );
  struct device *hostdev = init_host_device("HOSTDEV");
  vfs_mount("HOSTDEV", MOUNT_AS_ROOT);

  // register and mount rfs
  if( register_rfs() < 0 ) panic( "fs_init: cannot register rfs.\n" );
  struct device *ramdisk0 = init_rfs_device("RAMDISK0");
  rfs_format_dev(ramdisk0);
  vfs_mount("RAMDISK0", MOUNT_DEFAULT);
}

//
// initialize a proc_file_management data structure for a process.
// return the pointer to the page containing the data structure.
//
proc_file_management *init_proc_file_management(void) {
  proc_file_management *pfiles = (proc_file_management *)alloc_page();
  pfiles->cwd = vfs_root_dentry; // by default, cwd is the root
  pfiles->nfiles = 0;

  for (int fd = 0; fd < MAX_FILES; ++fd)
    pfiles->opened_files[fd].status = FD_NONE;

  sprint("FS: created a file management struct for a process.\n");
  return pfiles;
}

//
// reclaim the open-file management data structure of a process.
// note: this function is not used as PKE does not actually reclaim a process.
//
void reclaim_proc_file_management(proc_file_management *pfiles) {
  free_page(pfiles);
  return;
}

//
// get an opened file from proc->opened_file array.
// return: the pointer to the opened file structure.
//
struct file *get_opened_file(int fd) {
  struct file *pfile = NULL;

  // browse opened file list to locate the fd
  for (int i = 0; i < MAX_FILES; ++i) {
    pfile = &(current->pfiles->opened_files[i]);  // file entry
    if (i == fd) break;
  }
  if (pfile == NULL) panic("do_read: invalid fd!\n");
  return pfile;
}

//
// open a file named as "pathname" with the permission of "flags".
// return: -1 on failure; non-zero file-descriptor on success.
//
int do_open(char *pathname, int flags) {
  struct file *opened_file = NULL;
  if ((opened_file = vfs_open(pathname, flags)) == NULL) return -1;

  int fd = 0;
  if (current->pfiles->nfiles >= MAX_FILES) {
    panic("do_open: no file entry for current process!\n");
  }
  struct file *pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current->pfiles->nfiles;
  return fd;
}

//
// read content of a file ("fd") into "buf" for "count".
// return: actual length of data read from the file.
//
int do_read(int fd, char *buf, uint64 count) {
  struct file *pfile = get_opened_file(fd);

  if (pfile->readable == 0) panic("do_read: no readable file!\n");

  char buffer[count + 1];
  int len = vfs_read(pfile, buffer, count);
  buffer[count] = '\0';
  strcpy(buf, buffer);
  return len;
}

//
// write content ("buf") whose length is "count" to a file "fd".
// return: actual length of data written to the file.
//
int do_write(int fd, char *buf, uint64 count) {
  struct file *pfile = get_opened_file(fd);

  if (pfile->writable == 0) panic("do_write: cannot write file!\n");

  int len = vfs_write(pfile, buf, count);
  return len;
}

//
// reposition the file offset
//
int do_lseek(int fd, int offset, int whence) {
  struct file *pfile = get_opened_file(fd);
  return vfs_lseek(pfile, offset, whence);
}

//
// read the vinode information
//
int do_stat(int fd, struct istat *istat) {
  struct file *pfile = get_opened_file(fd);
  return vfs_stat(pfile, istat);
}

//
// read the inode information on the disk
//
int do_disk_stat(int fd, struct istat *istat) {
  struct file *pfile = get_opened_file(fd);
  return vfs_disk_stat(pfile, istat);
}

//
// close a file
//
int do_close(int fd) {
  struct file *pfile = get_opened_file(fd);
  return vfs_close(pfile);
}

//
// open a directory
// return: the fd of the directory file
//
int do_opendir(char *pathname) {
  struct file *opened_file = NULL;
  if ((opened_file = vfs_opendir(pathname)) == NULL) return -1;

  int fd = 0;
  struct file *pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }
  if (pfile->status != FD_NONE)  // no free entry
    panic("do_opendir: no file entry for current process!\n");

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current->pfiles->nfiles;
  return fd;
}

//
// read a directory entry
//
int do_readdir(int fd, struct dir *dir) {
  struct file *pfile = get_opened_file(fd);
  return vfs_readdir(pfile, dir);
}

//
// make a new directory
//
int do_mkdir(char *pathname) {
  return vfs_mkdir(pathname);
}

//
// close a directory
//
int do_closedir(int fd) {
  struct file *pfile = get_opened_file(fd);
  return vfs_closedir(pfile);
}

//
// create hard link to a file
//
int do_link(char *oldpath, char *newpath) {
  return vfs_link(oldpath, newpath);
}

//
// remove a hard link to a file
//
int do_unlink(char *path) {
  return vfs_unlink(path);
}

//
// (根据当前进程current的pfiles字段的cwd字段，将工作目录的绝对路径存入path中。)->(直接另外保存cwd字段，不然每次改变cwd，都要重新打开至工作目录，新建很多不需要的dentry。。)
//
int do_rcwd(char* path)
{
  struct dentry* cwd_entry = current->pfiles->cwd; // 当前工作目录的dentry
  //char cwd[MAX_PATH_LEN] = {0}; // 用于存储当前工作路径的绝对路径
  memset(path, 0, MAX_PATH_LEN);
  //接下来，先找到当前工作路径的绝对路径，并存储到cwd中。
  struct dentry* cur = cwd_entry;
  //if(cur->parent) sprint("do_rcwd: parent_name=%s\n",cur->parent->name);
  while(cur->parent) // 到达根目录/单独考虑
  {
    //sprint("do_rcwd: parent_name_inside=%s\n",cur->name);
    memmove(path + strlen(cur->name) + 2, path, strlen(path) + 1);
    strcpy(path + 1, cur->name); // 新查到的节点名插入path中
    path[0] = '/';
    cur = cur->parent; //查了半天发现这句没写。。。
  }
  path[0] = '/';
  sprint("do_rcwd: name=%s\n",path);
  return 0;
}

int do_ccwd(char* path) // Now can only handle absolute path, TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
{
  char ab_path[MAX_PATH_LEN]={0};
  get_absolute_path(ab_path, path);
  struct dentry *parent = vfs_root_dentry; // we start the path lookup from root.
  char miss_name[MAX_PATH_LEN];
  // path lookup.
  struct dentry *file_dentry = lookup_final_dentry(ab_path, &parent, miss_name);
  current->pfiles->cwd = file_dentry;
  sprint("do_ccwd: name=%s\n",current->pfiles->cwd->name);
  return 0;
}


//
// Get an absolute path
// ab_path is the final absolute path. ori_path could be a relative path, or it could be an absolute path already,
// in which case ori_path will be directly copied to ab_path.
//
void get_absolute_path(char* ab_path, char* ori_path)
{
  if(ori_path[0]!='.')//ori_path is already an absolute path
  {
    memcpy(ab_path, ori_path, MAX_PATH_LEN);
    return;
  }
  char cwd[MAX_PATH_LEN] = {0}; // 用于存储当前工作路径的绝对路径
  do_rcwd(cwd); //调用rcwd，得到工作目录的绝对路径。
  int cwd_len = strlen(cwd);
  //先处理..开头的情况
  if(ori_path[1]=='.')
  {
    int last_slash = 0;
    for(;cwd[cwd_len - last_slash - 1]!='/'; last_slash++);
    strcpy(cwd + cwd_len - last_slash, ori_path + 3);
  }
  else if(cwd_len == 1) //.开头，cwd为根目录
  {
    strcpy(cwd + 1, ori_path + 2);
  }
  else//.开头，cwd不为根目录
  {
    strcpy(cwd + cwd_len + 1, ori_path + 2);
    cwd[cwd_len] = '/';
  }
  sprint("get_absolute_path: %s\n", cwd);
  strcpy(ab_path, cwd);

}