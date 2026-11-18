"""End-to-end test for persistent multi-passenger browser bookings."""
import json, os, pathlib, subprocess, tempfile, time, urllib.request

ROOT=pathlib.Path(__file__).resolve().parent
PORT=5163

def request(path, payload=None):
    data=json.dumps(payload).encode() if payload is not None else None
    req=urllib.request.Request(f'http://127.0.0.1:{PORT}{path}',data=data,headers={'Content-Type':'application/json'} if data else {})
    with urllib.request.urlopen(req,timeout=8) as response: return json.load(response)

def wait_ready():
    for _ in range(40):
        try: request('/api/trains?q=11058'); return
        except Exception: time.sleep(.1)
    raise AssertionError('portal did not start')

with tempfile.TemporaryDirectory(prefix='tatkal-portal-') as directory:
    temp=pathlib.Path(directory); env=os.environ.copy(); env['TATKAL_WAL']=str(temp/'test.wal'); env['TATKAL_RING']='/tatkal_portal_test_ring'
    server=subprocess.Popen([ROOT/'tatkal-core','server','2','5162'],cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    portal=subprocess.Popen(['python3',ROOT/'web_ui.py','--db',ROOT/'railway.db','--booking-db',temp/'portal.db','--port',str(PORT),'--server-port','5162'],cwd=ROOT,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        wait_ready()
        passengers=[{'name':name,'age':20+i,'gender':'F' if i%2 else 'M'} for i,name in enumerate(('Asha','Bharat','Chitra','Dev','Esha'))]
        booked=request('/api/book',{'train':11058,'train_name':'ASR CSMT EXP','source_station':'ASR','destination_station':'CSMT','journey_date':'2026-12-01','travel_class':'SL','quota':'GENERAL','passengers':passengers})
        assert len(booked['request_ids'])==2 and len(booked['berths'])==5
        saved=request('/api/bookings?pnr='+booked['pnr']); assert len(saved)==1 and len(saved[0]['passengers'])==5 and saved[0]['status']=='CONFIRMED'
        cancelled=request('/api/bookings/cancel',{'pnr':booked['pnr']}); assert cancelled['status']=='CANCELLED'
        assert request('/api/bookings/delete',{'pnr':booked['pnr']})['deleted']==booked['pnr']
        print('portal multi-passenger persistence test: passed')
    finally:
        portal.terminate(); server.terminate(); portal.wait(timeout=5); server.wait(timeout=5)
