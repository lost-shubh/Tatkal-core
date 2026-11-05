#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path
import train_database

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "trains.db"
    db = train_database.connect(path)
    assert train_database.import_csv(db, Path(__file__).with_name("trains.csv")) == 10
    assert db.execute("select count(*) from trains").fetchone()[0] == 10
    assert db.execute("select train_number from trains where train_name like '%Rajdhani%'").fetchall()
    assert db.execute("select train_number from trains where source_station='New Delhi' and destination_station='Mumbai Central'").fetchone()[0] == 12952
    db.close()
    railpull_csv = Path(directory) / "railpull.csv"
    railpull_csv.write_text("train_number,name,source_station,destination_station,train_type,runs_days\n99001,Test Special,Alpha,Beta,MEX,\"Mon,Fri\"\n", encoding="utf-8")
    db = train_database.connect(path)
    # The importer accepts railpull's name/runs_days column names as well.
    assert train_database.import_csv(db, railpull_csv) == 1
    assert db.execute("select train_name from trains where train_number=99001").fetchone()[0] == "Test Special"
    db.close()
print("train database test: passed")
