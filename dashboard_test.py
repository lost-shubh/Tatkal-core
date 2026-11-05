#!/usr/bin/env python3
"""Checks terminal dashboard and CSV export against a running server."""
import os
import socket
import subprocess
import tempfile
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
PORT = 5098

with tempfile.TemporaryDirectory() as directory:
    csv_path = os.path.join(directory, "metrics.csv")
    env = os.environ | {"TATKAL_WAL": os.path.join(directory, "dashboard.wal"), "TATKAL_RING": "/tatkal_dashboard_test"}
    server = subprocess.Popen(["./tatkal-core", "server", "2", str(PORT)], cwd=ROOT, env=env,
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.4)
    with socket.create_connection(("127.0.0.1", PORT), timeout=3) as client:
        client.sendall(b"9101,12040,1,2,8\n")
        assert client.recv(256).decode().startswith("CONFIRMED request=9101"), "reservation failed"
    server.stdin.write(f"dashboard\nexport-csv {csv_path}\nshutdown\n")
    server.stdin.flush()
    output, _ = server.communicate(timeout=5)
    assert "Tatkal-Core Live Dashboard" in output, output
    assert f"Metrics CSV exported: {csv_path}" in output, output
    with open(csv_path, encoding="utf-8") as file:
        rows = file.read().splitlines()
    assert len(rows) == 2 and rows[0].startswith("sampled_at_ms,completed") and ",1,1,0," in rows[1], rows

print("dashboard and CSV export test: passed")
