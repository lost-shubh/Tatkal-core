#!/usr/bin/env python3
"""Isolated TCP/epoll ingress regression test."""
import os
import socket
import subprocess
import tempfile
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
PORT = 5097

with tempfile.TemporaryDirectory() as directory:
    env = os.environ | {"TATKAL_WAL": os.path.join(directory, "network.wal"), "TATKAL_RING": "/tatkal_network_test"}
    server = subprocess.Popen(["./tatkal-core", "server", "2", str(PORT)], cwd=ROOT, env=env,
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.4)
    with socket.create_connection(("127.0.0.1", PORT), timeout=3) as client:
        client.sendall(b"9001,12034,2,1,10\n")
        response = client.recv(256).decode()
    assert response.startswith("CONFIRMED request=9001 train=12034 berths=10,11"), response
    with socket.create_connection(("127.0.0.1", PORT), timeout=3) as client:
        client.sendall(b"STATUS,9001\n")
        status = client.recv(256).decode()
    assert status.startswith("STATUS CONFIRMED request=9001"), status
    with socket.create_connection(("127.0.0.1", PORT), timeout=3) as client:
        client.sendall(b"CANCEL,9001\n")
        cancelled = client.recv(256).decode()
    assert cancelled == "CANCELLED\n", cancelled
    with socket.create_connection(("127.0.0.1", PORT), timeout=3) as client:
        client.sendall(b"bad request\n")
        invalid = client.recv(256).decode()
    assert invalid.startswith("REJECTED reason=invalid format"), invalid
    server.stdin.write("shutdown\n")
    server.stdin.flush()
    output, _ = server.communicate(timeout=5)
    assert "TCP/epoll=" in output and "Confirmed:       1" in output and "Rejected:        1" in output, output

print("network TCP/epoll test: passed")
