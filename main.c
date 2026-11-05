#include "tatkal.h"
#include "ipc.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <time.h>

static void monitor_command(char *line, BookingSystem *b, TaskQueue *q, Metrics *m, Wal *w) {
    line[strcspn(line,"\r\n")]=0;
    if (!strcmp(line,"queue-stats")) {
        pthread_mutex_lock(&q->lock);
        printf("Queue: total=%d general=%d medium=%d high=%d capacity=%d\n",
               q->total,q->count[0],q->count[1],q->count[2],QUEUE_CAPACITY);
        pthread_mutex_unlock(&q->lock);
    } else if (!strcmp(line,"metrics")) print_metrics_summary(m,stdout);
    else if (!strcmp(line,"dashboard")) print_dashboard(m,q,stdout);
    else if (!strncmp(line,"availability ",13)) { int train; if(sscanf(line+13,"%d",&train)==1)booking_print_availability(b,train,stdout);else puts("usage: availability TRAIN_NUMBER"); }
    else if (!strncmp(line,"status ",7)) { unsigned long long id; BookingResult x; if(sscanf(line+7,"%llu",&id)==1&&booking_query_status(b,(uint64_t)id,&x)==0){printf("Status: CONFIRMED request=%llu train=%d berths=",id,x.train_id);for(int i=0;i<x.berth_count;i++)printf("%s%d",i?",":"",x.berth_ids[i]);puts("");}else if(sscanf(line+7,"%llu",&id)==1)printf("Status: not active request=%llu\n",id);else puts("usage: status REQUEST_ID"); }
    else if (!strncmp(line,"cancel ",7)) { unsigned long long id; if(sscanf(line+7,"%llu",&id)==1){BookingResult x=booking_cancel(b,w,(uint64_t)id);printf("Cancellation %s: request=%llu reason=%s\n",x.status==REQUEST_CANCELLED?"confirmed":"rejected",id,x.reason);}else puts("usage: cancel REQUEST_ID"); }
    else if (!strncmp(line,"export-csv ",11) && line[11]) metrics_export_csv(m,q,line+11,stdout);
    else if (!strcmp(line,"show-bookings")) booking_list_confirmed(b,stdout);
    else if (!strcmp(line,"memory-stats")) {
        int refs[]={1,2,3,2,4,1,5,2};
        puts("Simulation: 4 KB frames; Clock, FIFO, and LRU comparison");
        algo_memory_compare(stdout,3,refs,8);
    } else if (!strcmp(line,"arm-trace")) {
        int cylinders[]={98,183,37,122,14,124,65,67};
        puts("Simulation: virtual cylinders 0-199; FCFS, SCAN, LOOK, C-SCAN");
        algo_disk_compare(stdout,cylinders,8,53);
    } else if (!strcmp(line,"deadlock-test")) run_algorithm_demos(stdout);
    else if (!strcmp(line,"fault-test")) run_fault_injection_demo(stdout);
    else if (!strncmp(line,"ingress ",8) && line[8]) ingress_load_file(line+8,q,m);
    else if (*line) puts("Commands: dashboard | availability TRAIN | status REQUEST_ID | export-csv FILE | cancel REQUEST_ID | queue-stats | metrics | memory-stats | arm-trace | deadlock-test | fault-test | ingress FILE | shutdown");
    fflush(stdout);
}

typedef struct Pool Pool;
extern Pool *pool_start(int,TaskQueue *,BookingSystem *,Wal *,Metrics *);
extern void pool_join(Pool *);

static volatile sig_atomic_t keep_running = 1;
static void handle_sigint(int sig) { (void)sig; keep_running = 0; }

static void usage(const char *p){printf("Usage: %s cli | demo | ipc-demo | ipc-ring-demo | workload REQUESTS WORKERS | recover WAL | wal-inspect WAL | migrate-legacy SOURCE DESTINATION | wal-fault-demo | server WORKERS [TCP_PORT]\n",p);}

