#!/usr/bin/env python3
"""Local browser UI bridge for the Tatkal-Core SQLite database and TCP server."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs
import argparse, json, os, random, re, socket, sqlite3, time, subprocess, threading
from pathlib import Path

ROOT=Path(__file__).resolve().parent

BOOKING_SCHEMA="""
CREATE TABLE IF NOT EXISTS portal_bookings(
 pnr TEXT PRIMARY KEY, train_number INTEGER NOT NULL, train_name TEXT NOT NULL,
 source_station TEXT, destination_station TEXT, journey_date TEXT NOT NULL,
 travel_class TEXT NOT NULL, quota TEXT NOT NULL, status TEXT NOT NULL,
 created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS portal_passengers(
 id INTEGER PRIMARY KEY AUTOINCREMENT, pnr TEXT NOT NULL REFERENCES portal_bookings(pnr) ON DELETE CASCADE,
 name TEXT NOT NULL, age INTEGER NOT NULL, gender TEXT NOT NULL, berth INTEGER
);
CREATE TABLE IF NOT EXISTS portal_requests(
 pnr TEXT NOT NULL REFERENCES portal_bookings(pnr) ON DELETE CASCADE,
 request_id INTEGER NOT NULL UNIQUE, PRIMARY KEY(pnr,request_id)
);
"""

def tcp(host, port, line):
    with socket.create_connection((host, port), timeout=6) as s:
        s.sendall((line+'\n').encode()); return s.recv(1024).decode(errors='replace').strip()

def booking_db(path):
    con=sqlite3.connect(path); con.execute('PRAGMA foreign_keys=ON'); con.executescript(BOOKING_SCHEMA); return con

def extract_berths(reply):
    found=re.search(r'berths=([0-9,]+)',reply)
    return [int(x) for x in found.group(1).split(',')] if found else []

class Handler(BaseHTTPRequestHandler):
    db_path='railway.db'; booking_db_path='portal_bookings.db'; tcp_host='127.0.0.1'; tcp_port=5050
    def log_message(self, *args): pass
    def send_json(self, value, code=200):
        data=json.dumps(value).encode(); self.send_response(code); self.send_header('Content-Type','application/json'); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
    def body(self):
        n=int(self.headers.get('Content-Length','0')); return json.loads(self.rfile.read(n) or b'{}')
    def do_GET(self):
        u=urlparse(self.path)
        if u.path=='/':
            data=(ROOT/'ui'/'index.html').read_bytes(); self.send_response(200); self.send_header('Content-Type','text/html; charset=utf-8'); self.send_header('Cache-Control','no-store'); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data); return
        if u.path=='/app.js': self.asset('app.js','text/javascript'); return
        if u.path=='/styles.css': self.asset('styles.css','text/css'); return
        q=parse_qs(u.query)
        try:
            con=sqlite3.connect(self.db_path); con.row_factory=sqlite3.Row
            if u.path=='/api/trains':
                term=q.get('q',[''])[0].upper(); source=q.get('from',[''])[0].upper(); destination=q.get('to',[''])[0].upper()
                sql='SELECT DISTINCT t.train_number,t.name,t.train_type,t.runs_days,t.source_station,t.destination_station,t.stop_count FROM trains t'; where=[]; args=[]
                if term:
                    where.append('(CAST(t.train_number AS TEXT)=? OR UPPER(t.name) LIKE ?)'); args += [term,f'%{term}%']
                if source:
                    where.append("(UPPER(t.source_station) LIKE ? OR EXISTS (SELECT 1 FROM train_stops s WHERE s.train_number=t.train_number AND (UPPER(s.station_code)=? OR UPPER(s.station_name) LIKE ?)))"); args += [f'%{source}%',source,f'%{source}%']
                if destination:
                    where.append("(UPPER(t.destination_station) LIKE ? OR EXISTS (SELECT 1 FROM train_stops s WHERE s.train_number=t.train_number AND (UPPER(s.station_code)=? OR UPPER(s.station_name) LIKE ?)))"); args += [f'%{destination}%',destination,f'%{destination}%']
                if source and destination:
                    where.append("EXISTS (SELECT 1 FROM train_stops a JOIN train_stops b ON a.train_number=b.train_number WHERE a.train_number=t.train_number AND a.sequence<b.sequence AND (UPPER(a.station_code)=? OR UPPER(a.station_name) LIKE ?) AND (UPPER(b.station_code)=? OR UPPER(b.station_name) LIKE ?))"); args += [source,f'%{source}%',destination,f'%{destination}%']
                if where: sql += ' WHERE '+' AND '.join(where)
                rows=[dict(r) for r in con.execute(sql+' ORDER BY t.train_number LIMIT 50',args)]
                self.send_json(rows); return
            if u.path=='/api/stations':
                rows=[dict(r) for r in con.execute('SELECT station_code,station_name FROM stations ORDER BY station_name LIMIT 500')]
                if not rows: rows=[{'station_code':r[0],'station_name':r[0]} for r in con.execute('SELECT DISTINCT station_code,station_name FROM train_stops WHERE station_name IS NOT NULL ORDER BY station_name LIMIT 500')]
                if not rows: rows=[{'station_code':r[0],'station_name':r[0]} for r in con.execute('SELECT station_name,station_name FROM (SELECT source_station AS station_name FROM trains UNION SELECT destination_station FROM trains) ORDER BY station_name LIMIT 500')]
                self.send_json(rows); return
            if u.path=='/api/stops':
                n=int(q.get('train',['0'])[0]); rows=[dict(r) for r in con.execute('SELECT sequence,station_code,station_name,run_day,arrival,departure,halt_minutes,distance_km FROM train_stops WHERE train_number=? ORDER BY sequence',(n,))]; self.send_json(rows); return
            if u.path=='/api/availability':
                n=int(q.get('train',['0'])[0]); result=tcp(self.tcp_host,self.tcp_port,f'AVAIL,{n}'); self.send_json({'raw':result}); return
            if u.path=='/api/bookings':
                bcon=booking_db(self.booking_db_path); pnr=q.get('pnr',[''])[0]
                sql='SELECT * FROM portal_bookings'+(' WHERE pnr=?' if pnr else '')+' ORDER BY created_at DESC LIMIT 30'
                bookings=[]
                for row in bcon.execute(sql,(pnr,) if pnr else ()):
                    item=dict(zip(['pnr','train_number','train_name','source_station','destination_station','journey_date','travel_class','quota','status','created_at'],row))
                    item['passengers']=[dict(zip(['name','age','gender','berth'],p)) for p in bcon.execute('SELECT name,age,gender,berth FROM portal_passengers WHERE pnr=? ORDER BY id',(item['pnr'],))]
                    item['request_ids']=[p[0] for p in bcon.execute('SELECT request_id FROM portal_requests WHERE pnr=?',(item['pnr'],))]
                    bookings.append(item)
                bcon.close(); self.send_json(bookings); return
            con.close()
            self.send_json({'error':'not found'},404)
        except Exception as e: self.send_json({'error':str(e)},500)
    def asset(self,name,kind):
        data=(ROOT/'ui'/name).read_bytes(); self.send_response(200); self.send_header('Content-Type',kind); self.send_header('Cache-Control','no-store'); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
    def do_POST(self):
        try:
            b=self.body(); path=urlparse(self.path).path
            if path=='/api/book':
                train=int(b['train']); passengers=b.get('passengers',[])
                if not 1<=len(passengers)<=6: self.send_json({'error':'A ticket must contain 1 to 6 passengers.'},400); return
                if not re.fullmatch(r'\d{4}-\d{2}-\d{2}',str(b.get('journey_date',''))): self.send_json({'error':'Journey date must be YYYY-MM-DD.'},400); return
                for p in passengers:
                    if not str(p.get('name','')).strip() or not isinstance(p.get('age'),int) or not 1<=p['age']<=120 or p.get('gender') not in ('M','F','O'):
                        self.send_json({'error':'Every passenger needs a valid name, age, and gender.'},400); return
                request_ids=[]; berths=[]; replies=[]; base=int(time.time_ns()//1000)
                for offset in range(0,len(passengers),4):
                    rid=base+offset; reply=tcp(self.tcp_host,self.tcp_port,f'{rid},{train},{min(4,len(passengers)-offset)},1,-1'); replies.append(reply)
                    if not reply.startswith('CONFIRMED'):
                        for previous in request_ids: tcp(self.tcp_host,self.tcp_port,f'CANCEL,{previous}')
                        self.send_json({'error':'Reservation could not be confirmed for all passengers.','details':replies},409); return
                    request_ids.append(rid); berths.extend(extract_berths(reply))
                pnr=str(random.randint(1000000000,9999999999)); con=booking_db(self.booking_db_path)
                con.execute('INSERT INTO portal_bookings(pnr,train_number,train_name,source_station,destination_station,journey_date,travel_class,quota,status) VALUES(?,?,?,?,?,?,?,?,?)',(pnr,train,str(b.get('train_name','Train')),str(b.get('source_station','')),str(b.get('destination_station','')),b['journey_date'],str(b.get('travel_class','SL')),str(b.get('quota','GENERAL')),'CONFIRMED'))
                for p,berth in zip(passengers,berths): con.execute('INSERT INTO portal_passengers(pnr,name,age,gender,berth) VALUES(?,?,?,?,?)',(pnr,p['name'].strip(),p['age'],p['gender'],berth))
                for rid in request_ids: con.execute('INSERT INTO portal_requests VALUES(?,?)',(pnr,rid))
                con.commit(); con.close(); self.send_json({'pnr':pnr,'request_ids':request_ids,'berths':berths,'raw':replies}); return
            if path=='/api/status': self.send_json({'raw':tcp(self.tcp_host,self.tcp_port,f"STATUS,{int(b['request_id'])}")}); return
            if path=='/api/cancel': self.send_json({'raw':tcp(self.tcp_host,self.tcp_port,f"CANCEL,{int(b['request_id'])}")}); return
            if path=='/api/bookings/cancel':
                pnr=str(b.get('pnr','')); con=booking_db(self.booking_db_path); ids=[r[0] for r in con.execute('SELECT request_id FROM portal_requests WHERE pnr=?',(pnr,))]
                if not ids: con.close(); self.send_json({'error':'PNR not found.'},404); return
                replies=[tcp(self.tcp_host,self.tcp_port,f'CANCEL,{rid}') for rid in ids]
                state='CANCELLED' if all(r=='CANCELLED' for r in replies) else 'CANCELLATION_PENDING'
                con.execute('UPDATE portal_bookings SET status=? WHERE pnr=?',(state,pnr)); con.commit(); con.close(); self.send_json({'pnr':pnr,'status':state,'replies':replies}); return
            if path=='/api/bookings/delete':
                pnr=str(b.get('pnr','')); con=booking_db(self.booking_db_path); row=con.execute('SELECT status FROM portal_bookings WHERE pnr=?',(pnr,)).fetchone()
                if not row: con.close(); self.send_json({'error':'PNR not found.'},404); return
                if row[0] not in ('CANCELLED','CANCELLATION_PENDING'): con.close(); self.send_json({'error':'Cancel the active reservation before deleting its saved record.'},409); return
                con.execute('DELETE FROM portal_bookings WHERE pnr=?',(pnr,)); con.commit(); con.close(); self.send_json({'deleted':pnr}); return
            if path=='/api/os':
                test=str(b.get('test',''))
                if test=='same-seat':
                    base=int(time.time_ns()//1000); responses=[None,None]; gate=threading.Barrier(2)
                    def contender(index):
                        gate.wait(); responses[index]=tcp(self.tcp_host,self.tcp_port,f'{base+index},99999,1,1,0')
                    jobs=[threading.Thread(target=contender,args=(i,)) for i in range(2)]
                    [j.start() for j in jobs]; [j.join() for j in jobs]
                    confirmed=[base+i for i,r in enumerate(responses) if r and r.startswith('CONFIRMED')]
                    cleanup=[tcp(self.tcp_host,self.tcp_port,f'CANCEL,{rid}') for rid in confirmed]
                    seat_zero=sum('berths=0' in (r or '') for r in responses)
                    passed=seat_zero==1
                    self.send_json({'passed':passed,'title':'Simultaneous same-seat contention','output':'\n'.join(responses+cleanup),'note':'Both requests prefer berth 0. The allocator may give the losing request another free berth, but berth 0 must be allocated exactly once. Confirmed test bookings were cancelled after observation.'}); return
                commands={'deadlock':['demo'],'memory':['demo'],'disk':['demo'],'ipc':['ipc-ring-demo'],'wal':['wal-fault-demo']}
                if test not in commands: self.send_json({'error':'unknown OS test'},400); return
                run=subprocess.run([str(ROOT/'tatkal-core'),*commands[test]],cwd=ROOT,text=True,capture_output=True,timeout=20)
                lines=run.stdout.splitlines()
                if test=='deadlock': lines=[x for x in lines if 'Banker' in x or 'Wait-for' in x or 'Recovery:' in x]
                if test=='memory': lines=[x for x in lines if 'Memory' in x or x.startswith(('Clock:','FIFO:','LRU:'))]
                if test=='disk': lines=[x for x in lines if 'Disk' in x or x.startswith(('FCFS','SCAN','LOOK','C-SCAN','Service order'))]
                self.send_json({'passed':run.returncode==0,'title':test.title()+' test','output':'\n'.join(lines) or run.stderr}); return
            self.send_json({'error':'not found'},404)
        except Exception as e: self.send_json({'error':str(e)},500)

def main():
    p=argparse.ArgumentParser(); p.add_argument('--db',default='railway.db'); p.add_argument('--booking-db',default='portal_bookings.db'); p.add_argument('--port',type=int,default=8088); p.add_argument('--server-port',type=int,default=5050); a=p.parse_args()
    Handler.db_path=a.db; Handler.booking_db_path=a.booking_db; Handler.tcp_port=a.server_port
    booking_db(a.booking_db).close()
    print(f'Tatkal-Core web UI: http://127.0.0.1:{a.port}'); ThreadingHTTPServer(('127.0.0.1',a.port),Handler).serve_forever()
if __name__=='__main__': main()
