"""Real TCP birthday regression. Uses ONLY new temporary accounts/data and an isolated server process.
Run: python Tests/ProfileBirthdayIntegration.py path/to/ProjectVectorServer.exe
Refuses to start if port 9000 is occupied; never stops an existing server.
"""
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode = True
from LobbySocialIntegration import Client, fixed


def profile(client, target=''):
    client.send(84, fixed(target, 20))
    packet = client.wait(85)
    assert packet[0] == 0
    return packet


def birthday(packet):
    return packet[91:107].split(b'\0', 1)[0].decode()


def save(client, request, month, day, result=0):
    client.send(363, struct.pack('<IBB', request, month, day))
    packet = client.wait(364)
    assert len(packet) == 7 and struct.unpack_from('<I', packet)[0] == request
    assert packet[4] == result, (packet, result)
    assert tuple(packet[5:]) == ((month, day) if result == 0 else (0, 0))


def connect_existing(user):
    client = Client.__new__(Client)
    client.sock = socket.create_connection(('127.0.0.1', 9000), 3)
    client.pending = []
    client.user = user
    client.send(2, fixed(user, 20) + fixed('TestOnly123', 64))
    assert client.wait(3)[0] == 0
    client.send(4)
    client.wait(5)
    return client


def main():
    exe = Path(sys.argv[1]).resolve()
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(('0.0.0.0', 9000))
    root = Path(tempfile.mkdtemp(prefix='VectorBirthdayTest-'))
    source = Path(__file__).resolve().parents[1] / 'ProjectVectorServer' / 'ProjectVectorServer'
    for name in ['career_seasons.csv', 'career_rank_tiers.csv', 'career_items.csv', 'level_exp_table.csv', 'weapon_skins.csv', 'shop_catalog.csv', 'SkillPriceTable.csv', 'CreditRewardTable.csv']:
        if (source / name).exists(): shutil.copy2(source / name, root / name)
    print('Isolated test data:', root, flush=True)
    clients = []
    server = None
    with (root / 'server.log').open('wb') as log:
        def start():
            process = subprocess.Popen([str(exe)], cwd=root, stdout=log, stderr=subprocess.STDOUT,
                env={k.upper(): v for k, v in os.environ.items()}, creationflags=subprocess.CREATE_NO_WINDOW)
            time.sleep(1.5)
            assert process.poll() is None, 'isolated server failed to start'
            return process
        try:
            server = start()
            a, b = Client(0), Client(1)
            clients = [a, b]
            original = profile(a)
            assert birthday(original) == '----', 'legacy account defaults to unset'
            files_before = {p.name: p.read_bytes() for p in root.glob('*.csv')}
            save(a, 1, 2, 29)
            saved = profile(a)
            assert birthday(saved) == '02.29'
            assert original[:91] == saved[:91] and original[107:] == saved[107:], 'unrelated profile packet fields changed'
            assert all((root / n).read_bytes() == v for n, v in files_before.items()), 'birthday save rewrote existing account data'
            assert birthday(profile(b)) == '----', 'other account modified'
            for i, (month, day) in enumerate([(0, 1), (13, 1), (1, 0), (2, 30), (4, 31), (12, 32), (255, 255)]):
                save(a, 20 + i, month, day, 1)
                assert birthday(profile(a)) == '02.29', 'invalid date mutated stored birthday'
            # The request carries no target identity; oversize forged-target packets are ignored.
            a.send(363, struct.pack('<IBB', 30, 1, 1) + fixed(b.user, 20))
            assert birthday(profile(a)) == '02.29' and birthday(profile(b)) == '----'
            # Deliberately make only the test store's staging path unwritable.
            staging = root / 'profile_birthdays.csv.tmp'
            staging.mkdir()
            save(a, 31, 1, 1, 3)
            assert birthday(profile(a)) == '02.29', 'failed disk write did not roll back'
            staging.rmdir()
            save(a, 32, 10, 4)
            a.send(86, b'\1')
            assert a.wait(87)[0] == 0
            assert birthday(profile(a)) == '10.04', 'owner needs real value for editing private profile'
            assert birthday(profile(b, a.user)) == '----', 'private birthday leaked in wire response'
            a.send(86, b'\0')
            assert a.wait(87)[0] == 0
            assert birthday(profile(b, a.user)) == '10.04', 'public birthday missing'
            for client in clients: client.sock.close()
            clients = []
            server.terminate(); server.wait(timeout=8)
            server = start()
            a, b = connect_existing('socialtest0'), connect_existing('socialtest1')
            clients = [a, b]
            assert birthday(profile(a)) == '10.04' and birthday(profile(b)) == '----', 'restart/relogin persistence'
            save(a, 40, 12, 31)
            assert birthday(profile(a)) == '12.31', 'editing saved birthday'
            # A malformed birthday store must never be overwritten with partially loaded data.
            for client in clients: client.sock.close()
            clients = []
            server.terminate(); server.wait(timeout=8)
            with (root / 'profile_birthdays.csv').open('a') as file: file.write('broken,not-a-date\n')
            corrupted = (root / 'profile_birthdays.csv').read_bytes()
            server = start()
            a = connect_existing('socialtest0'); clients = [a]
            save(a, 41, 1, 1, 3)
            assert (root / 'profile_birthdays.csv').read_bytes() == corrupted, 'malformed store overwritten'
            assert birthday(profile(a)) == '12.31'
            print('PASS: legacy compatibility, leap birthday, invalid dates, packet size, self-only writes, unrelated data unchanged, atomic failure rollback, privacy, server restart/relogin, editing, malformed-store preservation', flush=True)
        finally:
            for client in clients: client.sock.close()
            if server is not None and server.poll() is None:
                server.terminate(); server.wait(timeout=8)
    print('Test data preserved:', root)


if __name__ == '__main__': main()
