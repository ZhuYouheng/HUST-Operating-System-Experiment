/*
 * header file to be used by applications.
 */

int printu(const char *s, ...);
int exit(int code);
void* naive_malloc();
void naive_free(void* va);
int fork();
void yield();
int sem_new(int semaphore);
void sem_P(int semaphore_id);
void sem_V(int semaphore_id);