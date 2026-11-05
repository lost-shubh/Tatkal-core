# Tatkal-Core — AI Handoff

## Location and platform

Project: `C:\Users\Admin\Tatkal-Core` (WSL path:
`/mnt/c/Users/Admin/Tatkal-Core`). Target: Linux/POSIX C11.

The user wants the **entire original Tatkal-Core synopsis implemented**. Do not
claim completion: the current code is a working partial prototype.

## Verified by the user in WSL

```sh
make
make test
./tatkal-core demo
./tatkal-core workload 1000 4
./tatkal-core recover bookings.wal
./tatkal-core ipc-demo
./tatkal-core ipc-ring-demo
./tatkal-core wal-inspect bookings.wal
./tatkal-core wal-fault-demo
```

Observed working results:

- Build completes with `cc`, `-std=c11`, and `-pthread`.
- `make test` reported `tests: passed`.
- 1,000 generated one-berth requests resulted in 120 confirmations and 880
  expected rejections because `MAX_BERTHS` is 120.
- WAL recovery reported `120 valid records`.
- `ipc-demo` reaped its child.
- `ipc-ring-demo` produced/consumed five requests, with ID sum 15.
- Partial-WAL test reports an ignored incomplete tail followed by one valid
  record.

## Implemented files

- `main.c`: commands and interactive CLI.
- `booking.c`: berth inventory, RW locks, duplicate request-ID tracking, and
  rollback helper.
- `queue.c`: bounded three-priority in-process queue with 200 ms aging.
- `worker_pool.c`: fixed pthread pool; rolls back a tentative allocation when
  WAL append fails.
- `wal.c`: append-only fixed binary records, checksum validation, inspect,
  recovery, and safe temporary partial-write demo.
- `supervisor.c`: fork/reap counter demo plus a shared-memory ring-buffer
  producer/consumer demo using process-shared semaphores.
- `algorithms.c`: deterministic Banker safe-state, WFG cycle+victim rollback,
  fixed Clock example, and fixed C-SCAN example.
- `tests.c`: booking rollback and priority-aging checks.

## Current commands

```text
./tatkal-core cli
./tatkal-core demo
./tatkal-core ipc-demo
./tatkal-core ipc-ring-demo
./tatkal-core workload REQUESTS WORKERS
./tatkal-core recover WAL_FILE
./tatkal-core wal-inspect WAL_FILE
./tatkal-core wal-fault-demo
make test
make smoke
```

## Important correctness details already fixed

1. WAL checksum must use `offsetof(WalRecord, checksum)`, not
   `sizeof(record) - sizeof(checksum)`, because tail padding can otherwise
   include checksum bytes.
2. Partial `fread` can set EOF; recovery reads byte counts and treats any
   nonzero short record as an incomplete trailing record.
3. WAL append failure must roll back the reserved berths before reporting
   rejection; current worker implementation does this.
4. `bookings.wal` is append-only. Delete it before a fresh benchmark if an
   isolated run is needed.

## Not implemented — highest-priority continuation work

1. Integrate the POSIX shared-memory ring into actual booking ingress and
   worker processing. It is currently a standalone demo.
2. Implement local request-file ingress; then add optional TCP + nonblocking
   `epoll` ingress behind an interface.
3. Split algorithms into configurable modules:
   - Banker candidate requests with documented max/allocation/available input
   - WFG graph input, detection trace, victim selection, rollback/retry
   - Clock/LRU with user-provided reference strings and frame count
   - C-SCAN/FCFS with user-provided cylinders, head, and direction
4. Add booking queries protected by reader locks and live CLI status/metrics.
5. Add persistent benchmark output (CSV/JSON) with latency distribution/P95,
   queue waits by priority, completion reconciliation, and configuration.
6. Add robust tests: concurrent same-berth requests, full/empty queue,
   duplicate IDs, malformed input, worker failure, WAL corruption, and
   algorithm known-answer cases.
7. Add graceful signal shutdown and stricter system-call/error handling.

## Honest scope statement

This is a functional educational prototype, **not** a complete implementation
of the full submitted synopsis and not a real IRCTC/railway system. The next
AI should preserve that distinction in code, README, and report language.
