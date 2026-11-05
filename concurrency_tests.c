#include "tatkal.h"
#include <assert.h>
#include <string.h>
#include <stdatomic.h>

typedef struct { BookingSystem *booking; int index; BookingResult result; } Job;
static void *reserve(void *arg) {
    Job *j=arg;
    BookingRequest r={.id=(uint64_t)j->index+1,.train_id=12001,.berth_count=1,.preferred_berth=119};
    j->result=booking_process(j->booking,r);
    return NULL;
}
typedef struct { BookingSystem *booking; atomic_int *failures; } Reader;
static void *read_status(void *arg){Reader *r=arg;for(int i=0;i<1000;i++){BookingResult result;if(booking_query_status(r->booking,500,&result)!=0||result.berth_count!=1)atomic_fetch_add(r->failures,1);}return NULL;}
int main(void) {
    BookingSystem b; assert(!booking_init(&b));
    /* Leave exactly one available berth; all workers contend for it. */
    b.train_ids[0]=12001;
    for(int i=0;i<119;i++) b.berths[0][i].reserved_by=999;
    pthread_t threads[32]; Job jobs[32];
    for(int i=0;i<32;i++) {
        jobs[i]=(Job){.booking=&b,.index=i};
        assert(!pthread_create(&threads[i],NULL,reserve,&jobs[i]));
    }
    int confirmed=0;
    for(int i=0;i<32;i++) { assert(!pthread_join(threads[i],NULL)); confirmed+=jobs[i].result.status==REQUEST_CONFIRMED; }
    assert(confirmed==1);
    assert(b.berths[0][119].reserved_by>=1 && b.berths[0][119].reserved_by<=32);
    BookingRequest stable={.id=500,.train_id=12002,.berth_count=1,.preferred_berth=0};assert(booking_process(&b,stable).status==REQUEST_CONFIRMED);
    pthread_t readers[8];atomic_int failures=0;Reader reader={.booking=&b,.failures=&failures};for(int i=0;i<8;i++)assert(!pthread_create(&readers[i],NULL,read_status,&reader));
    for(int i=0;i<20;i++){BookingRequest write={.id=(uint64_t)1000+i,.train_id=12002,.berth_count=1,.preferred_berth=1+i};assert(booking_process(&b,write).status==REQUEST_CONFIRMED);}
    for(int i=0;i<8;i++){assert(!pthread_join(readers[i],NULL));}assert(atomic_load(&failures)==0);
    booking_destroy(&b);
    puts("concurrency: 32 contenders, exactly one confirmation; concurrent readers passed");
    return 0;
}
