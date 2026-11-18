# Tatkal-Core Synopsis Completion Checklist

This checklist maps the project synopsis objectives to executable evidence.
Tatkal-Core is a Linux/POSIX educational prototype: CPU scheduling, memory
replacement, deadlock, and disk scheduling are deliberately user-space
simulations, not modifications to the Linux kernel or physical devices.

| Synopsis requirement | Implementation and evidence |
| --- | --- |
| Concurrent ticket-reservation engine | `booking.c`, `tests.c`, `concurrency_tests.c` |
| Process creation and supervision | `supervisor.c`, `./tatkal-core ipc-demo` |
| POSIX shared-memory IPC | `ipc.c`, `./tatkal-core ipc-ring-demo` |
| Fixed-size POSIX worker pool | `worker_pool.c`, `workload N WORKERS` |
| Priority queues and aging | `queue.c`, queue metrics, `tests.c` |
| Mutex/semaphore/readers-writers synchronization | `booking.c`, `ipc.c`, `concurrency_tests.c` |
| Banker's safe-state checking | `algorithms.c`, `algorithm_tests.c` |
| Wait-for-graph detection and recovery | `algorithms.c`, `algorithm_tests.c`, `demo` |
| Clock page replacement | `algorithms.c`, `algorithm_tests.c`, `memory-stats` |
| C-SCAN disk scheduling | `algorithms.c`, `algorithm_tests.c`, `arm-trace` |
| WAL and restart recovery | `wal.c`, `recovery.c`, `recovery_tests.c`, `wal-fault-demo` |
| Throughput/latency/queue measurements | `metrics.c`, `benchmark.py`, `dashboard`, `export-csv` |
| Local and TCP/epoll ingress | `client.c`, `ingress.c`, `network_test.py` |
| CLI monitoring and fault handling | `main.c`, `fault_injection.c`, `dashboard_test.py` |

Run the complete synopsis verification in WSL:

```sh
make synopsis-check
```

It rebuilds the application, runs unit/integration/network/database tests,
runs the IPC/WAL demonstrations, and executes the repeatable 100/1,000/10,000
request benchmark. The benchmark uses temporary directories and does not alter
your active `bookings.wal`.
