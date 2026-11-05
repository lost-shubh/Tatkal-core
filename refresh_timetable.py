#!/usr/bin/env python3
"""Export railpull data and atomically refresh railway.db."""
import argparse, json, subprocess, sys, tempfile
from pathlib import Path
import railway_dataset

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--railpull", default="../railpull")
    ap.add_argument("--db", default="railway.db")
    ap.add_argument("--allow-partial", action="store_true")
    args=ap.parse_args()
    root=Path(args.railpull).resolve(); raw=root/"data/raw/crawl-status.json"; out=root/"data/out"
    if raw.exists():
        status=json.loads(raw.read_text(encoding="utf-8")); phase=status.get("phase","")
        if phase not in {"schedules-done","complete","exported"} and not args.allow_partial:
            print(f"Crawl is not complete (phase={phase}, trains={status.get('numbersFound',0)}). Refusing partial refresh.",file=sys.stderr); return 2
    export=root/"transform/export.py"
    if export.exists():
        result=subprocess.run([sys.executable,str(export)],cwd=root)
        if result.returncode: return result.returncode
    if not (out/"trains.csv").exists():
        print(f"Missing export: {out/'trains.csv'}",file=sys.stderr); return 2
    destination=Path(args.db).resolve(); destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=destination.stem+".",suffix=".db",dir=destination.parent,delete=False) as f: temp=Path(f.name)
    try:
        print(railway_dataset.clean(out,temp))
        temp.replace(destination)
    finally:
        if temp.exists(): temp.unlink()
    print(f"Installed {destination}")
    return 0

if __name__ == "__main__": raise SystemExit(main())
