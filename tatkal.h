#ifndef TATKAL_H
#define TATKAL_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_BERTHS 120
#define MAX_TRAINS 16
#define MAX_REQUEST_BERTHS 4
#define QUEUE_CAPACITY 256
#define WAL_MAGIC 0x54434f52u
#define WAL_VERSION 2u

typedef enum { PRIORITY_GENERAL, PRIORITY_MEDIUM, PRIORITY_HIGH } Priority;
typedef enum { REQUEST_PENDING, REQUEST_CONFIRMED, REQUEST_REJECTED } RequestStatus;

typedef struct {
    uint64_t id;
    int train_id;
    int berth_count;
    Priority priority;
    int preferred_berth;
    uint64_t arrived_ms;
    int wants_reply;
    int reply_fd; /* TCP client socket when wants_reply is 2 */
} BookingRequest;

typedef struct {
    uint64_t request_id;
    int train_id;
    RequestStatus status;
    int berth_ids[MAX_REQUEST_BERTHS];
    int berth_count;
    char reason[48];
} BookingResult;

typedef struct {
    uint64_t reserved_by;
    pthread_rwlock_t lock;
} Berth;

typedef struct {
    Berth berths[MAX_TRAINS][MAX_BERTHS];
    int train_ids[MAX_TRAINS];
    pthread_mutex_t train_lock;
    pthread_mutex_t transaction_lock;
    pthread_mutex_t seen_lock;
    uint64_t *seen_ids;
    size_t seen_count, seen_cap;
} BookingSystem;

typedef struct { BookingRequest items[3][QUEUE_CAPACITY]; int head[3], tail[3], count[3], total; pthread_mutex_t lock; pthread_cond_t not_empty, not_full; int stopping; } TaskQueue;
typedef struct { FILE *file; pthread_mutex_t lock; } Wal;

typedef struct {
    uint64_t submitted, confirmed, rejected, wal_failures, queue_high_water, aged_promotions;
    uint64_t response_total_ms, response_max_ms;
    uint64_t queue_wait_total_ms[3], queue_wait_max_ms[3], dispatched[3];
    uint64_t response_times[10000]; // Fixed buffer for latency tracking (P95)
    int response_count;
    pthread_mutex_t lock;
} Metrics;

typedef struct {
    uint64_t sampled_at_ms, submitted, confirmed, rejected, wal_failures;
    uint64_t queue_high_water, response_total_ms, response_max_ms, p95_ms, aged_promotions;
    uint64_t queue_wait_total_ms[3], queue_wait_max_ms[3], dispatched[3];
    int latency_samples, queue_total, queue_count[3];
} MetricsSnapshot;

uint64_t now_ms(void);
int booking_init(BookingSystem *s);
void booking_destroy(BookingSystem *s);
BookingResult booking_process(BookingSystem *s, BookingRequest r);
BookingResult booking_commit(BookingSystem *s, Wal *w, BookingRequest r);
void booking_rollback(BookingSystem *s, const BookingResult *r);

/* New: Booking Queries */
int booking_query_status(BookingSystem *s, uint64_t request_id, BookingResult *out);
void booking_list_confirmed(BookingSystem *s, FILE *out);

int queue_init(TaskQueue *q);
void queue_destroy(TaskQueue *q);
int queue_push(TaskQueue *q, BookingRequest r, Metrics *m);
int queue_try_push(TaskQueue *q, BookingRequest r, Metrics *m);
int queue_pop(TaskQueue *q, BookingRequest *r, Metrics *m);
void queue_stop(TaskQueue *q);
int wal_open(Wal *w, const char *path);
void wal_close(Wal *w);
int wal_append(Wal *w, const BookingResult *r);
int wal_recover(const char *path, FILE *out);
int wal_restore(const char *path, BookingSystem *s);
int wal_inspect(const char *path, FILE *out);
int wal_fault_demo(FILE *out);
int wal_migrate_legacy(const char *source, const char *destination, FILE *out);

/* Modular Algorithms */
int algo_banker_safe(int available[], int allocation[][3], int maximum[][3], int num_p, int num_r);
int algo_banker_request(int available[], int allocation[][3], int maximum[][3], int num_p, int num_r, int process, const int request[]);
int algo_wfg_cycle(int g[4][4], int *victim);
void algo_clock_demo(FILE *o, int frames_count, int refs[], int refs_count);
void algo_fifo_demo(FILE *o, int frames_count, int refs[], int refs_count);
void algo_lru_demo(FILE *o, int frames_count, int refs[], int refs_count);
void algo_memory_compare(FILE *o, int frames_count, int refs[], int refs_count);
void algo_cscan_demo(FILE *o, int r[], int r_count, int head);
void algo_fcfs_demo(FILE *o, int r[], int r_count, int head);
void algo_scan_demo(FILE *o, int r[], int r_count, int head);
void algo_look_demo(FILE *o, int r[], int r_count, int head);
void algo_disk_compare(FILE *o, int r[], int r_count, int head);

void run_algorithm_demos(FILE *out);
int run_ipc_demo(FILE *out);
int run_ipc_queue_demo(FILE *out);

/* Ingress */
int ingress_load_file(const char *path, TaskQueue *q, Metrics *m);
typedef struct TcpIngress TcpIngress;
int ingress_tcp_start(int port, TcpIngress **out);
int ingress_tcp_poll(TcpIngress *tcp, TaskQueue *q, Metrics *m, int timeout_ms);
void ingress_tcp_stop(TcpIngress *tcp);

/* Metrics */
void print_metrics_summary(Metrics *m, FILE *out);
void print_dashboard(Metrics *m, TaskQueue *q, FILE *out);
int metrics_export_csv(Metrics *m, TaskQueue *q, const char *path, FILE *out);

#endif
