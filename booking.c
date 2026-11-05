#include "tatkal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000; }

int booking_init(BookingSystem *s) {
    memset(s,0,sizeof *s);
    if(pthread_mutex_init(&s->train_lock,0)||pthread_mutex_init(&s->transaction_lock,0)||pthread_mutex_init(&s->seen_lock,0)) return -1;
    for(int t=0;t<MAX_TRAINS;t++) for(int b=0;b<MAX_BERTHS;b++) if(pthread_rwlock_init(&s->berths[t][b].lock,0)) return -1;
    return 0;
}
void booking_destroy(BookingSystem *s) { for(int t=0;t<MAX_TRAINS;t++) for(int b=0;b<MAX_BERTHS;b++) pthread_rwlock_destroy(&s->berths[t][b].lock); pthread_mutex_destroy(&s->seen_lock); pthread_mutex_destroy(&s->transaction_lock); pthread_mutex_destroy(&s->train_lock); free(s->seen_ids); }
static int train_slot(BookingSystem *s,int id,int create) { int slot=-1; pthread_mutex_lock(&s->train_lock); for(int i=0;i<MAX_TRAINS;i++)if(s->train_ids[i]==id){slot=i;break;} if(slot<0&&create)for(int i=0;i<MAX_TRAINS;i++)if(!s->train_ids[i]){s->train_ids[i]=id;slot=i;break;} pthread_mutex_unlock(&s->train_lock); return slot; }
static int seen_or_add(BookingSystem *s,uint64_t id) { pthread_mutex_lock(&s->seen_lock); for(size_t i=0;i<s->seen_count;i++)if(s->seen_ids[i]==id){pthread_mutex_unlock(&s->seen_lock);return 1;} if(s->seen_count==s->seen_cap){size_t n=s->seen_cap?s->seen_cap*2:128;uint64_t *p=realloc(s->seen_ids,n*sizeof*p);if(!p){pthread_mutex_unlock(&s->seen_lock);return 1;}s->seen_ids=p;s->seen_cap=n;}s->seen_ids[s->seen_count++]=id;pthread_mutex_unlock(&s->seen_lock);return 0; }
static BookingResult process_locked(BookingSystem *s,BookingRequest r) {
    BookingResult x={.request_id=r.id,.train_id=r.train_id,.status=REQUEST_REJECTED};
    if(!r.id||r.train_id<1||r.train_id>99999||r.berth_count<1||r.berth_count>MAX_REQUEST_BERTHS){strcpy(x.reason,"invalid request");return x;}
    if(seen_or_add(s,r.id)){strcpy(x.reason,"duplicate request ID");return x;} int t=train_slot(s,r.train_id,1);if(t<0){strcpy(x.reason,"train inventory limit");return x;}
    int picked[MAX_REQUEST_BERTHS],n=0,start=(r.preferred_berth>=0&&r.preferred_berth<MAX_BERTHS)?r.preferred_berth:0;
    for(int pass=0;pass<2&&n<r.berth_count;pass++)for(int b=pass?0:start;b<(pass?start:MAX_BERTHS)&&n<r.berth_count;b++){pthread_rwlock_wrlock(&s->berths[t][b].lock);if(!s->berths[t][b].reserved_by)picked[n++]=b;else pthread_rwlock_unlock(&s->berths[t][b].lock);}
    if(n!=r.berth_count){for(int i=0;i<n;i++)pthread_rwlock_unlock(&s->berths[t][picked[i]].lock);strcpy(x.reason,"berths unavailable");return x;}
    for(int i=0;i<n;i++){s->berths[t][picked[i]].reserved_by=r.id;x.berth_ids[i]=picked[i];pthread_rwlock_unlock(&s->berths[t][picked[i]].lock);}x.berth_count=n;x.status=REQUEST_CONFIRMED;strcpy(x.reason,"confirmed");return x;
}
static void rollback_locked(BookingSystem *s,const BookingResult *r) { int t=train_slot(s,r->train_id,0);if(t<0)return;for(int i=0;i<r->berth_count;i++){int b=r->berth_ids[i];if(b<0||b>=MAX_BERTHS)continue;pthread_rwlock_wrlock(&s->berths[t][b].lock);if(s->berths[t][b].reserved_by==r->request_id)s->berths[t][b].reserved_by=0;pthread_rwlock_unlock(&s->berths[t][b].lock);} }
BookingResult booking_process(BookingSystem *s,BookingRequest r){pthread_mutex_lock(&s->transaction_lock);BookingResult x=process_locked(s,r);pthread_mutex_unlock(&s->transaction_lock);return x;}
BookingResult booking_commit(BookingSystem *s,Wal *w,BookingRequest r){pthread_mutex_lock(&s->transaction_lock);BookingResult x=process_locked(s,r);if(x.status==REQUEST_CONFIRMED&&wal_append(w,&x)){rollback_locked(s,&x);x.status=REQUEST_REJECTED;strcpy(x.reason,"persistence failure");}pthread_mutex_unlock(&s->transaction_lock);return x;}
BookingResult booking_cancel(BookingSystem *s,Wal *w,uint64_t id){BookingResult x={.request_id=id,.status=REQUEST_REJECTED};if(!id){strcpy(x.reason,"invalid request ID");return x;}pthread_mutex_lock(&s->transaction_lock);for(int t=0;t<MAX_TRAINS;t++)if(s->train_ids[t]){x.train_id=s->train_ids[t];x.berth_count=0;for(int b=0;b<MAX_BERTHS;b++){pthread_rwlock_rdlock(&s->berths[t][b].lock);if(s->berths[t][b].reserved_by==id&&x.berth_count<MAX_REQUEST_BERTHS)x.berth_ids[x.berth_count++]=b;pthread_rwlock_unlock(&s->berths[t][b].lock);}if(x.berth_count)break;}if(!x.berth_count){strcpy(x.reason,"booking not found");pthread_mutex_unlock(&s->transaction_lock);return x;}x.status=REQUEST_CANCELLED;strcpy(x.reason,"cancelled");if(wal_append(w,&x)!=0){x.status=REQUEST_REJECTED;strcpy(x.reason,"persistence failure");pthread_mutex_unlock(&s->transaction_lock);return x;}rollback_locked(s,&x);pthread_mutex_unlock(&s->transaction_lock);return x;}
void booking_rollback(BookingSystem *s,const BookingResult *r){if(r->status!=REQUEST_CONFIRMED)return;pthread_mutex_lock(&s->transaction_lock);rollback_locked(s,r);pthread_mutex_unlock(&s->transaction_lock);}
int booking_query_status(BookingSystem *s,uint64_t id,BookingResult *out){memset(out,0,sizeof*out);out->request_id=id;out->status=REQUEST_REJECTED;if(!id){strcpy(out->reason,"invalid request ID");return -1;}pthread_mutex_lock(&s->transaction_lock);for(int t=0;t<MAX_TRAINS;t++)if(s->train_ids[t])for(int b=0;b<MAX_BERTHS;b++){pthread_rwlock_rdlock(&s->berths[t][b].lock);if(s->berths[t][b].reserved_by==id){out->train_id=s->train_ids[t];out->status=REQUEST_CONFIRMED;if(out->berth_count<MAX_REQUEST_BERTHS)out->berth_ids[out->berth_count++]=b;strcpy(out->reason,"confirmed");}pthread_rwlock_unlock(&s->berths[t][b].lock);}pthread_mutex_unlock(&s->transaction_lock);if(out->status!=REQUEST_CONFIRMED){strcpy(out->reason,"not found or rejected");return -1;}return 0;}
void booking_list_confirmed(BookingSystem *s,FILE *out){pthread_mutex_lock(&s->transaction_lock);fprintf(out,"--- Confirmed Bookings ---\n");for(int t=0;t<MAX_TRAINS;t++)if(s->train_ids[t])for(int b=0;b<MAX_BERTHS;b++){pthread_rwlock_rdlock(&s->berths[t][b].lock);if(s->berths[t][b].reserved_by)fprintf(out,"Train %d berth %d: Request %llu\n",s->train_ids[t],b,(unsigned long long)s->berths[t][b].reserved_by);pthread_rwlock_unlock(&s->berths[t][b].lock);}pthread_mutex_unlock(&s->transaction_lock);}
