/*
 * implementing the scheduler
 */

#include "sched.h"
#include "spike_interface/spike_utils.h"

process* ready_queue_head = NULL;
process* block_queue_head = NULL;

//
// insert a process, proc, into the END of ready queue.
//
void insert_to_ready_queue( process* proc ) {
  sprint( "going to insert process %d to ready queue.\n", proc->pid );
  // if the queue is empty in the beginning
  if( ready_queue_head == NULL ){
    proc->status = READY;
    proc->queue_next = NULL;
    ready_queue_head = proc;
    return;
  }

  // ready queue is not empty
  process *p;
  // browse the ready queue to see if proc is already in-queue
  for( p=ready_queue_head; p->queue_next!=NULL; p=p->queue_next )
    if( p == proc ) return;  //already in queue

  // p points to the last element of the ready queue
  if( p==proc ) return;
  p->queue_next = proc;
  proc->status = READY;
  proc->queue_next = NULL;

  return;
}
void insert_to_block_queue(process* proc) {
    if (block_queue_head == NULL) {
        // 如果阻塞队列为空，则将进程直接插入到队列头部
        proc->status = BLOCKED;
        proc->queue_next = NULL;
        block_queue_head = proc;
    } else {
        // 如果阻塞队列不为空，则遍历队列，查看进程是否已经在队列中
        process* p = block_queue_head;
        while (p->queue_next != NULL) {
            if (p == proc)
                return; // 进程已在队列中
            p = p->queue_next;
        }
        // 将进程插入到队列尾部
        if (p != proc) {
            p->queue_next = proc;
            proc->status = BLOCKED;
            proc->queue_next = NULL;
        }
    }
}


//
// choose a proc from the ready queue, and put it to run.
// note: schedule() does not take care of previous current process. If the current
// process is still runnable, you should place it into the ready queue (by calling
// ready_queue_insert), and then call schedule().
//
extern process procs[NPROC];
void schedule() {
  if ( !ready_queue_head ){
    // by default, if there are no ready process, and all processes are in the status of
    // FREE and ZOMBIE, we should shutdown the emulated RISC-V machine.
    int should_shutdown = 1;

    for( int i=0; i<NPROC; i++ )
      if( (procs[i].status != FREE) && (procs[i].status != ZOMBIE) ){
        should_shutdown = 0;
        sprint( "ready queue empty, but process %d is not in free/zombie state:%d\n", 
          i, procs[i].status );
      }

    if( should_shutdown ){
      sprint( "no more ready processes, system shutdown now.\n" );
      shutdown( 0 );
    }else{
      panic( "Not handled: we should let system wait for unfinished processes.\n" );
    }
  }


  current = ready_queue_head;
  assert( current->status == READY );
  ready_queue_head = ready_queue_head->queue_next;

  current->status = RUNNING;
  sprint( "going to schedule process %d to run.\n", current->pid );
  switch_to( current );
}

void schedule_b(process* proc) {
    if (!block_queue_head)
        return; // 如果阻塞队列为空，则直接返回

    process* p = block_queue_head;
    process* prev = NULL;

    // 寻找要调度的进程在阻塞队列中的位置
    while (p && p != proc) {
        prev = p;
        p = p->queue_next;
    }

    if (!p) {
        panic("fail on schedule_block"); // 如果未找到要调度的进程，则产生错误
        return;
    }

    // 从阻塞队列中移除要调度的进程
    if (prev)
        prev->queue_next = p->queue_next;
    else
        block_queue_head = p->queue_next;

    // 将要调度的进程插入到就绪队列中
    insert_to_ready_queue(proc);
}
