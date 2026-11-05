#include "tatkal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int algo_banker_request(int available[], int allocation[][3], int maximum[][3], int num_p, int num_r, int process, const int request[]) {
    if(num_p<1 || num_p>1000 || num_r<1 || num_r>3 || process<0 || process>=num_p) return 0;
    for(int r=0;r<num_r;r++)
        if(request[r]<0 || request[r]>available[r] || request[r]>maximum[process][r]-allocation[process][r]) return 0;
    for(int r=0;r<num_r;r++) { available[r]-=request[r]; allocation[process][r]+=request[r]; }
    if(algo_banker_safe(available,allocation,maximum,num_p,num_r)) return 1;
    for(int r=0;r<num_r;r++) { available[r]+=request[r]; allocation[process][r]-=request[r]; }
    return 0;
}

int algo_banker_safe(int available[], int allocation[][3], int maximum[][3], int num_p, int num_r) {
    if(num_p<1 || num_p>1000 || num_r<1 || num_r>3) return 0;
    for(int r=0;r<num_r;r++) {
        if(available[r]<0) return 0;
        for(int p=0;p<num_p;p++)
            if(allocation[p][r]<0 || maximum[p][r]<allocation[p][r]) return 0;
    }
    int work[3], finish[num_p];
    memcpy(work, available, sizeof(int) * num_r);
    memset(finish, 0, sizeof(finish));

    int count = 0;
    while (count < num_p) {
        int found = 0;
        for (int p = 0; p < num_p; p++) {
            if (!finish[p]) {
                int ok = 1;
                for (int r = 0; r < num_r; r++) {
                    if (maximum[p][r] - allocation[p][r] > work[r]) {
                        ok = 0; break;
                    }
                }
                if (ok) {
                    for (int r = 0; r < num_r; r++) work[r] += allocation[p][r];
                    finish[p] = 1;
                    count++;
                    found = 1;
                }
            }
        }
        if (!found) return 0;
    }
    return 1;
}

int algo_wfg_cycle(int g[4][4], int *victim) {
    int reachable[4][4];
    memcpy(reachable, g, sizeof reachable);
    for(int k=0;k<4;k++) for(int i=0;i<4;i++) for(int j=0;j<4;j++)
        reachable[i][j] = reachable[i][j] || (reachable[i][k] && reachable[k][j]);
    *victim = -1;
    for(int i=3;i>=0;i--) if(reachable[i][i]) { *victim=i; return 1; }
    return 0;
}

void algo_clock_demo(FILE *o, int frames_count, int refs[], int refs_count) {
    if(frames_count<1 || frames_count>10000 || refs_count<0) { fputs("Invalid Clock input\n",o); return; }
    int *frames = malloc(sizeof(int) * frames_count);
    int *bits = calloc(frames_count, sizeof(int));
    if(!frames || !bits) { free(frames); free(bits); fputs("Clock allocation failed\n",o); return; }
    for(int i=0; i<frames_count; i++) frames[i] = -1;
    int hand = 0, faults = 0;

    for (int k = 0; k < refs_count; k++) {
        int hit = -1;
        for (int i = 0; i < frames_count; i++) if (frames[i] == refs[k]) hit = i;
        if (hit >= 0) bits[hit] = 1;
        else {
            faults++;
            while (frames[hand] != -1 && bits[hand]) {
                bits[hand] = 0;
                hand = (hand + 1) % frames_count;
            }
            frames[hand] = refs[k];
            bits[hand] = 1;
            hand = (hand + 1) % frames_count;
        }
    }
    fprintf(o, "Clock: refs=%d faults=%d hits=%d frames=[", refs_count, faults,refs_count-faults);
    for(int i=0;i<frames_count;i++) fprintf(o,"%s%d",i?",":"",frames[i]);
    fputs("]\n",o);
    free(frames); free(bits);
}

static void print_frames(FILE *o,const char *name,int refs_count,int faults,const int *frames,int count){
    fprintf(o,"%s: refs=%d faults=%d hits=%d frames=[",name,refs_count,faults,refs_count-faults);
    for(int i=0;i<count;i++)fprintf(o,"%s%d",i?",":"",frames[i]);
    fputs("]\n",o);
}

