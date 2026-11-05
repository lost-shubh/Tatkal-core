#include "tatkal.h"
#include <string.h>
int queue_init(TaskQueue *q){memset(q,0,sizeof(*q));return pthread_mutex_init(&q->lock,0)||pthread_cond_init(&q->not_empty,0)||pthread_cond_init(&q->not_full,0);}
void queue_destroy(TaskQueue *q){pthread_mutex_destroy(&q->lock);pthread_cond_destroy(&q->not_empty);pthread_cond_destroy(&q->not_full);}
int queue_push(TaskQueue *q, BookingRequest r, Metrics *m){int p=r.priority;if(p<PRIORITY_GENERAL||p>PRIORITY_HIGH)p=PRIORITY_GENERAL;pthread_mutex_lock(&q->lock);while(q->total==QUEUE_CAPACITY&&!q->stopping)pthread_cond_wait(&q->not_full,&q->lock);if(q->stopping){pthread_mutex_unlock(&q->lock);return -1;}q->items[p][q->tail[p]]=r;q->tail[p]=(q->tail[p]+1)%QUEUE_CAPACITY;q->count[p]++;q->total++;pthread_mutex_lock(&m->lock);if((uint64_t)q->total>m->queue_high_water)m->queue_high_water=q->total;pthread_mutex_unlock(&m->lock);pthread_cond_signal(&q->not_empty);pthread_mutex_unlock(&q->lock);return 0;}
int queue_try_push(TaskQueue *q,BookingRequest r,Metrics *m){int p=r.priority;if(p<PRIORITY_GENERAL||p>PRIORITY_HIGH)p=PRIORITY_GENERAL;pthread_mutex_lock(&q->lock);if(q->stopping||q->total==QUEUE_CAPACITY){pthread_mutex_unlock(&q->lock);return -1;}q->items[p][q->tail[p]]=r;q->tail[p]=(q->tail[p]+1)%QUEUE_CAPACITY;q->count[p]++;q->total++;pthread_mutex_lock(&m->lock);if((uint64_t)q->total>m->queue_high_water)m->queue_high_water=q->total;pthread_mutex_unlock(&m->lock);pthread_cond_signal(&q->not_empty);pthread_mutex_unlock(&q->lock);return 0;}
int queue_pop(TaskQueue *q,BookingRequest *r,Metrics *m){
    pthread_mutex_lock(&q->lock);while(!q->total&&!q->stopping)pthread_cond_wait(&q->not_empty,&q->lock);if(!q->total){pthread_mutex_unlock(&q->lock);return -1;}
    uint64_t now=now_ms(), oldest=0;int p=-1,aged=0;
    for(int i=PRIORITY_GENERAL;i<=PRIORITY_HIGH;i++)if(q->count[i]){uint64_t wait=now-q->items[i][q->head[i]].arrived_ms;if(wait>=200&&wait>=oldest){oldest=wait;p=i;aged=1;}}
    if(p<0)for(int i=PRIORITY_HIGH;i>=PRIORITY_GENERAL;i--)if(q->count[i]){p=i;break;}
    *r=q->items[p][q->head[p]];q->head[p]=(q->head[p]+1)%QUEUE_CAPACITY;q->count[p]--;q->total--;pthread_cond_signal(&q->not_full);pthread_mutex_unlock(&q->lock);
    uint64_t wait=now-r->arrived_ms;pthread_mutex_lock(&m->lock);m->dispatched[p]++;m->queue_wait_total_ms[p]+=wait;if(wait>m->queue_wait_max_ms[p])m->queue_wait_max_ms[p]=wait;if(aged)m->aged_promotions++;pthread_mutex_unlock(&m->lock);return 0;
}
void queue_stop(TaskQueue *q){pthread_mutex_lock(&q->lock);q->stopping=1;pthread_cond_broadcast(&q->not_empty);pthread_cond_broadcast(&q->not_full);pthread_mutex_unlock(&q->lock);}