static int server_mode(int workers,int tcp_port) {
    if (workers < 1 || workers > 128) { fputs("workers must be 1-128\n", stderr); return 1; }
    BookingSystem b; TaskQueue q; Wal w; Metrics m = {0};
    const char *wal_path=getenv("TATKAL_WAL"); if(!wal_path) wal_path="bookings.wal";
    if(booking_init(&b)||queue_init(&q)||wal_open(&w,wal_path)||pthread_mutex_init(&m.lock,0)){
        fprintf(stderr,"initialization failed\n"); return 1;
    }

    int recovered = wal_restore(wal_path, &b);
    if (recovered < 0) {
        fprintf(stderr,"WAL recovery failed. Preserve %s; for an overlapping legacy log run: ./tatkal-core migrate-legacy %s %s.migrated\n",wal_path,wal_path,wal_path);
        wal_close(&w); queue_destroy(&q); booking_destroy(&b); pthread_mutex_destroy(&m.lock);
        return 1;
    }
    printf("Restored %d confirmed bookings.\n", recovered);
    Pool *p = pool_start(workers, &q, &b, &w, &m);
    if(!p){ fprintf(stderr,"worker startup failed\n"); return 1; }

    const char *ring_name = getenv("TATKAL_RING");
    if (!ring_name) ring_name = "/tatkal_ingress_ring";
    SharedRing *ring = NULL;
    if(ipc_ring_init(ring_name, &ring) != 0) {
        fprintf(stderr, "Failed to init IPC ring. Is another server running?\n");
        pool_join(p); return 1;
    }
    TcpIngress *tcp=NULL;
    if(ingress_tcp_start(tcp_port,&tcp)!=0){perror("Failed to start TCP/epoll ingress");ipc_ring_cleanup(ring_name,ring);pool_join(p);return 1;}

    printf("Tatkal-Core Server started. IPC ring=%s; TCP/epoll=0.0.0.0:%d. Press Ctrl+C to stop.\n", ring_name,tcp_port);
    puts("TCP protocol: one newline-terminated CSV request: request_id,train,berths,priority,preferred_berth");
    puts("Commands: dashboard | availability TRAIN | status REQUEST_ID | export-csv FILE | cancel REQUEST_ID | queue-stats | metrics | memory-stats | arm-trace | deadlock-test | fault-test | ingress FILE | shutdown");
    fflush(stdout);
    char command[256]; size_t command_len=0; int input_open=1, overflow=0;

    while(keep_running) {
        struct pollfd input={.fd=STDIN_FILENO,.events=POLLIN};
        while(input_open && poll(&input,1,0)>0 && (input.revents&(POLLIN|POLLHUP))) {
            char c;
            ssize_t n=read(STDIN_FILENO,&c,1);
            if(n<=0) { input_open=0; break; }
            if(c=='\n') {
                command[command_len]=0;
                if(overflow) puts("Command too long");
                else if(!strcmp(command,"shutdown")) keep_running=0;
                else monitor_command(command,&b,&q,&m,&w);
                command_len=0; overflow=0;
            } else if(command_len<sizeof command-1) command[command_len++]=c;
            else overflow=1;
        }
        if(!keep_running) break;
        if(ingress_tcp_poll(tcp,&q,&m,&b,&w,50)!=0) perror("TCP ingress error");
        BookingRequest r;
        if(ipc_ring_pop(ring, &r) == 0) {
            r.arrived_ms = now_ms();
            r.reply_fd = -1;
            queue_push(&q, r, &m);
        }
    }

    printf("\nShutting down server...\n");
    ingress_tcp_stop(tcp);
    pool_join(p);
    print_metrics_summary(&m, stdout);
    ipc_ring_cleanup(ring_name, ring);
    wal_close(&w); queue_destroy(&q); booking_destroy(&b); pthread_mutex_destroy(&m.lock);
    return 0;
}

