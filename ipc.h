#ifndef TATKAL_IPC_H
#define TATKAL_IPC_H

#include "tatkal.h"
#include <semaphore.h>
#include <sys/types.h>

#define IPC_RING_MAGIC 0x54435247u

typedef struct {
    sem_t mutex, slots, items;
    uint32_t magic;
    pid_t owner_pid;
    int head, tail;
    BookingRequest entries[8];
} SharedRing;

int ipc_ring_init(const char *name, SharedRing **ring);
void ipc_ring_cleanup(const char *name, SharedRing *ring);
int ipc_ring_push(SharedRing *q, BookingRequest r);
int ipc_ring_pop(SharedRing *q, BookingRequest *r);

#endif
