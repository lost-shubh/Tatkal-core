#include "tatkal.h"
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ipc.h"

/* A minimal supervisor/child IPC demonstration.  The semaphore lives inside
   shared memory and is explicitly process-shared, so this is not just a
   copied address space after fork(). */
typedef struct { sem_t lock; int accepted; int rejected; } SharedCounters;

int run_ipc_demo(FILE *out) {

    char name[64];
    snprintf(name, sizeof name, "/tatkal_core_%ld", (long)getpid());
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 || ftruncate(fd, sizeof(SharedCounters)) != 0) {
        if (fd >= 0) { close(fd); shm_unlink(name); }
        perror("shared-memory setup"); return -1;
    }
    SharedCounters *c = mmap(NULL, sizeof *c, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (c == MAP_FAILED || sem_init(&c->lock, 1, 1) != 0) {
        if (c != MAP_FAILED) munmap(c, sizeof *c);
        close(fd); shm_unlink(name); perror("shared-memory initialization"); return -1;
    }
    c->accepted = 0; c->rejected = 0;
    pid_t child = fork();
    if (child == 0) {
        sem_wait(&c->lock); c->accepted += 3; c->rejected += 1; sem_post(&c->lock);
        munmap(c, sizeof *c); close(fd); _exit(0);
    }
    if (child < 0) { perror("fork"); sem_destroy(&c->lock); munmap(c, sizeof *c); close(fd); shm_unlink(name); return -1; }
    int status; waitpid(child, &status, 0);
    sem_wait(&c->lock);
    fprintf(out, "IPC demo: child=%ld accepted=%d rejected=%d status=%s\n", (long)child, c->accepted, c->rejected, WIFEXITED(status) && WEXITSTATUS(status) == 0 ? "reaped" : "failed");
    sem_post(&c->lock);
    sem_destroy(&c->lock); munmap(c, sizeof *c); close(fd); shm_unlink(name);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

int run_ipc_queue_demo(FILE *out) {
    char name[64]; snprintf(name, sizeof name, "/tatkal_ring_%ld", (long)getpid());
    int fd=shm_open(name,O_CREAT|O_EXCL|O_RDWR,0600);
    if(fd<0 || ftruncate(fd,sizeof(SharedRing))!=0){if(fd>=0){close(fd);shm_unlink(name);}perror("ring setup");return -1;}
    SharedRing *q=mmap(NULL,sizeof *q,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(q==MAP_FAILED || sem_init(&q->mutex,1,1)||sem_init(&q->slots,1,8)||sem_init(&q->items,1,0)){if(q!=MAP_FAILED)munmap(q,sizeof *q);close(fd);shm_unlink(name);perror("ring initialization");return -1;}
    q->head=q->tail=0; pid_t child=fork();
    if(child==0){for(int i=0;i<5;i++){BookingRequest r={.id=(uint64_t)i+1,.train_id=12001,.berth_count=1,.priority=PRIORITY_GENERAL};sem_wait(&q->slots);sem_wait(&q->mutex);q->entries[q->tail]=r;q->tail=(q->tail+1)%8;sem_post(&q->mutex);sem_post(&q->items);}munmap(q,sizeof *q);close(fd);_exit(0);}
    if(child<0){perror("fork");sem_destroy(&q->mutex);sem_destroy(&q->slots);sem_destroy(&q->items);munmap(q,sizeof *q);close(fd);shm_unlink(name);return -1;}
    int consumed=0; uint64_t id_sum=0; for(int i=0;i<5;i++){sem_wait(&q->items);sem_wait(&q->mutex);BookingRequest r=q->entries[q->head];q->head=(q->head+1)%8;sem_post(&q->mutex);sem_post(&q->slots);consumed++;id_sum+=r.id;}
    int status;waitpid(child,&status,0);fprintf(out,"IPC ring demo: produced=5 consumed=%d request_id_sum=%llu status=%s\n",consumed,(unsigned long long)id_sum,WIFEXITED(status)&&WEXITSTATUS(status)==0?"reaped":"failed");
    sem_destroy(&q->mutex);sem_destroy(&q->slots);sem_destroy(&q->items);munmap(q,sizeof *q);close(fd);shm_unlink(name);return WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-1;
}
