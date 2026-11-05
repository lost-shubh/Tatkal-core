#include "tatkal.h"
#include <assert.h>
#include <string.h>

static void test_booking(void) {
    BookingSystem s; assert(booking_init(&s)==0);
    BookingRequest r={.id=1,.train_id=12001,.berth_count=1,.preferred_berth=5};
    BookingResult a=booking_process(&s,r); assert(a.status==REQUEST_CONFIRMED);
    BookingResult status;
    assert(booking_query_status(&s, 0, &status) == -1);
    assert(booking_query_status(&s, 1, &status) == 0);
    assert(status.berth_count == 1 && status.berth_ids[0] == 5);
    BookingResult b=booking_process(&s,r); assert(b.status==REQUEST_REJECTED && !strcmp(b.reason,"duplicate request ID"));
    booking_rollback(&s,&a); assert(s.berths[0][5].reserved_by==0);
    r.id=2; BookingResult c=booking_process(&s,r); assert(c.status==REQUEST_CONFIRMED);
    assert(s.berths[0][5].reserved_by==2); booking_destroy(&s);
}
static void test_train_isolation(void) {
    BookingSystem s; assert(booking_init(&s)==0);
    BookingRequest a={.id=101,.train_id=12001,.berth_count=1,.preferred_berth=9};
    BookingRequest b={.id=102,.train_id=12002,.berth_count=1,.preferred_berth=9};
    assert(booking_process(&s,a).status==REQUEST_CONFIRMED);
    assert(booking_process(&s,b).status==REQUEST_CONFIRMED);
    BookingResult out; assert(!booking_query_status(&s,102,&out));
    assert(out.train_id==12002 && out.berth_count==1 && out.berth_ids[0]==9);
    booking_destroy(&s);
}
static void test_aging(void) {
    TaskQueue q; Metrics m={0}; assert(queue_init(&q)==0); assert(pthread_mutex_init(&m.lock,0)==0);
    BookingRequest old={.id=10,.priority=PRIORITY_GENERAL,.arrived_ms=now_ms()-201};
    BookingRequest high={.id=11,.priority=PRIORITY_HIGH,.arrived_ms=now_ms()};
    BookingRequest out; assert(queue_push(&q,old,&m)==0); assert(queue_push(&q,high,&m)==0); assert(queue_pop(&q,&out,&m)==0); assert(out.id==10); assert(m.aged_promotions==1);
    queue_destroy(&q); pthread_mutex_destroy(&m.lock);
}
int main(void) { test_booking(); test_train_isolation(); test_aging(); puts("tests: passed"); return 0; }
