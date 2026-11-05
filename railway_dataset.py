#!/usr/bin/env python3
"""Clean railpull timetable exports and load a normalized SQLite database."""
import csv, sqlite3, sys
from pathlib import Path

SCHEMA = """
PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS trains(
 train_number INTEGER PRIMARY KEY, name TEXT NOT NULL, train_type TEXT NOT NULL,
 runs_days TEXT NOT NULL, source_station TEXT NOT NULL, destination_station TEXT NOT NULL,
 distance_km REAL, stop_count INTEGER, active INTEGER NOT NULL DEFAULT 1,
 source_updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS stations(
 station_code TEXT PRIMARY KEY, station_name TEXT NOT NULL, latitude REAL, longitude REAL
);
CREATE TABLE IF NOT EXISTS train_stops(
 train_number INTEGER NOT NULL REFERENCES trains(train_number) ON DELETE CASCADE,
 sequence INTEGER NOT NULL, station_code TEXT, station_name TEXT NOT NULL,
 run_day INTEGER, arrival TEXT, departure TEXT, halt_minutes INTEGER, distance_km REAL,
 PRIMARY KEY(train_number,sequence)
);
CREATE INDEX IF NOT EXISTS idx_stops_station ON train_stops(station_code);
CREATE INDEX IF NOT EXISTS idx_stops_name ON train_stops(station_name);
CREATE TABLE IF NOT EXISTS train_schedules(
 train_number INTEGER PRIMARY KEY REFERENCES trains(train_number) ON DELETE CASCADE,
 payload_json TEXT NOT NULL
);
"""

def text(value): return " ".join((value or "").strip().split())
def number(value, integer=False):
    try: return int(float(value)) if integer else float(value)
    except (TypeError,ValueError): return None
def time_value(value):
    value=text(value)
    if not value: return None
    if value.count(":")==1: value += ":00"
    parts=value.split(":")
    if len(parts)!=3 or any(not p.isdigit() for p in parts): return None
    h,m,s=map(int,parts)
    return f"{h:02d}:{m:02d}:{s:02d}" if 0<=h<=48 and 0<=m<60 and 0<=s<60 else None

def clean(input_dir, output_db):
    root=Path(input_dir); db=sqlite3.connect(output_db); db.executescript(SCHEMA)
    stats={"trains_read":0,"trains_loaded":0,"stations_loaded":0,"stops_read":0,"stops_loaded":0,"schedules_loaded":0,"rejected":0}
    train_file=root/"trains.csv"; station_file=root/"stations.csv"; stops_file=root/"stops.csv"
    with train_file.open(newline="",encoding="utf-8-sig") as f:
        rows=csv.DictReader(f)
        for row in rows:
            stats["trains_read"]+=1; n=number(row.get("number") or row.get("train_number"),True)
            name=text(row.get("name") or row.get("train_name")); source=text(row.get("source") or row.get("source_station")); dest=text(row.get("destination") or row.get("destination_station")); typ=text(row.get("type") or row.get("train_type")) or "UNKNOWN"; days=text(row.get("runs_days") or row.get("runs_on")) or "UNKNOWN"
            if not n or not name or not source or not dest: stats["rejected"]+=1; continue
            db.execute("INSERT OR REPLACE INTO trains(train_number,name,train_type,runs_days,source_station,destination_station,distance_km,stop_count) VALUES(?,?,?,?,?,?,?,?)",(n,name,typ,days,source,dest,number(row.get("distance")),number(row.get("stop_count"),True)));stats["trains_loaded"]+=1
    if station_file.exists():
        with station_file.open(newline="",encoding="utf-8-sig") as f:
            for row in csv.DictReader(f):
                code=text(row.get("code") or row.get("station_code")).upper(); name=text(row.get("name") or row.get("station_name"))
                if not code or not name: stats["rejected"]+=1; continue
                db.execute("INSERT OR REPLACE INTO stations VALUES(?,?,?,?)",(code,name,number(row.get("lat") or row.get("latitude")),number(row.get("lon") or row.get("longitude"))));stats["stations_loaded"]+=1
    if stops_file.exists():
        with stops_file.open(newline="",encoding="utf-8-sig") as f:
            for row in csv.DictReader(f):
                stats["stops_read"]+=1;n=number(row.get("train") or row.get("train_number"),True);seq=number(row.get("sequence"),True);name=text(row.get("station_name") or row.get("name"));
                if not n or not seq or not name or db.execute("SELECT 1 FROM trains WHERE train_number=?",(n,)).fetchone() is None: stats["rejected"]+=1;continue
                code=text(row.get("station_code") or row.get("code")).upper() or None
                db.execute("INSERT OR REPLACE INTO train_stops VALUES(?,?,?,?,?,?,?,?,?)",(n,seq,code,name,number(row.get("day"),True),time_value(row.get("arrival")),time_value(row.get("departure")),number(row.get("halt"),True),number(row.get("distance"))));stats["stops_loaded"]+=1
    schedule_file=root/"schedules.jsonl"
    if schedule_file.exists():
        import json
        with schedule_file.open(encoding="utf-8-sig") as f:
            for line in f:
                try:
                    payload=json.loads(line); n=number(payload.get("train_number") or payload.get("number"),True)
                    if not n or db.execute("SELECT 1 FROM trains WHERE train_number=?",(n,)).fetchone() is None: stats["rejected"]+=1; continue
                    db.execute("INSERT OR REPLACE INTO train_schedules VALUES(?,?)",(n,json.dumps(payload,separators=(",",":"),ensure_ascii=False)));stats["schedules_loaded"]+=1
                except (ValueError,TypeError,json.JSONDecodeError): stats["rejected"]+=1
    db.commit();db.close();return stats

def main(argv):
    if len(argv)!=3: print("Usage: railway_dataset.py CLEANED_INPUT_DIR OUTPUT_DB");return 1
    print(clean(argv[1],argv[2]));return 0
if __name__=="__main__": sys.exit(main(sys.argv))
