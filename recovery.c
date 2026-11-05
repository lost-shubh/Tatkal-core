#include "tatkal.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

typedef struct { uint32_t magic, version, length; uint64_t request_id; int32_t train_id, status, berth_count, berths[MAX_REQUEST_BERTHS]; uint32_t checksum; } Record;
typedef struct { uint32_t magic, length; uint64_t request_id; int32_t status, berth_count, berths[MAX_REQUEST_BERTHS]; uint32_t checksum; } LegacyRecord;
static uint32_t record_sum(const void *v,size_t n){const unsigned char *p=v;uint32_t x=0;for(size_t i=0;i<n;i++)x=(x<<5)^(x>>27)^p[i];return x;}

int wal_restore(const char *path, BookingSystem *s) {
    FILE *f = fopen(path, "rb+");
    if (!f) return errno == ENOENT ? 0 : -1;
    uint64_t owners[MAX_TRAINS][MAX_BERTHS] = {{0}}, ids[MAX_TRAINS*MAX_BERTHS];
    int train_ids[MAX_TRAINS]={0};
    int count = 0;
    long end = 0;
    for (;;) {
        uint32_t prefix[2];
        size_t n = fread(prefix, 1, sizeof prefix, f);
        if (ferror(f)) goto bad;
        if (!n) break;
        if (n != sizeof prefix) {
            if (ftruncate(fileno(f), end) || fsync(fileno(f))) goto bad;
            break;
        }
        if(fseek(f,-(long)sizeof prefix,SEEK_CUR))goto bad;
        uint64_t request_id; int train_id,status,berth_count,berths[MAX_REQUEST_BERTHS];
        if(prefix[1]==WAL_VERSION){Record r;n=fread(&r,1,sizeof r,f);if(n!=sizeof r){if(ftruncate(fileno(f),end)||fsync(fileno(f)))goto bad;break;}if(r.magic!=WAL_MAGIC||r.version!=WAL_VERSION||r.length!=sizeof r||r.checksum!=record_sum(&r,offsetof(Record,checksum))){goto bad;}request_id=r.request_id;train_id=r.train_id;status=r.status;berth_count=r.berth_count;memcpy(berths,r.berths,sizeof berths);}
        else {LegacyRecord r;n=fread(&r,1,sizeof r,f);if(n!=sizeof r){if(ftruncate(fileno(f),end)||fsync(fileno(f)))goto bad;break;}if(r.magic!=WAL_MAGIC||r.length!=sizeof r||r.checksum!=record_sum(&r,offsetof(LegacyRecord,checksum))){goto bad;}request_id=r.request_id;train_id=12001;status=r.status;berth_count=r.berth_count;memcpy(berths,r.berths,sizeof berths);}
        if(!request_id || train_id<1 || train_id>99999 || (status!=REQUEST_CONFIRMED&&status!=REQUEST_CANCELLED) || berth_count<1 || berth_count>MAX_REQUEST_BERTHS || count==MAX_TRAINS*MAX_BERTHS)goto bad;
        int t=-1;for(int i=0;i<MAX_TRAINS;i++)if(train_ids[i]==train_id){t=i;break;}if(t<0)for(int i=0;i<MAX_TRAINS;i++)if(!train_ids[i]){train_ids[i]=train_id;t=i;break;}if(t<0)goto bad;
        if(status==REQUEST_CONFIRMED)for(int i=0;i<count;i++)if(ids[i]==request_id)goto bad;
        if(status==REQUEST_CONFIRMED){for(int i=0;i<berth_count;i++){int b=berths[i];if(b<0||b>=MAX_BERTHS||owners[t][b])goto bad;owners[t][b]=request_id;}ids[count++]=request_id;}
        else {int found_id=-1;for(int i=0;i<count;i++)if(ids[i]==request_id){found_id=i;break;}if(found_id<0)goto bad;for(int i=0;i<berth_count;i++){int b=berths[i];if(b<0||b>=MAX_BERTHS||owners[t][b]!=request_id)goto bad;owners[t][b]=0;}ids[found_id]=ids[count-1];count--;}
        end=ftell(f);
        if(end<0) goto bad;
    }
    uint64_t *saved = count ? malloc((size_t)count*sizeof(*saved)) : NULL;
    if(count && !saved) goto bad;
    if(count) memcpy(saved,ids,(size_t)count*sizeof(*saved));
    free(s->seen_ids);
    s->seen_ids=saved; s->seen_count=s->seen_cap=(size_t)count;
    memcpy(s->train_ids,train_ids,sizeof train_ids);
    for(int t=0;t<MAX_TRAINS;t++)for(int b=0;b<MAX_BERTHS;b++) s->berths[t][b].reserved_by=owners[t][b];
    fclose(f);
    return count;
bad:
    fclose(f);
    return -1;
}
