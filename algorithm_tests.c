#include "tatkal.h"
#include <assert.h>
#include <string.h>

int main(void) {
    int avail[3]={0,0,0}, alloc[2][3]={{1,0,0},{0,1,0}}, max[2][3]={{1,1,0},{1,1,0}};
    assert(!algo_banker_safe(avail,alloc,max,2,3));
    avail[0]=1; assert(algo_banker_safe(avail,alloc,max,2,3));
    assert(!algo_banker_safe(avail,alloc,max,2,4));
    int resources[3]={1,0,0}, assigned[2][3]={{1,0,0},{0,0,0}}, demands[2][3]={{2,0,0},{2,0,0}};
    int request[3]={1,0,0};
    assert(!algo_banker_request(resources,assigned,demands,2,3,1,request));
    assert(resources[0]==1 && assigned[1][0]==0);
    assert(algo_banker_request(resources,assigned,demands,2,3,0,request));
    assert(resources[0]==0 && assigned[0][0]==2);
    int g[4][4]={{0,1,0,0},{0,0,1,0},{0,1,0,0},{0,0,0,0}}, victim;
    assert(algo_wfg_cycle(g,&victim) && victim==2);
    g[2][1]=0; assert(!algo_wfg_cycle(g,&victim) && victim==-1);
    FILE *f=tmpfile(); assert(f);
    int refs[]={1,2,3,2,4,1,5,2}, disk[]={98,183,37,122,14,124,65,67};
    algo_memory_compare(f,3,refs,8); algo_disk_compare(f,disk,8,53);
    rewind(f); char output[2048]={0};
    size_t bytes=fread(output,1,sizeof output-1,f); assert(bytes>0 && !ferror(f)); fclose(f);
    assert(strstr(output,"faults=7 hits=1 frames=[2,1,5]"));
    assert(strstr(output,"FIFO: refs=8 faults=7 hits=1 frames=[2,1,5]"));
    assert(strstr(output,"LRU: refs=8 faults=7 hits=1 frames=[2,5,1]"));
    assert(strstr(output,"FCFS: start=53 total movement=640"));
    assert(strstr(output,"SCAN: start=53 total movement=331"));
    assert(strstr(output,"LOOK: start=53 total movement=299"));
    assert(strstr(output,"Service order: 65 67 98 122 124 183 14 37"));
    assert(strstr(output,"total movement=382"));
    puts("algorithm tests: passed");
    return 0;
}
