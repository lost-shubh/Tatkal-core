#include "ipc.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

static int wait_mutex(sem_t *s) {
    int rc;
    do { rc = sem_wait(s); } while (rc < 0 && errno == EINTR);
    return rc;
}

int ipc_ring_init(const char *name, SharedRing **ring) {
retry: ;
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 && errno==EEXIST) {
        fd=shm_open(name,O_RDWR,0600);
        if(fd<0) return -1;
        SharedRing *stale=mmap(NULL,sizeof *stale,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        if(stale==MAP_FAILED){close(fd);return -1;}
        pid_t owner=stale->owner_pid; uint32_t magic=stale->magic;
        munmap(stale,sizeof *stale); close(fd);
        if(magic==IPC_RING_MAGIC && owner>0 && (kill(owner,0)==0 || errno==EPERM)) { errno=EADDRINUSE; return -1; }
        if(shm_unlink(name)!=0 && errno!=ENOENT) return -1;
        goto retry;
    }
    if (fd < 0) return -1;
    if (ftruncate(fd, sizeof(SharedRing)) != 0) {
        close(fd); shm_unlink(name); return -1;
    }
    SharedRing *q = mmap(NULL, sizeof(SharedRing), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (q == MAP_FAILED) {
        close(fd); shm_unlink(name); return -1;
    }
    if (sem_init(&q->mutex, 1, 1) != 0 ||
        sem_init(&q->slots, 1, 8) != 0 ||
        sem_init(&q->items, 1, 0) != 0) {
        munmap(q, sizeof(SharedRing)); close(fd); shm_unlink(name); return -1;
    }
    q->magic=IPC_RING_MAGIC; q->owner_pid=getpid(); q->head = q->tail = 0;
    *ring = q;
    close(fd);
    return 0;
}

void ipc_ring_cleanup(const char *name, SharedRing *ring) {
    if (ring) {
        sem_destroy(&ring->mutex);
        sem_destroy(&ring->slots);
        sem_destroy(&ring->items);
        munmap(ring, sizeof(SharedRing));
    }
    shm_unlink(name);
}

int ipc_ring_push(SharedRing *q, BookingRequest r) {
    if (wait_mutex(&q->slots) != 0) return -1;
    if (wait_mutex(&q->mutex) != 0) { sem_post(&q->slots); return -1; }
    q->entries[q->tail] = r;
    q->tail = (q->tail + 1) % 8;
    sem_post(&q->mutex);
    sem_post(&q->items);
    return 0;
}

int ipc_ring_pop(SharedRing *q, BookingRequest *r) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) return -1;
    deadline.tv_nsec += 100000000L;
    if(deadline.tv_nsec>=1000000000L){deadline.tv_sec++;deadline.tv_nsec-=1000000000L;}
    if (sem_timedwait(&q->items, &deadline) != 0) return -1;
    if (wait_mutex(&q->mutex) != 0) { sem_post(&q->items); return -1; }
    *r = q->entries[q->head];
    q->head = (q->head + 1) % 8;
    sem_post(&q->mutex);
    sem_post(&q->slots);
    return 0;
}
