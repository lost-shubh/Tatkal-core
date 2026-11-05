#include "tatkal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int compare_uint64(const void *a, const void *b) {
    uint64_t arg1 = *(const uint64_t *)a;
    uint64_t arg2 = *(const uint64_t *)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

static void snapshot(Metrics *m,TaskQueue *q,MetricsSnapshot *s){
    memset(s,0,sizeof *s);s->sampled_at_ms=now_ms();
    pthread_mutex_lock(&m->lock);
    s->submitted=m->submitted;s->confirmed=m->confirmed;s->rejected=m->rejected;s->wal_failures=m->wal_failures;s->queue_high_water=m->queue_high_water;s->response_total_ms=m->response_total_ms;s->response_max_ms=m->response_max_ms;s->aged_promotions=m->aged_promotions;s->latency_samples=m->response_count;
    memcpy(s->queue_wait_total_ms,m->queue_wait_total_ms,sizeof s->queue_wait_total_ms);memcpy(s->queue_wait_max_ms,m->queue_wait_max_ms,sizeof s->queue_wait_max_ms);memcpy(s->dispatched,m->dispatched,sizeof s->dispatched);
    uint64_t times[10000];if(s->latency_samples)memcpy(times,m->response_times,sizeof(uint64_t)*(size_t)s->latency_samples);
    pthread_mutex_unlock(&m->lock);
    if(s->latency_samples){qsort(times,(size_t)s->latency_samples,sizeof(uint64_t),compare_uint64);s->p95_ms=times[(95*s->latency_samples+99)/100-1];}
    if(q){pthread_mutex_lock(&q->lock);s->queue_total=q->total;memcpy(s->queue_count,q->count,sizeof s->queue_count);pthread_mutex_unlock(&q->lock);}
}

void print_metrics_summary(Metrics *m, FILE *out) {
    pthread_mutex_lock(&m->lock);
    uint64_t sub = m->submitted;
    uint64_t conf = m->confirmed;
    uint64_t rej = m->rejected;
    uint64_t walf = m->wal_failures;
    uint64_t total_ms = m->response_total_ms;
    uint64_t max_ms = m->response_max_ms;
    uint64_t aged=m->aged_promotions, waits[3], wait_max[3], dispatched[3];
    memcpy(waits,m->queue_wait_total_ms,sizeof waits); memcpy(wait_max,m->queue_wait_max_ms,sizeof wait_max); memcpy(dispatched,m->dispatched,sizeof dispatched);
    int count = m->response_count;
    uint64_t times[10000];
    if(count>0) memcpy(times, m->response_times, sizeof(uint64_t) * count);
    pthread_mutex_unlock(&m->lock);

    double avg = sub ? (double)total_ms / sub : 0;

    uint64_t p95 = 0;
    if (count > 0) {
        qsort(times, count, sizeof(uint64_t), compare_uint64);
        p95 = times[(95 * count + 99) / 100 - 1];
    }

    fprintf(out, "\n--- System Metrics ---\n");
    fprintf(out, "Total Completed: %llu\n", (unsigned long long)sub);
    fprintf(out, "Confirmed:       %llu\n", (unsigned long long)conf);
    fprintf(out, "Rejected:        %llu\n", (unsigned long long)rej);
    fprintf(out, "WAL Failures:    %llu\n", (unsigned long long)walf);
    fprintf(out, "Avg Latency:     %.2f ms\n", avg);
    fprintf(out, "Max Latency:     %llu ms\n", (unsigned long long)max_ms);
    fprintf(out, "P95 Latency:     %llu ms\n", (unsigned long long)p95);
    fprintf(out, "Aged promotions: %llu\n",(unsigned long long)aged);
    for(int p=0;p<3;p++)fprintf(out,"Queue %s: dispatched=%llu avg_wait=%.2f ms max_wait=%llu ms\n",p==0?"general":p==1?"medium":"high",(unsigned long long)dispatched[p],dispatched[p]?(double)waits[p]/dispatched[p]:0.0,(unsigned long long)wait_max[p]);

    fprintf(out, "Latency samples: %d (first 10000 completions maximum)\n", count);
}

