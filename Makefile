CC ?= gcc
CFLAGS ?= -D_XOPEN_SOURCE=700 -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread
LDFLAGS ?= -pthread -lrt
SRC = main.c booking.c queue.c worker_pool.c wal.c recovery.c migrate.c algorithms.c fault_injection.c supervisor.c ipc.c metrics.c ingress.c
OBJ = $(SRC:.c=.o)

all: tatkal-core tatkal-client tatkal-net-client

portal:
	python3 -m py_compile tatkal_portal.py

refresh-timetable:
	python3 refresh_timetable.py

$(OBJ) client.o: tatkal.h
ipc.o client.o main.o supervisor.o: ipc.h

tatkal-core: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

tatkal-client: client.o ipc.o
	$(CC) client.o ipc.o -o $@ $(LDFLAGS)

tatkal-net-client: net_client.o
	$(CC) net_client.o -o $@ $(LDFLAGS)

test: all
	$(CC) $(CFLAGS) tests.c booking.c queue.c wal.c -o tatkal-tests $(LDFLAGS)
	./tatkal-tests
	$(CC) $(CFLAGS) recovery_tests.c booking.c wal.c recovery.c -o tatkal-recovery-tests $(LDFLAGS)
	./tatkal-recovery-tests
	$(CC) $(CFLAGS) algorithm_tests.c algorithms.c -o tatkal-algorithm-tests $(LDFLAGS)
	./tatkal-algorithm-tests
	$(CC) $(CFLAGS) concurrency_tests.c booking.c wal.c -o tatkal-concurrency-tests $(LDFLAGS)
	./tatkal-concurrency-tests
	python3 network_test.py
	python3 dashboard_test.py
	python3 train_database_test.py
	python3 railway_dataset_test.py

smoke: tatkal-core
	./tatkal-core demo
	./tatkal-core ipc-demo
	./tatkal-core ipc-ring-demo
	./tatkal-core wal-fault-demo

clean:
	rm -f $(OBJ) *.o tatkal-core tatkal-client tatkal-net-client tatkal-tests tatkal-recovery-tests tatkal-algorithm-tests tatkal-concurrency-tests

.PHONY: all clean test smoke portal refresh-timetable
