#ifndef _SCHED_H_
#define _SCHED_H_

#include "process.h"

//length of a time slice, in number of ticks
#define TIME_SLICE_LEN  2

void insert_to_ready_queue( process* proc );
void insert_to_block_queue( process* proc );
void schedule_b(process* proc);
void schedule();
typedef struct semaphore{
    int sem;
    process* wait[5];
}Semaphore;

extern Semaphore sem_list[5];
extern int count;
#endif
