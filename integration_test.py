"""Run in WSL; isolated WAL and IPC channel, no production data changes."""
import os
import pathlib
import subprocess
import tempfile
import time
import uuid

root = pathlib.Path(__file__).resolve().parent
env = dict(os.environ, TATKAL_RING='/tatkal_test_' + uuid.uuid4().hex)
with tempfile.TemporaryDirectory(prefix='tatkal-test-') as directory:
    batch = pathlib.Path(directory) / 'requests.csv'
    batch.write_text('2001,12035,1,0,0\n2002,12036,1,2,0\ninvalid\n')
    def start():
        process = subprocess.Popen([str(root/'tatkal-core'), 'server', '4'],
                                   cwd=directory, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for _ in range(100):
            if process.poll() is not None:
                raise AssertionError(process.communicate()[0])
            if pathlib.Path('/dev/shm', env['TATKAL_RING'][1:]).exists():
                return process
            time.sleep(.05)
        process.terminate(); process.communicate(timeout=5)
        raise AssertionError('server startup timed out')
    server = start()
    try:
        result = subprocess.run([str(root/'tatkal-client')], input='1\n12034\n3\n1\n34\n2\n',
                                text=True, capture_output=True, cwd=directory, env=env, timeout=35)
        assert result.returncode == 0, result.stderr
        assert 'CONFIRMED. Train 12034 berths: 34 35 36' in result.stdout, result.stdout
        print('customer confirmation: passed')
    finally:
        output = server.communicate(f'queue-stats\nmetrics\nshow-bookings\nmemory-stats\narm-trace\ningress {batch}\nshutdown\n', timeout=8)[0]
        assert server.returncode == 0, output
        assert 'Queue: total=0' in output, output
        assert 'Total Completed: 3' in output, output
        assert 'Train 12034 berth 34: Request' in output, output
        assert 'Ingress: accepted=2 rejected=1' in output, output
        assert 'faults=7 hits=1' in output, output
        assert 'total movement=382' in output, output
        print('live monitoring and command shutdown: passed')
    server = start()
    server.terminate()
    output = server.communicate(timeout=5)[0]
    assert 'Restored 3 confirmed bookings.' in output, output
    assert server.returncode == 0, output
    print('server restart and idle shutdown: passed')
    server = start()
    server.kill()
    server.communicate(timeout=5)
    server = start()
    output = server.communicate('shutdown\n',timeout=8)[0]
    assert server.returncode==0, output
    assert 'Restored 3 confirmed bookings.' in output, output
    print('stale IPC cleanup after crash: passed')
