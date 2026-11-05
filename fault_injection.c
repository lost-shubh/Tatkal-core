#include "tatkal.h"
#include <stdint.h>
#include <stdio.h>

int run_fault_injection_demo(FILE *out){
    TaskQueue q; Metrics m={0}; int accepted=0;
    if(queue_init(&q)||pthread_mutex_init(&m.lock,0)){fputs("Fault test setup failed\n",out);return -1;}
    for(int i=0;i<QUEUE_CAPACITY;i++){BookingRequest r={.id=(uint64_t)i+1,.train_id=12001,.berth_count=1,.priority=PRIORITY_GENERAL,.preferred_berth=i%MAX_BERTHS,.arrived_ms=now_ms()};if(queue_try_push(&q,r,&m)==0)accepted++;}
    BookingRequest overflow={.id=999999,.train_id=12001,.berth_count=1,.priority=PRIORITY_HIGH,.preferred_berth=0,.arrived_ms=now_ms()};
    int rejected=queue_try_push(&q,overflow,&m)!=0;
    queue_destroy(&q);pthread_mutex_destroy(&m.lock);
    fprintf(out,"Fault queue saturation: accepted=%d capacity=%d overflow_rejected=%s\n",accepted,QUEUE_CAPACITY,rejected?"yes":"no");
    if(accepted!=QUEUE_CAPACITY||!rejected)return -1;
    fprintf(out,"Fault WAL partial-tail: ");if(wal_fault_demo(out)!=0)return -1;
    fputs("Fault injection: recovery behavior verified\n",out);return 0;
}