void algo_fifo_demo(FILE *o,int frames_count,int refs[],int refs_count){
    if(frames_count<1||frames_count>10000||refs_count<0){fputs("Invalid FIFO input\n",o);return;}
    int *frames=malloc(sizeof *frames*(size_t)frames_count);if(!frames){fputs("FIFO allocation failed\n",o);return;}for(int i=0;i<frames_count;i++)frames[i]=-1;
    int hand=0,faults=0;for(int k=0;k<refs_count;k++){int hit=0;for(int i=0;i<frames_count;i++)if(frames[i]==refs[k])hit=1;if(!hit){frames[hand]=refs[k];hand=(hand+1)%frames_count;faults++;}}
    print_frames(o,"FIFO",refs_count,faults,frames,frames_count);free(frames);
}

void algo_lru_demo(FILE *o,int frames_count,int refs[],int refs_count){
    if(frames_count<1||frames_count>10000||refs_count<0){fputs("Invalid LRU input\n",o);return;}
    int *frames=malloc(sizeof *frames*(size_t)frames_count),*used=calloc((size_t)frames_count,sizeof *used);if(!frames||!used){free(frames);free(used);fputs("LRU allocation failed\n",o);return;}for(int i=0;i<frames_count;i++)frames[i]=-1;
    int tick=0,faults=0;for(int k=0;k<refs_count;k++){int slot=-1;for(int i=0;i<frames_count;i++)if(frames[i]==refs[k])slot=i;if(slot<0){faults++;slot=0;for(int i=0;i<frames_count;i++)if(frames[i]<0){slot=i;break;}else if(used[i]<used[slot])slot=i;frames[slot]=refs[k];}used[slot]=++tick;}
    print_frames(o,"LRU",refs_count,faults,frames,frames_count);free(frames);free(used);
}

void algo_memory_compare(FILE *o,int frames_count,int refs[],int refs_count){
    fprintf(o,"Memory comparison: %d frames, %d references\n",frames_count,refs_count);
    algo_clock_demo(o,frames_count,refs,refs_count);algo_fifo_demo(o,frames_count,refs,refs_count);algo_lru_demo(o,frames_count,refs,refs_count);
}

void algo_cscan_demo(FILE *o, int r[], int r_count, int head) {
    if(r_count<0 || r_count>10000 || head<0 || head>199) { fputs("Invalid C-SCAN input\n",o); return; }
    if(!r_count) { fputs("C-SCAN: no requests, movement=0\n",o); return; }
    for(int i=0;i<r_count;i++) if(r[i]<0 || r[i]>199) { fputs("Invalid cylinder\n",o); return; }
    int sorted[r_count];
    memcpy(sorted, r, sizeof(int) * r_count);
    for(int i=0; i<r_count-1; i++) {
        for(int j=0; j<r_count-i-1; j++) {
            if(sorted[j] > sorted[j+1]) {
                int tmp = sorted[j]; sorted[j] = sorted[j+1]; sorted[j+1] = tmp;
            }
        }
    }

    int movement = 0;
    long long waiting=0;
    fputs("Service order:",o);
    int last = head;
    for (int i = 0; i < r_count; i++) {
        if (sorted[i] >= head) {
            movement += sorted[i] - last;
            last = sorted[i];
            waiting+=movement; fprintf(o," %d",sorted[i]);
        }
    }
    movement += (199 - last) + 199;
    last = 0;
    for (int i = 0; i < r_count; i++) {
        if (sorted[i] < head) {
            movement += sorted[i] - last;
            last = sorted[i];
            waiting+=movement; fprintf(o," %d",sorted[i]);
        }
    }
    fprintf(o, "\nC-SCAN: start=%d total movement=%d mean_wait=%.2f cylinder-time units (1 per cylinder; wrap included)\n", head, movement,(double)waiting/r_count);
}

