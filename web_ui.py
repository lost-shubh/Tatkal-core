#!/usr/bin/env python3
"""Local browser UI bridge for the Tatkal-Core SQLite database and TCP server."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs
import argparse, json, os, random, re, socket, sqlite3, time
from pathlib import Path

ROOT=Path(__file__).resolve().parent

def tcp(host, port, line):
    with socket.create_connection((host, port), timeout=6) as s:
        s.sendall((line+'\n').encode()); return s.recv(1024).decode(errors='replace').strip()

class Handler(BaseHTTPRequestHandler):
    db_path='railway.db'; tcp_host='127.0.0.1'; tcp_port=5050
    def log_message(self, *args): pass
    def send_json(self, value, code=200):
        data=json.dumps(value).encode(); self.send_response(code); self.send_header('Content-Type','application/json'); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
    def body(self):
        n=int(self.headers.get('Content-Length','0')); return json.loads(self.rfile.read(n) or b'{}')
    def do_GET(self):
        u=urlparse(self.path)
        if u.path=='/':
            data=(ROOT/'ui'/'index.html').read_bytes(); self.send_response(200); self.send_header('Content-Type','text/html; charset=utf-8'); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data); return
        if u.path=='/app.js': self.asset('app.js','text/javascript'); return
        if u.path=='/styles.css': self.asset('styles.css','text/css'); return
        q=parse_qs(u.query)
        try:
            con=sqlite3.connect(self.db_path); con.row_factory=sqlite3.Row
            if u.path=='/api/trains':
                term=q.get('q',[''])[0].upper(); sql='SELECT train_number,name,train_type,runs_days,source_station,destination_station,stop_count FROM trains'; args=[]
                if term: sql+=' WHERE CAST(train_number AS TEXT)=? OR UPPER(name) LIKE ? OR UPPER(source_station) LIKE ? OR UPPER(destination_station) LIKE ?'; args=[term,f'%{term}%',f'%{term}%',f'%{term}%']
                rows=[dict(r) for r in con.execute(sql+' ORDER BY train_number LIMIT 50',args)]
                self.send_json(rows); return
            if u.path=='/api/stops':
                n=int(q.get('train',['0'])[0]); rows=[dict(r) for r in con.execute('SELECT sequence,station_code,station_name,run_day,arrival,departure,halt_minutes,distance_km FROM train_stops WHERE train_number=? ORDER BY sequence',(n,))]; self.send_json(rows); return
            if u.path=='/api/availability':
                n=int(q.get('train',['0'])[0]); result=tcp(self.tcp_host,self.tcp_port,f'AVAIL,{n}'); self.send_json({'raw':result}); return
            con.close()
            self.send_json({'error':'not found'},404)
        except Exception as e: self.send_json({'error':str(e)},500)
    def asset(self,name,kind):
        data=(ROOT/'ui'/name).read_bytes(); self.send_response(200); self.send_header('Content-Type',kind); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
    def do_POST(self):
        try:
            b=self.body(); path=urlparse(self.path).path
            if path=='/api/book':
                rid=int(time.time_ns()//1000); train=int(b['train']); count=int(b['count']); raw=tcp(self.tcp_host,self.tcp_port,f'{rid},{train},{count},1,-1'); self.send_json({'request_id':rid,'pnr':random.randint(1000000000,9999999999),'raw':raw}); return
            if path=='/api/status': self.send_json({'raw':tcp(self.tcp_host,self.tcp_port,f"STATUS,{int(b['request_id'])}")}); return
            if path=='/api/cancel': self.send_json({'raw':tcp(self.tcp_host,self.tcp_port,f"CANCEL,{int(b['request_id'])}")}); return
            self.send_json({'error':'not found'},404)
        except Exception as e: self.send_json({'error':str(e)},500)

def main():
    p=argparse.ArgumentParser(); p.add_argument('--db',default='railway.db'); p.add_argument('--port',type=int,default=8000); p.add_argument('--server-port',type=int,default=5050); a=p.parse_args()
    Handler.db_path=a.db; Handler.tcp_port=a.server_port
    print(f'Tatkal-Core web UI: http://127.0.0.1:{a.port}'); ThreadingHTTPServer(('127.0.0.1',a.port),Handler).serve_forever()
if __name__=='__main__': main()
