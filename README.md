# Tatkal-Core

C-based Linux/POSIX reservation workload for demonstrating bounded concurrency,
priority scheduling with aging, safe berth allocation, WAL recovery, Banker's
algorithm, wait-for graph cycle detection, Clock replacement, and C-SCAN.

## Build and run (Linux)

```sh
make
./tatkal-core demo
./tatkal-core cli
./tatkal-core ipc-demo
./tatkal-core ipc-ring-demo
./tatkal-core workload 1000 4
./tatkal-core recover bookings.wal
./tatkal-core wal-inspect bookings.wal
./tatkal-core wal-fault-demo
make test
```

## TCP/epoll network ingress

The server also runs a non-blocking TCP listener managed by Linux `epoll`.
It binds to loopback (`127.0.0.1`) by default. Set `TATKAL_BIND=0.0.0.0`
only when remote clients should be allowed. Port `5050` is the default; provide
a final port argument to change it:

```sh
TATKAL_WAL=bookings.migrated.wal ./tatkal-core server 4 5050
./tatkal-net-client 127.0.0.1 5050 9001 12034 2 1:-1
```

The TCP protocol is one newline-terminated CSV request per connection:
`request_id,train_number,berth_count,priority,preferred_berth`. The server
returns one line, either `CONFIRMED ...` with berth numbers or `REJECTED ...`.
Malformed input and a full bounded worker queue receive explicit rejections.
The same TCP port accepts `STATUS,request_id` and `CANCEL,request_id` control
requests. The helper client supports `status HOST PORT ID` and
`cancel HOST PORT ID` modes.
Incomplete TCP requests expire after 10 seconds, preventing idle clients from
occupying bounded ingress slots indefinitely.

## Make a customer reservation (two WSL terminals)

Terminal 1 starts the server and creates its shared-memory request channel:

```sh
./tatkal-core server 4
```

Terminal 2 opens the interactive client. It asks for a train number, berth
count, priority, and optional preferred berth:

```sh
./tatkal-client
```

Do not double-click `tatkal-client`; it is a Linux terminal program. The
client submits a request and shows its ID, then waits up to 30 seconds for the
confirmed berth numbers or rejection reason. A timeout means the outcome is
uncertain; retain the ID and inspect the server WAL before retrying.
The server processes requests through the worker pool and durably writes
confirmed bookings to `bookings.wal` before returning confirmation.

After rebuilding, stop and restart the server before connecting clients.
Server startup restores inventory from the WAL, repairs an incomplete trailing
record, and refuses duplicate or conflicting records. Preserve any rejected WAL
for inspection; old benchmark runs may have appended conflicting events.
Run `python3 integration_test.py` in WSL for an isolated reservation/restart test.
`TATKAL_RING` selects a separate IPC channel for tests; server and client must
use the same value. `make clean` preserves reservation history.

While the server is running, type `queue-stats`, `metrics`, or `show-bookings`
in its terminal to inspect live state. `memory-stats` compares Clock, FIFO,
and LRU page replacement; `arm-trace` compares FCFS, SCAN, LOOK, and C-SCAN;
`deadlock-test` runs a labelled deterministic simulation. `shutdown` drains the
worker queue and exits. Commands are checked at least once per idle second.
Latency P95 uses nearest-rank over the first 10,000 completions. `metrics`
also reports per-priority queue wait times and aging promotions. Aging promotes
the oldest queue head that has waited at least 200 ms; otherwise high, medium,
then general priority is served.

Type `dashboard` for a compact live terminal dashboard. Type `export-csv FILE`
to append a timestamped metrics snapshot to a CSV file, suitable for Excel or
LibreOffice charts. The CSV includes completion counts, confirmation/rejection
counts, queue depth by priority, latency statistics, WAL failures, aging, and
per-priority waiting times.

To cancel an active reservation from the server terminal, type `cancel
REQUEST_ID`. Cancellation is appended to the WAL before berths are released,
and replay applies the cancellation after restart.

Use `status REQUEST_ID` to query an active booking. Request IDs remain
idempotent after cancellation and cannot be reused after restart.

Use `availability TRAIN` in the server terminal to view free/occupied berths
by coach. The model maps 120 berths into three coaches of 40 berths each.

Run `fault-test` to exercise bounded-queue saturation and partial-WAL recovery
without modifying customer data.