static int workload(int n,int workers){if(n<1||workers<1||workers>128)return 1;const char *wal_path=getenv("TATKAL_WAL");if(!wal_path)wal_path="bookings.wal";BookingSystem b;TaskQueue q;Wal w;Metrics m={0};if(booking_init(&b)||queue_init(&q)||wal_open(&w,wal_path)||pthread_mutex_init(&m.lock,0)){fprintf(stderr,"initialization failed\n");return 1;}if(wal_restore(wal_path,&b)<0){fprintf(stderr,"WAL recovery failed: %s\n",wal_path);return 1;}Pool *p=pool_start(workers,&q,&b,&w,&m);if(!p){fprintf(stderr,"worker startup failed\n");return 1;}struct timespec stamp;clock_gettime(CLOCK_REALTIME,&stamp);uint64_t started=now_ms(),base=(uint64_t)stamp.tv_sec*1000000000ULL+(uint64_t)stamp.tv_nsec;for(int i=0;i<n;i++){BookingRequest r={.id=base+(uint64_t)i,.train_id=12001+(i%4),.berth_count=1,.priority=(Priority)(i%3),.preferred_berth=i%MAX_BERTHS,.arrived_ms=now_ms()};queue_push(&q,r,&m);}pool_join(p);uint64_t elapsed=now_ms()-started;double average=m.submitted?(double)m.response_total_ms/m.submitted:0.0;double throughput=elapsed?(1000.0*m.submitted/elapsed):0.0;printf("completed=%llu confirmed=%llu rejected=%llu wal_failures=%llu queue_high_water=%llu elapsed_ms=%llu throughput_rps=%.2f avg_response_ms=%.2f max_response_ms=%llu\n",(unsigned long long)m.submitted,(unsigned long long)m.confirmed,(unsigned long long)m.rejected,(unsigned long long)m.wal_failures,(unsigned long long)m.queue_high_water,(unsigned long long)elapsed,throughput,average,(unsigned long long)m.response_max_ms);wal_close(&w);queue_destroy(&q);booking_destroy(&b);pthread_mutex_destroy(&m.lock);return 0;}

static int cli(void){char line[160],path[120];int n,w;puts("Tatkal-Core CLI: demo | ipc | ring | workload N W | recover FILE | inspect FILE | wal-fault | ingress FILE | quit");while(fputs("tatkal> ",stdout),fflush(stdout),fgets(line,sizeof line,stdin)){if(!strncmp(line,"quit",4)||!strncmp(line,"shutdown",8))return 0;if(!strncmp(line,"demo",4))run_algorithm_demos(stdout);else if(!strncmp(line,"ipc",3)&&strncmp(line,"ring",4))run_ipc_demo(stdout);else if(!strncmp(line,"ring",4))run_ipc_queue_demo(stdout);else if(sscanf(line,"workload %d %d",&n,&w)==2)workload(n,w);else if(!strncmp(line,"workload",8))puts("usage: workload REQUESTS WORKERS");else if(sscanf(line,"recover %119s",path)==1)wal_recover(path,stdout);else if(!strncmp(line,"recover",7))puts("usage: recover WAL_FILE");else if(sscanf(line,"inspect %119s",path)==1)wal_inspect(path,stdout);else if(!strncmp(line,"inspect",7))puts("usage: inspect WAL_FILE");else if(!strncmp(line,"wal-fault",9))wal_fault_demo(stdout);else if(sscanf(line,"ingress %119s",path)==1){printf("ingress not available in CLI mode (requires server running)\n");}else puts("unknown command");}return 0;}

int main(int argc,char **argv){
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    if(argc<2){usage(argv[0]);return 1;}
    if(!strcmp(argv[1],"cli"))return cli();
    if(!strcmp(argv[1],"demo")){run_algorithm_demos(stdout);return 0;}
    if(!strcmp(argv[1],"ipc-demo"))return run_ipc_demo(stdout)!=0;
    if(!strcmp(argv[1],"ipc-ring-demo"))return run_ipc_queue_demo(stdout)!=0;
    if(!strcmp(argv[1],"recover")&&argc==3)return wal_recover(argv[2],stdout)<0;
    if(!strcmp(argv[1],"wal-inspect")&&argc==3)return wal_inspect(argv[2],stdout)<0;
    if(!strcmp(argv[1],"migrate-legacy")&&argc==4)return wal_migrate_legacy(argv[2],argv[3],stdout)!=0;
    if(!strcmp(argv[1],"wal-fault-demo"))return wal_fault_demo(stdout)!=0;
    if(!strcmp(argv[1],"fault-test"))return run_fault_injection_demo(stdout)!=0;
    if(!strcmp(argv[1],"workload")&&argc==4)return workload(atoi(argv[2]),atoi(argv[3]));
    if(!strcmp(argv[1],"server")&&(argc==3||argc==4))return server_mode(atoi(argv[2]),argc==4?atoi(argv[3]):5050);
    usage(argv[0]);return 1;
}
