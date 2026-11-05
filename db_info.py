#!/usr/bin/env python3
"""Audit the installed timetable database and railpull source checkpoint."""
import argparse, json, sqlite3
from pathlib import Path

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--db",default="railway.db"); ap.add_argument("--railpull",default="../railpull"); a=ap.parse_args()
    db=Path(a.db); source=Path(a.railpull)
    print(f"database={db.resolve()} exists={db.exists()}")
    if db.exists():
        c=sqlite3.connect(db)
        for table in ("trains","stations","train_stops","train_schedules"):
            try: print(f"{table}={c.execute('SELECT count(*) FROM '+table).fetchone()[0]}")
            except sqlite3.Error: print(f"{table}=missing")
        c.close()
    status=source/"data/raw/crawl-status.json"
    if status.exists():
        s=json.loads(status.read_text(encoding="utf-8")); print("crawl_phase="+str(s.get("phase"))); print("crawl_numbers="+str(s.get("numbersFound",0))); print("crawl_updated="+str(s.get("updatedAt","")))
    out=source/"data/out"
    for name in ("trains.csv","stations.csv","stops.csv","schedules.jsonl"):
        p=out/name; print(f"export_{name}="+(str(p.stat().st_size) if p.exists() else "missing"))
if __name__=="__main__": main()
