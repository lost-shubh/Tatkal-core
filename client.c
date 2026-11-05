#include "tatkal.h"
#include "ipc.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <mqueue.h>
#include <sys/stat.h>

static int read_int(const char *label, int minimum, int maximum, int *value) {
    char line[80], *end;
    long parsed;
    for (;;) {
        printf("%s", label);
        if (!fgets(line, sizeof line, stdin)) return -1;
        errno = 0;
        parsed = strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t') end++;
        if (errno == 0 && end != line && (*end == '\n' || *end == '\0') && parsed >= minimum && parsed <= maximum) {
            *value = (int)parsed;
            return 0;
        }
        printf("Enter a whole number from %d to %d.\n", minimum, maximum);
    }
}

static uint64_t next_request_id(unsigned int sequence) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 100000ULL + (ts.tv_nsec / 10000U) + sequence;
}

int main(void) {
    const char *ring_name = getenv("TATKAL_RING");
    if (!ring_name) ring_name = "/tatkal_ingress_ring";
    int fd = shm_open(ring_name, O_RDWR, 0600);
    if (fd < 0) {
        perror("Cannot connect to Tatkal-Core server. Start './tatkal-core server 4' first");
        return 1;
    }
    struct stat state;
    if (fstat(fd, &state) || state.st_size != sizeof(SharedRing)) {
        fputs("Incompatible server: restart it after rebuilding.\n", stderr); close(fd); return 1;
    }
    SharedRing *ring = mmap(NULL, sizeof *ring, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ring == MAP_FAILED) {
        perror("Cannot map Tatkal-Core request channel");
        return 1;
    }
    if(ring->magic!=IPC_RING_MAGIC || ring->owner_pid<=0) {
        fputs("Invalid or stale server IPC channel. Restart the server.\n",stderr);
        munmap(ring,sizeof *ring); return 1;
    }

    puts("\n=== Tatkal-Core Customer Reservation Client ===");
    puts("Reservations are submitted to the running server for processing.");
    puts("Priority: 0 = general, 1 = medium, 2 = high.");
    puts("Preferred berth: -1 = automatic allocation, 0-119 = preferred berth.\n");

    for (unsigned int sequence = 1;; sequence++) {
        int choice;
        if (read_int("1) Make reservation  2) Exit\nChoose: ", 1, 2, &choice) != 0 || choice == 2) break;

        int train_id, berth_count, priority, preferred_berth;
        if (read_int("Train number (1-99999): ", 1, 99999, &train_id) ||
            read_int("Number of berths (1-4): ", 1, MAX_REQUEST_BERTHS, &berth_count) ||
            read_int("Priority (0-2): ", PRIORITY_GENERAL, PRIORITY_HIGH, &priority) ||
            read_int("Preferred berth (-1 for automatic, 0-119): ", -1, MAX_BERTHS - 1, &preferred_berth)) break;

        BookingRequest request = {
            .id = next_request_id(sequence),
            .train_id = train_id,
            .berth_count = berth_count,
            .priority = (Priority)priority,
            .preferred_berth = preferred_berth,
            .arrived_ms = 0, /* server timestamps on receipt */
            .wants_reply = 1
        };
        char reply_name[64];
        snprintf(reply_name, sizeof reply_name, "/tatkal_reply_%llu", (unsigned long long)request.id);
        struct mq_attr attr = {.mq_maxmsg=1, .mq_msgsize=sizeof(BookingResult)};
        mqd_t reply = mq_open(reply_name, O_CREAT | O_EXCL | O_RDONLY, 0600, &attr);
        if (reply == (mqd_t)-1) { perror("Cannot create response channel"); continue; }
        if (ipc_ring_push(ring, request) != 0) {
            puts("Reservation could not be submitted.");
            mq_close(reply); mq_unlink(reply_name);
            continue;
        }
        printf("Reservation submitted. Request ID: %" PRIu64 "\n", request.id);
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_sec += 30;
        BookingResult result;
        ssize_t received;
        do { received = mq_timedreceive(reply, (char *)&result, sizeof result, NULL, &deadline); }
        while (received < 0 && errno == EINTR);
        mq_close(reply); mq_unlink(reply_name);
        if (received != sizeof result || result.request_id != request.id) {
            puts("Result not received. Booking outcome is uncertain; retain the request ID and check the server log before retrying.");
        } else if (result.status == REQUEST_CONFIRMED) {
            printf("CONFIRMED. Train %d berths:", request.train_id);
            for (int i=0; i<result.berth_count && i<MAX_REQUEST_BERTHS; i++) printf(" %d", result.berth_ids[i]);
            puts("");
        } else printf("REJECTED: %.48s\n", result.reason);
    }

    munmap(ring, sizeof *ring);
    puts("Client closed.");
    return 0;
}
