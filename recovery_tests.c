#include "tatkal.h"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char path[]="/tmp/tatkal-recovery-XXXXXX";
    int fd=mkstemp(path); assert(fd>=0); close(fd);
    BookingSystem b; assert(!booking_init(&b));
    Wal w; assert(!wal_open(&w,path));
    BookingResult r={.request_id=123,.train_id=12001,.status=REQUEST_CONFIRMED,.berth_count=2,.berth_ids={3,4}};
    assert(!wal_append(&w,&r)); wal_close(&w);
    FILE *f=fopen(path,"ab"); assert(f); assert(fwrite("xyz",1,3,f)==3); fclose(f);
    assert(wal_restore(path,&b)==1);
    BookingResult out; assert(!booking_query_status(&b,123,&out));
    assert(out.berth_count==2 && out.berth_ids[0]==3 && out.berth_ids[1]==4);
    assert(wal_restore(path,&b)==1);
    assert(!wal_open(&w,path)); assert(!wal_append(&w,&r)); wal_close(&w);
    assert(wal_restore(path,&b)==-1); /* duplicate/conflicting committed event */
    assert(!booking_query_status(&b,123,&out)); /* failed replay publishes nothing */
    booking_destroy(&b); unlink(path);

    char cancel_path[]="/tmp/tatkal-cancel-XXXXXX";
    fd=mkstemp(cancel_path); assert(fd>=0); close(fd);
    assert(!booking_init(&b)); assert(!wal_open(&w,cancel_path));
    BookingRequest request={.id=777,.train_id=12002,.berth_count=1,.preferred_berth=8};
    BookingResult confirmed=booking_commit(&b,&w,request); assert(confirmed.status==REQUEST_CONFIRMED);
    BookingResult cancelled=booking_cancel(&b,&w,777); assert(cancelled.status==REQUEST_CANCELLED);
    wal_close(&w); booking_destroy(&b);
    assert(!booking_init(&b)); assert(wal_restore(cancel_path,&b)==0);
    assert(booking_query_status(&b,777,&out)==-1); booking_destroy(&b); unlink(cancel_path);
    puts("recovery tests: passed (restart, partial-tail repair, duplicate rejection, cancellation replay)");
    return 0;
}