## Train catalog database

Initialize and load the SQLite catalog:

```sh
python3 train_database.py init trains.db
python3 train_database.py import trains.db trains.csv
python3 train_database.py search trains.db Rajdhani
```

`trains.csv` is a small seed for demonstrations, not a claim to contain every
current Indian Railways service. For a full current timetable, use the
`railpull` NTES crawler, then import its exported `data/out/trains.csv`:

```sh
git clone https://github.com/shwetankg07/railpull.git
cd railpull
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
python ntes/crawl.py
python transform/export.py
cd ../Tatkal-Core
python3 train_database.py import-railpull trains.db ../railpull/data/out/trains.csv
```

The crawl is resumable and may take hours. Respect NTES rate limits and terms;
the project does not silently download or redistribute a third-party bulk
snapshot. The official Railway Board timetable is the authoritative reference
for the applicable timetable period.

Clean and normalize the full crawler output into the project database:

```sh
python3 railway_dataset.py ../railpull/data/out railway.db
```

This creates normalized `trains`, `stations`, and `train_stops` tables,
rejects invalid rows, trims names, normalizes station codes and times, removes
duplicate train/stop keys, and prints cleaning counters.

Use `ingress FILE` in the server terminal to load local CSV requests. Each
non-comment line is `request_id,train_number,berth_count,priority,preferred_berth`.
Malformed lines are rejected. The system supports up to 16 distinct trains per
run, with 120 berths per train. The same berth number may be confirmed once on
each different train.

WAL version 2 persists train identifiers. Existing pre-version-2 WAL records
are read as legacy records for train 12001, so earlier local history remains
recoverable. New records are version 2. If an old log contains conflicting
historical confirmations, preserve it and create a clean active chart with:

```sh
./tatkal-core migrate-legacy bookings.wal bookings.migrated.wal
TATKAL_WAL=bookings.migrated.wal ./tatkal-core server 4
```

Migration never overwrites either file. It keeps the earliest non-conflicting
booking for each berth and reports the number of historical conflicts.

`demo` runs deterministic demonstrations. `workload N WORKERS` submits N
generated requests to a bounded worker pool. Successful bookings are appended
to `bookings.wal`, or to the file selected by `TATKAL_WAL`. This is an educational user-space simulation; it does not
control the Linux scheduler, VM, or storage device.

## Current implementation boundary

The runnable implementation provides bounded threaded reservation processing, a
WAL, concurrency-safe berth state, three logical priority queues with aging,
Clock/FIFO/LRU/FCFS/SCAN/LOOK/C-SCAN/Banker/WFG demonstrations, and recovery validation. `ipc-demo`
demonstrates a supervisor reaping a child that updates POSIX shared memory
under process-shared semaphores. Local text-file ingress is implemented;
network/epoll ingress is implemented on the configured TCP port.
Read-only status, availability, and chart queries use POSIX reader locks and
can run concurrently while booking/cancellation updates take the writer path.

`ipc-ring-demo` is the bounded shared-memory queue demonstration: a child
process produces five booking requests and its supervisor consumes them using
process-shared `mutex`, `slots`, and `items` semaphores.

`wal-fault-demo` uses a temporary file under `/tmp`, writes one valid event,
then appends only three bytes of another record. Recovery must report one valid
record and an ignored incomplete tail.

The interactive CLI accepts `demo`, `ipc`, `ring`, `workload N WORKERS`,
`recover FILE`, `inspect FILE`, `wal-fault`, and `shutdown`; server-only
operations include `availability`, `status`, `cancel`, and `export-csv`.

## Customer reservation portal

After importing `railway.db`, start the server in one WSL terminal:

```sh
./tatkal-core server 4 5050
```

Then run the database-backed portal in another:

```sh
python3 tatkal_portal.py --db railway.db --port 5050
```

The portal searches the timetable by train number or station, displays route,
train type, running days, stop count, and live seats remaining, then collects
passenger name, age, gender, quota, and seat count. Berth numbers are never
chosen by the customer: the booking engine allocates them atomically and
returns the assigned berths and a local PNR record in `passengers.csv`.
Selecting a train also displays every intermediate station with sequence,
station code, arrival/departure, halt minutes, running day, and distance from
the cleaned `train_stops` table.
