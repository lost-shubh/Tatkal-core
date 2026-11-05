#include "tatkal.h"
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/* Replay is performed before workers start. Validate the entire log before
   publishing any recovered state; never skip interior corruption. */

typedef struct { uint32_t magic, version, length; uint64_t request_id; int32_t train_id, status, berth_count; int32_t berths[MAX_REQUEST_BERTHS]; uint32_t checksum; } WalRecord;
typedef struct { uint32_t magic, length; uint64_t request_id; int32_t status, berth_count; int32_t berths[MAX_REQUEST_BERTHS]; uint32_t checksum; } LegacyWalRecord;
/* offsetof is required: sizeof(record)-sizeof(checksum) may include checksum
   bytes when the compiler adds tail padding to the record. */
static uint32_t checksum(const WalRecord *r) { const unsigned char *p=(const unsigned char *)r; uint32_t x=0; for(size_t i=0;i<offsetof(WalRecord, checksum);i++) x=(x<<5)^(x>>27)^p[i]; return x; }
static uint32_t legacy_checksum(const LegacyWalRecord *r) { const unsigned char *p=(const unsigned char *)r; uint32_t x=0; for(size_t i=0;i<offsetof(LegacyWalRecord, checksum);i++) x=(x<<5)^(x>>27)^p[i]; return x; }
int wal_open(Wal *w,const char *path){memset(w,0,sizeof(*w));w->file=fopen(path,"ab+");return !w->file||pthread_mutex_init(&w->lock,0)?-1:0;}
void wal_close(Wal *w){if(w->file)fclose(w->file);pthread_mutex_destroy(&w->lock);}
int wal_append(Wal *w,const BookingResult *r){
    WalRecord x={.magic=WAL_MAGIC,.version=WAL_VERSION,.length=sizeof x,.request_id=r->request_id,.train_id=r->train_id,.status=r->status,.berth_count=r->berth_count};
    memcpy(x.berths,r->berth_ids,sizeof x.berths);x.checksum=checksum(&x);
    pthread_mutex_lock(&w->lock);
    int ok=fwrite(&x,sizeof x,1,w->file)==1 && fflush(w->file)==0 && fsync(fileno(w->file))==0;
    pthread_mutex_unlock(&w->lock);return ok?0:-1;
}
static int scan(const char *path, FILE *out, int verbose) {
    FILE *f=fopen(path,"rb"); if(!f){fprintf(out,"WAL unavailable: %s\n",path);return -1;}
    int valid=0;
    for(;;) {
        uint32_t prefix[2]; size_t bytes=fread(prefix,1,sizeof prefix,f);
        if(!bytes) break;
        if(bytes!=sizeof prefix){fprintf(out,"WAL has incomplete trailing record; ignored\n");break;}
        if(fseek(f,-(long)sizeof prefix,SEEK_CUR)){fclose(f);return -1;}
        uint64_t id; int train,status,count; int berths[MAX_REQUEST_BERTHS]; int legacy=prefix[1]!=WAL_VERSION;
        if(legacy) { LegacyWalRecord x; bytes=fread(&x,1,sizeof x,f); if(bytes!=sizeof x){fprintf(out,"WAL has incomplete trailing record; ignored\n");break;} if(x.magic!=WAL_MAGIC||x.length!=sizeof x||x.checksum!=legacy_checksum(&x)){fprintf(out,"WAL invalid record after %d valid records\n",valid);fclose(f);return -1;} id=x.request_id;train=12001;status=x.status;count=x.berth_count;memcpy(berths,x.berths,sizeof berths); }
        else { WalRecord x; bytes=fread(&x,1,sizeof x,f); if(bytes!=sizeof x){fprintf(out,"WAL has incomplete trailing record; ignored\n");break;} if(x.magic!=WAL_MAGIC||x.version!=WAL_VERSION||x.length!=sizeof x||x.checksum!=checksum(&x)){fprintf(out,"WAL invalid record after %d valid records\n",valid);fclose(f);return -1;} id=x.request_id;train=x.train_id;status=x.status;count=x.berth_count;memcpy(berths,x.berths,sizeof berths); }
        if(train<1||train>99999||count<1||count>MAX_REQUEST_BERTHS){fprintf(out,"WAL invalid record after %d valid records\n",valid);fclose(f);return -1;}
        if(verbose){const char *label=status==REQUEST_CONFIRMED?"CONFIRMED":status==REQUEST_CANCELLED?"CANCELLED":"REJECTED";fprintf(out,"train=%d request=%llu status=%s berths=",train,(unsigned long long)id,label);for(int i=0;i<count;i++)fprintf(out,"%s%d",i?",":"",berths[i]);fputc('\n',out);} valid++;
    }
    if(ferror(f)){fprintf(out,"WAL read error after %d valid records\n",valid);fclose(f);return -1;} fprintf(out,"WAL recovery: %d valid records\n",valid);fclose(f);return valid;
}
int wal_recover(const char *path,FILE *out){return scan(path,out,0);}
int wal_inspect(const char *path,FILE *out){return scan(path,out,1);}
int wal_fault_demo(FILE *out) { const char *path="/tmp/tatkal-core-wal-fault-demo.wal"; remove(path); Wal w; BookingResult r={.request_id=99,.train_id=12001,.status=REQUEST_CONFIRMED,.berth_count=1,.berth_ids={7}}; if(wal_open(&w,path)||wal_append(&w,&r)){fprintf(out,"WAL fault demo setup failed\n");return -1;}wal_close(&w); FILE *f=fopen(path,"ab"); if(!f){return -1;} unsigned char partial[3]={0x54,0x43,0x4f}; fwrite(partial,1,sizeof partial,f);fclose(f);fprintf(out,"WAL fault demo (partial write): ");int result=wal_recover(path,out);remove(path);return result==1?0:-1;}
