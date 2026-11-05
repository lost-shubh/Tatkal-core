#include "tatkal.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

typedef struct { uint32_t magic,length; uint64_t request_id; int32_t status,berth_count,berths[MAX_REQUEST_BERTHS]; uint32_t checksum; } LegacyRecord;
static uint32_t sum(const LegacyRecord *r){const unsigned char *p=(const unsigned char *)r;uint32_t x=0;for(size_t i=0;i<offsetof(LegacyRecord,checksum);i++)x=(x<<5)^(x>>27)^p[i];return x;}

int wal_migrate_legacy(const char *source,const char *destination,FILE *out){
    if(!source||!destination||!strcmp(source,destination)||access(destination,F_OK)==0){fprintf(out,"Migration destination must be a new file: %s\n",destination?destination:"(null)");return -1;}
    FILE *f=fopen(source,"rb");if(!f){fprintf(out,"WAL unavailable: %s\n",source);return -1;}
    uint64_t owners[MAX_BERTHS]={0},ids[MAX_BERTHS];int active=0,conflicts=0;LegacyRecord r;
    while(fread(&r,1,sizeof r,f)==sizeof r){
        if(r.magic!=WAL_MAGIC||r.length!=sizeof r||r.checksum!=sum(&r)||r.status!=REQUEST_CONFIRMED||r.berth_count<1||r.berth_count>MAX_REQUEST_BERTHS){fclose(f);fprintf(out,"Migration stopped: invalid legacy record\n");return -1;}
        int bad=0;for(int i=0;i<active;i++)if(ids[i]==r.request_id)bad=1;for(int i=0;i<r.berth_count;i++)if(r.berths[i]<0||r.berths[i]>=MAX_BERTHS||owners[r.berths[i]])bad=1;
        if(bad){conflicts++;continue;}
        for(int i=0;i<r.berth_count;i++) owners[r.berths[i]]=r.request_id;
        ids[active++]=r.request_id;
    }
    if(ferror(f)){fclose(f);return -1;}if(!feof(f)){fclose(f);fprintf(out,"Migration stopped: incomplete legacy record\n");return -1;}fclose(f);
    Wal w;if(wal_open(&w,destination)){fprintf(out,"Cannot create migration destination: %s\n",destination);return -1;}
    for(int i=0;i<active;i++){BookingResult x={.request_id=ids[i],.train_id=12001,.status=REQUEST_CONFIRMED};for(int b=0;b<MAX_BERTHS;b++)if(owners[b]==ids[i]&&x.berth_count<MAX_REQUEST_BERTHS)x.berth_ids[x.berth_count++]=b;if(wal_append(&w,&x)){wal_close(&w);return -1;}}
    wal_close(&w);fprintf(out,"Migration complete: %d active records written to %s; %d conflicting historical records retained only in %s\n",active,destination,conflicts,source);return 0;
}