static int disk_valid(FILE *o,int r[],int count,int head){if(count<0||count>10000||head<0||head>199){fputs("Invalid disk input\n",o);return 0;}for(int i=0;i<count;i++)if(r[i]<0||r[i]>199){fputs("Invalid cylinder\n",o);return 0;}return 1;}
static void disk_sort(int *out,int r[],int count){memcpy(out,r,sizeof(int)*(size_t)count);for(int i=0;i<count-1;i++)for(int j=0;j<count-i-1;j++)if(out[j]>out[j+1]){int t=out[j];out[j]=out[j+1];out[j+1]=t;}}
void algo_fcfs_demo(FILE *o,int r[],int count,int head){
    if(!disk_valid(o,r,count,head))return;
    if(!count){fputs("FCFS: no requests, movement=0\n",o);return;}
    int movement=0,last=head;long long waiting=0;
    for(int i=0;i<count;i++){movement+=abs(r[i]-last);last=r[i];waiting+=movement;}
    fprintf(o,"FCFS order:");for(int i=0;i<count;i++)fprintf(o," %d",r[i]);
    fprintf(o,"\nFCFS: start=%d total movement=%d mean_wait=%.2f cylinder-time units\n",head,movement,(double)waiting/count);
}

static void elevator_demo(FILE *o,const char *name,int r[],int count,int head,int go_to_end){
    if(!disk_valid(o,r,count,head))return;
    if(!count){fprintf(o,"%s: no requests, movement=0\n",name);return;}
    int *sorted=malloc(sizeof *sorted*(size_t)count),*order=malloc(sizeof *order*(size_t)count);
    if(!sorted||!order){free(sorted);free(order);fputs("Disk allocation failed\n",o);return;}
    disk_sort(sorted,r,count);int n=0,last=head,movement=0;long long waiting=0;
    for(int i=0;i<count;i++)if(sorted[i]>=head){movement+=sorted[i]-last;last=sorted[i];waiting+=movement;order[n++]=sorted[i];}
    int below=0;for(int i=0;i<count;i++)if(sorted[i]<head)below=1;
    if(below&&go_to_end){movement+=199-last;last=199;}
    for(int i=count-1;i>=0;i--)if(sorted[i]<head){movement+=last-sorted[i];last=sorted[i];waiting+=movement;order[n++]=sorted[i];}
    fprintf(o,"%s order:",name);for(int i=0;i<n;i++)fprintf(o," %d",order[i]);
    fprintf(o,"\n%s: start=%d total movement=%d mean_wait=%.2f cylinder-time units\n",name,head,movement,(double)waiting/count);free(sorted);free(order);
}

void algo_scan_demo(FILE *o,int r[],int count,int head){elevator_demo(o,"SCAN",r,count,head,1);}
void algo_look_demo(FILE *o,int r[],int count,int head){elevator_demo(o,"LOOK",r,count,head,0);}
void algo_disk_compare(FILE *o,int r[],int count,int head){fputs("Disk scheduling comparison: FCFS, SCAN, LOOK, C-SCAN\n",o);algo_fcfs_demo(o,r,count,head);algo_scan_demo(o,r,count,head);algo_look_demo(o,r,count,head);algo_cscan_demo(o,r,count,head);}

void run_algorithm_demos(FILE *out) {
    int avail[3] = {3, 3, 2};
    int alloc[5][3] = {{0, 1, 0}, {2, 0, 0}, {3, 0, 2}, {2, 1, 1}, {0, 0, 2}};
    int max[5][3] = {{7, 5, 3}, {3, 2, 2}, {9, 0, 2}, {2, 2, 2}, {4, 3, 3}};
    fprintf(out, "Banker safe state: %s\n", algo_banker_safe(avail, alloc, max, 5, 3) ? "yes" : "no");

    int waits[4][4] = {{0, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}};
    int victim = -1;
    fprintf(out, "Wait-for graph cycle: %s\n", algo_wfg_cycle(waits, &victim) ? "detected" : "none");
    if (victim != -1) {
        int held[4]={1,1,1,0}, tentative[4]={10,20,30,0};
        held[victim]=0; tentative[victim]=0;
        for(int j=0;j<4;j++) waits[victim][j]=waits[j][victim]=0;
        int remaining;
        fprintf(out, "Recovery: victim=%d held=%d tentative=%d retry=pending cycle_after=%s\n",
                victim, held[victim], tentative[victim],
                algo_wfg_cycle(waits,&remaining)?"present":"none");
    }

    int refs[] = {1, 2, 3, 2, 4, 1, 5, 2};
    algo_memory_compare(out, 3, refs, 8);

    int cylinders[] = {98, 183, 37, 122, 14, 124, 65, 67};
    algo_disk_compare(out, cylinders, 8, 53);
}
