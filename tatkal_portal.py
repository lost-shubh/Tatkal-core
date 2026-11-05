#!/usr/bin/env python3
"""IRCTC-style terminal portal backed by railway.db and the Tatkal TCP server."""
import argparse, csv, os, random, socket, sqlite3, time, re

def ask(prompt, validator=lambda x: bool(x)):
    while True:
        value = input(prompt).strip()
        if validator(value): return value
        print("Invalid value; please try again.")

def trains(db, source, destination, number):
    where, args = [], []
    if number:
        where.append("CAST(train_number AS TEXT)=?"); args.append(number)
    if source:
        where.append("(UPPER(source_station) LIKE ? OR EXISTS (SELECT 1 FROM train_stops s WHERE s.train_number=trains.train_number AND UPPER(s.station_name) LIKE ?))")
        args.extend([f"%{source.upper()}%", f"%{source.upper()}%"])
    if destination:
        where.append("(UPPER(destination_station) LIKE ? OR EXISTS (SELECT 1 FROM train_stops s WHERE s.train_number=trains.train_number AND UPPER(s.station_name) LIKE ?))")
        args.extend([f"%{destination.upper()}%", f"%{destination.upper()}%"])
    sql = "SELECT train_number,name,train_type,runs_days,source_station,destination_station,stop_count FROM trains"
    if where: sql += " WHERE " + " AND ".join(where)
    return db.execute(sql + " ORDER BY train_number LIMIT 30", args).fetchall()

def server(host, port, line):
    with socket.create_connection((host, port), timeout=5) as s:
        s.sendall((line + "\n").encode())
        return s.recv(512).decode(errors="replace").strip()

def show_stops(db, train_number):
    rows = db.execute("SELECT sequence,station_code,station_name,run_day,arrival,departure,halt_minutes,distance_km FROM train_stops WHERE train_number=? ORDER BY sequence", (train_number,)).fetchall()
    if not rows:
        print("No intermediate-station timetable is loaded for this train yet.")
        print("Re-import the completed railpull export to populate train_stops.")
        return []
    print("\nIntermediate stations")
    print("Seq  Code      Station                         Day  Arrival  Departure  Halt  Distance")
    for seq, code, name, day, arrival, departure, halt, distance in rows:
        print(f"{seq:3}  {(code or '-'):8}  {name[:32]:32} {day or '-':>3}  {(arrival or '-'):>7}  {(departure or '-'):>9}  {(halt if halt is not None else '-'):>4}  {(distance if distance is not None else '-'):>8}")
    return rows

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default="railway.db")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5050)
    args = ap.parse_args()
    if not os.path.exists(args.db):
        raise SystemExit(f"Database not found: {args.db}. Import railpull output first.")
    db = sqlite3.connect(args.db)
    print("\n=== Tatkal-Core Customer Reservation Portal ===")
    print("Berths are allocated automatically by the server; passengers cannot select a berth.")
    while True:
        print("\n1) Search trains  2) Book seats  3) Exit")
        choice = input("Choose: ").strip()
        if choice == "3": break
        if choice not in {"1", "2"}: print("Choose 1, 2, or 3."); continue
        source = input("From station/code (blank for any): ").strip()
        destination = input("To station/code (blank for any): ").strip()
        number = input("Train number (blank for any): ").strip()
        rows = trains(db, source, destination, number)
        if not rows: print("No trains found in the timetable database."); continue
        print("\nTrain  Name                              Type   Runs       Route                 Stops")
        for r in rows:
            print(f"{r[0]:5}  {r[1][:32]:32} {r[2][:6]:6} {r[3][:9]:9} {r[4][:10]} -> {r[5][:10]} {r[6] or 0:5}")
        details = input("View intermediate stations for a train (number, blank to continue): ").strip()
        if details:
            if any(str(r[0]) == details for r in rows): show_stops(db, details)
            else: print("That train is not in the displayed results.")
        if choice == "1": continue
        selected = ask("Train number to book: ", lambda x: any(str(r[0]) == x for r in rows))
        stop_rows = show_stops(db, selected)
        journey_date = ask("Journey date (YYYY-MM-DD): ", lambda x: bool(re.fullmatch(r"\d{4}-\d{2}-\d{2}", x)))
        boarding = input("Boarding station code/name (blank for train origin): ").strip()
        destination_stop = input("Destination station code/name (blank for train destination): ").strip()
        if stop_rows:
            names = [(str(r[1] or "").upper(), str(r[2]).upper()) for r in stop_rows]
            if boarding and not any(boarding.upper() in pair for pair in names): print("Boarding station is not on this train route."); continue
            if destination_stop and not any(destination_stop.upper() in pair for pair in names): print("Destination station is not on this train route."); continue
        count = int(ask("Number of seats (1-4): ", lambda x: x.isdigit() and 1 <= int(x) <= 4))
        print("Checking live availability...")
        try:
            availability = server(args.host, args.port, f"AVAIL,{selected}")
            print(availability)
            available = int(availability.split("seats=")[1].split()[0])
            if count > available: print("Not enough seats available."); continue
        except Exception as e:
            print(f"Could not query server availability: {e}"); continue
        name = ask("Passenger full name: ")
        age = ask("Passenger age: ", lambda x: x.isdigit() and 1 <= int(x) <= 120)
        gender = ask("Gender (M/F/O): ", lambda x: x.upper() in {"M", "F", "O"}).upper()
        quota = ask("Quota (GENERAL/TATKAL): ", lambda x: x.upper() in {"GENERAL", "TATKAL"}).upper()
        pnr = random.randint(1000000000, 9999999999)
        request_id = int(time.time_ns() // 1000)
        try:
            result = server(args.host, args.port, f"{request_id},{selected},{count},1,-1")
        except Exception as e:
            print(f"Booking request failed: {e}"); continue
        print("\n" + result)
        if result.startswith("CONFIRMED"):
            with open("passengers.csv", "a", newline="", encoding="utf-8") as f:
                csv.writer(f).writerow([pnr, request_id, selected, journey_date, boarding, destination_stop, name, age, gender, quota, count, result])
            print(f"PNR: {pnr}")
            print(f"Passenger: {name}, age {age}, {gender}; quota={quota}; berths assigned by system.")
    db.close()

if __name__ == "__main__": main()
