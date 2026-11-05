"""Repeatable isolated load tests; preserves the server's bookings.wal."""
import json
import os
import pathlib
import platform
import re
import subprocess
import tempfile

root=pathlib.Path(__file__).resolve().parent
report={'platform':platform.platform(), 'cpu_count':os.cpu_count(),
        'workers':4, 'arrival':'producer burst with bounded-queue backpressure',
        'runs':[]}
for requests in (100,1000,10000):
    for repeat in range(3):
        with tempfile.TemporaryDirectory(prefix='tatkal-benchmark-') as directory:
            result=subprocess.run([str(root/'tatkal-core'),'workload',str(requests),'4'],
                                  cwd=directory,text=True,capture_output=True,timeout=120,check=True)
            summary=next(line for line in result.stdout.splitlines() if line.startswith('completed='))
            fields=dict(re.findall(r'(\w+)=([\d.]+)',summary))
            assert int(fields['completed'])==requests
            assert int(fields['confirmed'])+int(fields['rejected'])==requests
            assert int(fields['confirmed'])==min(requests,480)
            assert int(fields['wal_failures'])==0
            assert int(fields['queue_high_water'])<=256
            checked=subprocess.run([str(root/'tatkal-core'),'recover','bookings.wal'],cwd=directory,
                                   text=True,capture_output=True,check=True)
            assert f"{fields['confirmed']} valid records" in checked.stdout
            report['runs'].append({'requests':requests,'repeat':repeat+1,**fields})
            print(summary)
print(json.dumps(report,indent=2))