void print_dashboard(Metrics *m,TaskQueue *q,FILE *out){
    MetricsSnapshot s;snapshot(m,q,&s);double avg=s.submitted?(double)s.response_total_ms/s.submitted:0.0;
    fprintf(out,"\n+---------------- Tatkal-Core Live Dashboard ----------------+\n");
    fprintf(out,"| Completed %8llu | Confirmed %8llu | Rejected %8llu |\n",(unsigned long long)s.submitted,(unsigned long long)s.confirmed,(unsigned long long)s.rejected);
    fprintf(out,"| Queue total %5d/%-3d | G:%3d M:%3d H:%3d | high-water %5llu |\n",s.queue_total,QUEUE_CAPACITY,s.queue_count[0],s.queue_count[1],s.queue_count[2],(unsigned long long)s.queue_high_water);
    fprintf(out,"| Latency avg %8.2f ms | p95 %6llu ms | max %6llu ms |\n",avg,(unsigned long long)s.p95_ms,(unsigned long long)s.response_max_ms);
    fprintf(out,"| WAL failures %6llu | aging promotions %8llu          |\n",(unsigned long long)s.wal_failures,(unsigned long long)s.aged_promotions);
    fprintf(out,"+-----------------------------------------------------------+\n");
    for(int p=0;p<3;p++)fprintf(out,"  %-7s dispatched=%llu avg_wait=%.2f ms max_wait=%llu ms\n",p==0?"general":p==1?"medium":"high",(unsigned long long)s.dispatched[p],s.dispatched[p]?(double)s.queue_wait_total_ms[p]/s.dispatched[p]:0.0,(unsigned long long)s.queue_wait_max_ms[p]);
}

int metrics_export_csv(Metrics *m,TaskQueue *q,const char *path,FILE *out){
    if(!path||!*path)return -1;
    MetricsSnapshot s;snapshot(m,q,&s);
    FILE *file=fopen(path,"a+");if(!file){perror("metrics export");return -1;}
    if(fseek(file,0,SEEK_END)!=0){fclose(file);return -1;}
    if(ftell(file)==0)fputs("sampled_at_ms,completed,confirmed,rejected,wal_failures,queue_total,queue_general,queue_medium,queue_high,queue_high_water,avg_latency_ms,p95_latency_ms,max_latency_ms,aged_promotions,general_avg_wait_ms,medium_avg_wait_ms,high_avg_wait_ms\n",file);
    double avg=s.submitted?(double)s.response_total_ms/s.submitted:0.0;
    fprintf(file,"%llu,%llu,%llu,%llu,%llu,%d,%d,%d,%d,%llu,%.2f,%llu,%llu,%llu,%.2f,%.2f,%.2f\n",(unsigned long long)s.sampled_at_ms,(unsigned long long)s.submitted,(unsigned long long)s.confirmed,(unsigned long long)s.rejected,(unsigned long long)s.wal_failures,s.queue_total,s.queue_count[0],s.queue_count[1],s.queue_count[2],(unsigned long long)s.queue_high_water,avg,(unsigned long long)s.p95_ms,(unsigned long long)s.response_max_ms,(unsigned long long)s.aged_promotions,s.dispatched[0]?(double)s.queue_wait_total_ms[0]/s.dispatched[0]:0.0,s.dispatched[1]?(double)s.queue_wait_total_ms[1]/s.dispatched[1]:0.0,s.dispatched[2]?(double)s.queue_wait_total_ms[2]/s.dispatched[2]:0.0);
    int flush_rc=fflush(file),close_rc=fclose(file);
    if(!flush_rc&&!close_rc)fprintf(out,"Metrics CSV exported: %s\n",path);
    return flush_rc||close_rc?-1:0;
}
