#!/usr/bin/env python3
"""SQLite train catalog for Tatkal-Core.

CSV columns: train_number,train_name,source_station,destination_station,train_type,runs_on
The importer is intentionally authoritative-data agnostic: supply a current
Indian Railways/CRIS export rather than silently treating a stale web snapshot
as complete.
"""
import csv
import sqlite3
import sys
from pathlib import Path

SCHEMA = """
CREATE TABLE IF NOT EXISTS trains (
  train_number INTEGER PRIMARY KEY CHECK(train_number BETWEEN 1 AND 99999),
  train_name TEXT NOT NULL,
  source_station TEXT NOT NULL,
  destination_station TEXT NOT NULL,
  train_type TEXT NOT NULL DEFAULT 'UNKNOWN',
  runs_on TEXT NOT NULL DEFAULT 'ALL',
  active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1)),
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_trains_name ON trains(train_name);
CREATE INDEX IF NOT EXISTS idx_trains_route ON trains(source_station,destination_station);
"""

SEED = [
    (12001, "Bhopal Shatabdi", "New Delhi", "Rani Kamlapati", "SHATABDI", "ALL"),
    (12002, "Bhopal Shatabdi", "Rani Kamlapati", "New Delhi", "SHATABDI", "ALL"),
    (12034, "New Delhi - Kanpur Shatabdi", "New Delhi", "Kanpur Central", "SHATABDI", "ALL"),
    (12301, "Kolkata Rajdhani", "Howrah", "New Delhi", "RAJDHANI", "ALL"),
    (12302, "Kolkata Rajdhani", "New Delhi", "Howrah", "RAJDHANI", "ALL"),
    (12951, "Mumbai Rajdhani", "Mumbai Central", "New Delhi", "RAJDHANI", "ALL"),
    (12952, "Mumbai Rajdhani", "New Delhi", "Mumbai Central", "RAJDHANI", "ALL"),
    (12626, "Kerala Express", "New Delhi", "Thiruvananthapuram", "EXPRESS", "ALL"),
    (12627, "Karnataka Express", "New Delhi", "KSR Bengaluru", "EXPRESS", "ALL"),
    (12953, "August Kranti Rajdhani", "Mumbai Central", "New Delhi", "RAJDHANI", "ALL"),
]

def connect(path):
    db = sqlite3.connect(path)
    db.executescript(SCHEMA)
    return db

def seed(db):
    db.executemany("""INSERT INTO trains(train_number,train_name,source_station,destination_station,train_type,runs_on)
                     VALUES(?,?,?,?,?,?) ON CONFLICT(train_number) DO UPDATE SET
                     train_name=excluded.train_name,source_station=excluded.source_station,
                     destination_station=excluded.destination_station,train_type=excluded.train_type,
                     runs_on=excluded.runs_on,updated_at=CURRENT_TIMESTAMP""", SEED)
    db.commit()

def import_csv(db, filename):
    with open(filename, newline="", encoding="utf-8-sig") as stream:
        rows = csv.DictReader(stream)
        fields = set(rows.fieldnames or set())
        required = {"train_number","source_station","destination_station","train_type"}
        if not required.issubset(fields) or not ({"train_name","name"}&fields) or not ({"runs_on","runs_days"}&fields):
            raise ValueError("CSV must contain train_number, name/train_name, source_station, destination_station, train_type, runs_days/runs_on")
        count = 0
        for row in rows:
            name_key="train_name" if "train_name" in fields else "name"
            days_key="runs_on" if "runs_on" in fields else "runs_days"
            values = tuple(row[key].strip() for key in ("train_number",name_key,"source_station","destination_station","train_type",days_key))
            db.execute("""INSERT INTO trains(train_number,train_name,source_station,destination_station,train_type,runs_on)
                          VALUES(?,?,?,?,?,?) ON CONFLICT(train_number) DO UPDATE SET
                          train_name=excluded.train_name,source_station=excluded.source_station,
                          destination_station=excluded.destination_station,train_type=excluded.train_type,
                          runs_on=excluded.runs_on,updated_at=CURRENT_TIMESTAMP""", values)
            count += 1
    db.commit()
    return count

def main(argv):
    if len(argv) < 3:
        print("Usage: train_database.py init DB | seed DB | import DB CSV | search DB TEXT | route DB SOURCE DEST")
        return 1
    command, path = argv[1], Path(argv[2])
    db = connect(path)
    try:
        if command == "init": print(f"Train database initialized: {path}")
        elif command == "seed": seed(db); print(f"Seed trains loaded: {len(SEED)}")
        elif command in ("import","import-railpull") and len(argv) == 4: print(f"Imported trains: {import_csv(db, argv[3])}")
        elif command == "search" and len(argv) == 4:
            for row in db.execute("SELECT train_number,train_name,source_station,destination_station,train_type FROM trains WHERE train_name LIKE ? OR CAST(train_number AS TEXT)=? ORDER BY train_number", (f"%{argv[3]}%",argv[3])): print(" | ".join(map(str,row)))
        elif command == "route" and len(argv) == 5:
            for row in db.execute("SELECT train_number,train_name,source_station,destination_station,train_type FROM trains WHERE source_station LIKE ? AND destination_station LIKE ? ORDER BY train_number", (f"%{argv[3]}%",f"%{argv[4]}%")): print(" | ".join(map(str,row)))
        else: print("Invalid command or arguments"); return 1
    finally: db.close()
    return 0

if __name__ == "__main__": sys.exit(main(sys.argv))
