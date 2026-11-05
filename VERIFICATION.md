# Verification status

Executed in WSL2 on 2026-10-27, Linux 6.6.114.1, 8 reported CPUs.

## Passing checks

- `make test`: booking/aging, recovery, algorithms, concurrent contention.
- `python3 integration_test.py`: customer receives berths 34,35,36;
  live queue/metrics/chart commands; command shutdown; restart restoration.
- `python3 network_test.py`: TCP/epoll reservation, status, cancellation, and
  malformed-input paths are verified.
- `python3 dashboard_test.py`: the live terminal dashboard reports actual
  work and a CSV metrics snapshot is generated with its header and data row.
- Cancellation replay test: a confirmed booking is cancelled durably, then
  restart replay leaves its berths available while preserving request-ID
  idempotency.
- Availability test coverage reports free/occupied capacity per train coach.
- Fault-injection test confirms queue overflow rejection and partial-WAL
  recovery behavior without modifying customer data.
- TCP ingress defaults to loopback; bind-address configuration is explicit for
  deployments that intentionally allow remote clients.
- TCP idle-client timeout is enforced for incomplete requests.
- Reader-writer stress test passes with eight concurrent status readers during
  exclusive booking updates.
- `python3 train_database_test.py`: SQLite schema, CSV import, name search, and
  route lookup pass using the project seed catalog.
- `python3 benchmark.py`: three runs each of 100, 1000, and 10000 requests,
  four workers, burst producer with bounded queue backpressure. Temporary
  directories isolate all benchmark logs from customer reservations.
- For 10000 requests: 480 confirmed, 9520 rejected, zero WAL failures,
  maximum queue depth 256. Recorded durations were 199, 201, and 202 ms.
- 32 threads competing for the last free berth: exactly one confirmation.
- Banker's candidate request: unsafe allocation is rolled back, safe request
  is granted. WFG victim belongs to a cycle; the highest numbered participant
  is chosen. Clock, FIFO, LRU, FCFS, SCAN, LOOK, and C-SCAN results match
  fixed expected examples.
- Legacy WAL migration was run on a copy and on the bundled historical log:
  120 active records were written to `bookings.migrated.wal`; 103 conflicting
  historical records remain untouched in `bookings.wal`.

## Measurement limits

Timing uses milliseconds. Short runs may report zero duration; their reported
zero throughput is a resolution artifact, not a measured performance result.
Benchmarks predominantly reject requests after inventory fills. They do not
measure 10000 successful commits or production readiness. Latency statistics
retain only the first 10000 completion samples.

## Completion boundary

Train-separated inventory, atomic booking/WAL visibility, stale shared-memory
ring cleanup, per-priority waiting metrics, CSV ingress, and versioned WAL
compatibility are implemented. Legacy logs with historical berth conflicts are
migrated non-destructively into a new active WAL; strict recovery deliberately
refuses the ambiguous original. The application is a Linux/POSIX educational
prototype as declared in the synopsis. Network/epoll ingress is implemented
alongside the local client/server workflow.

The direct `workload` command uses the current directory's `bookings.wal`
unless `TATKAL_WAL` is set. Run it in a new results directory, as
`benchmark.py` does, to preserve a live customer log.
