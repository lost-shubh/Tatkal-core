#include "tatkal.h"
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct Pool {
    pthread_t *threads;
    int count;
    TaskQueue *queue;
    BookingSystem *booking;
    Wal *wal;
    Metrics *metrics;
} Pool;

static void *worker(void *arg){
    Pool *p = (Pool *)arg;
    BookingRequest r;
    while(queue_pop(p->queue, &r, p->metrics) == 0){
        BookingResult x = booking_commit(p->booking, p->wal, r);
        int wal_failed = x.status == REQUEST_REJECTED && !strcmp(x.reason, "persistence failure");
        uint64_t response = now_ms() - r.arrived_ms;

        pthread_mutex_lock(&p->metrics->lock);
        if(x.status == REQUEST_CONFIRMED) p->metrics->confirmed++;
        else p->metrics->rejected++;
        if(wal_failed) p->metrics->wal_failures++;

        p->metrics->response_total_ms += response;
        if(response > p->metrics->response_max_ms) p->metrics->response_max_ms = response;

        if(p->metrics->response_count < 10000) {
            p->metrics->response_times[p->metrics->response_count++] = response;
        }
        p->metrics->submitted++;
        pthread_mutex_unlock(&p->metrics->lock);

        if (r.wants_reply == 1) {
            char name[64];
            snprintf(name, sizeof name, "/tatkal_reply_%llu", (unsigned long long)r.id);
            mqd_t reply = mq_open(name, O_WRONLY | O_NONBLOCK);
            if (reply != (mqd_t)-1) {
                if (mq_send(reply, (const char *)&x, sizeof x, 0) != 0)
                    perror("customer reply unavailable");
                mq_close(reply);
            }
        } else if (r.wants_reply == 2 && r.reply_fd >= 0) {
            char reply[192]; int used;
            if (x.status == REQUEST_CONFIRMED) {
                used=snprintf(reply,sizeof reply,"CONFIRMED request=%llu train=%d berths=",(unsigned long long)x.request_id,x.train_id);
                for(int i=0;i<x.berth_count&&used<(int)sizeof reply-2;i++) used+=snprintf(reply+used,sizeof reply-(size_t)used,"%s%d",i?",":"",x.berth_ids[i]);
                snprintf(reply+(used<(int)sizeof reply?used:(int)sizeof reply-1),used<(int)sizeof reply?sizeof reply-(size_t)used:1,"\n");
            } else snprintf(reply,sizeof reply,"REJECTED request=%llu reason=%s\n",(unsigned long long)x.request_id,x.reason);
            (void)send(r.reply_fd,reply,strlen(reply),MSG_NOSIGNAL);
            close(r.reply_fd);
        }

        if (x.status == REQUEST_CONFIRMED) {
            printf("Reservation confirmed: train=%d request=%llu berths=", x.train_id,(unsigned long long)x.request_id);
            for (int i = 0; i < x.berth_count; i++) printf("%s%d", i ? "," : "", x.berth_ids[i]);
            puts("");
        } else {
            printf("Reservation rejected: request=%llu reason=%s\n", (unsigned long long)x.request_id, x.reason);
        }
        fflush(stdout);
    }
    return NULL;
}

Pool *pool_start(int n, TaskQueue *q, BookingSystem *b, Wal *w, Metrics *m){
    Pool *p = calloc(1, sizeof(Pool));
    if(!p) return NULL;
    p->threads = calloc(n, sizeof(pthread_t));
    if(!p->threads){ free(p); return NULL; }
    p->count = n;
    p->queue = q;
    p->booking = b;
    p->wal = w;
    p->metrics = m;
    for(int i=0; i<n; i++) if(pthread_create(&p->threads[i], 0, worker, p)){
        queue_stop(q);
        while(i--) pthread_join(p->threads[i], 0);
        free(p->threads); free(p); return NULL;
    }
    return p;
}

void pool_join(Pool *p){
    if(!p) return;
    queue_stop(p->queue);
    for(int i=0; i<p->count; i++) pthread_join(p->threads[i], 0);
    free(p->threads); free(p);
}
