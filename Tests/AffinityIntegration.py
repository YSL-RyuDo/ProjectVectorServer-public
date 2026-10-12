"""Live TCP affinity contract test, ONLY in a fresh temporary data directory.
Usage: python -B Tests/AffinityIntegration.py <server.exe>
Never stops an existing server. No real accounts, wallet edits, or Unity edits.
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


def state(client, request, character, kind=366, revision=0, dialogue='', choice=0, expected=0):
    if kind == 366:
        body = struct.pack('<Ii', request, character)
    else:
        body = struct.pack('<IIi', request, revision, character) + fixed(dialogue, 32) + bytes([choice])
    client.send(kind, body)
    packet = client.wait(kind+1, lambda data: struct.unpack_from('<I', data)[0] == request)
    request_id, status, version, returned_character, level, points, next_points, count = struct.unpack_from('<IBIiiiiH', packet)
    assert status == expected and returned_character == character
    assert len(packet) == 27 + count * 33
    entries = {}
    for i in range(count):
        offset = 27 + i * 33
        key = packet[offset:offset+32].split(b'\0',1)[0].decode('ascii')
        entries[key] = packet[offset+32]
    return version, level, points, next_points, entries


def main():
    exe = Path(sys.argv[1]).resolve()
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(('0.0.0.0', 9000))
    root = Path(tempfile.mkdtemp(prefix='VectorAffinityTcp-'))
    source = Path(__file__).resolve().parents[1] / 'ProjectVectorServer' / 'ProjectVectorServer'
    for name in ['career_seasons.csv','career_rank_tiers.csv','career_items.csv','level_exp_table.csv',
                 'weapon_skins.csv','shop_catalog.csv','SkillPriceTable.csv','CreditRewardTable.csv','affinity_rules.csv']:
        if (source/name).exists(): shutil.copy2(source/name, root/name)
    print('Isolated test data:', root, flush=True)
    clients=[]
    server=None
    with (root/'server.log').open('wb') as log:
        try:
            server=subprocess.Popen([str(exe)],cwd=root,stdout=log,stderr=subprocess.STDOUT,
                env={k.upper():v for k,v in os.environ.items()},creationflags=subprocess.CREATE_NO_WINDOW)
            time.sleep(1.5)
            a,b=Client(801),Client(802);clients=[a,b]
            # Legacy Unity parser remains compatible: byte count + four int/int entries.
            a.send(324)
            legacy=a.wait(325)
            assert len(legacy)==33 and legacy[0]==4
            assert [struct.unpack_from('<ii',legacy,1+i*8) for i in range(4)]==[(i,0) for i in range(4)]
            assert state(a,1,0)[:4]==(0,0,0,100)
            state(a,2,-1,expected=1)
            state(a,3,0,368,0,'H1_1',0,expected=4)
            state(a,4,0,368,0,'S0_1',0,expected=5)
            state(a,5,0,368,0,'H0_3',0,expected=6)
            unchanged={path.name:path.read_bytes() for path in root.glob('*.csv')}
            saved=state(a,6,0,368,0,'H0_1',0)
            assert saved[0]==1 and saved[4]=={'H0_1':0}
            assert state(a,7,0,368,0,'H0_1',0)==saved
            state(a,8,0,368,0,'H0_2',0,expected=7)
            saved=state(a,9,0,368,1,'H0_3',2)
            assert saved[0]==2 and saved[4]['H0_3']==2
            state(a,10,0,368,2,'H0_3',1,expected=8)
            assert state(b,11,0)[4]=={} and state(a,12,1)[4]=={}
            # Affinity never edits legacy account, wallet, mastery, or birthday tables.
            assert all((root/name).read_bytes()==data for name,data in unchanged.items())
            staging=root/'character_affinity_progress.dat.tmp'
            staging.mkdir()
            failed=state(a,13,0,368,2,'H0_2',0,expected=3)
            assert failed[0]==2 and 'H0_2' not in failed[4]
            staging.rmdir()  # Only the empty directory created by this test.
            assert state(a,14,0,368,2,'H0_2',0)[0]==3
            for client in clients: client.sock.close()
            clients=[]
            server.terminate();server.wait(timeout=10);server=None
            server=subprocess.Popen([str(exe)],cwd=root,stdout=log,stderr=subprocess.STDOUT,
                env={k.upper():v for k,v in os.environ.items()},creationflags=subprocess.CREATE_NO_WINDOW)
            time.sleep(1.5)
            from ProfileBirthdayIntegration import connect_existing
            restored=connect_existing(a.user);clients=[restored]
            result=state(restored,15,0)
            assert result[0]==3 and result[4]=={'H0_1':0,'H0_2':0,'H0_3':2}
            print('PASS: legacy packet, per-account/character state, validation, idempotent retry, stale revision, atomic rollback, restart, existing CSV preservation')
        finally:
            for client in clients:
                try: client.sock.close()
                except OSError: pass
            if server is not None:
                server.terminate();server.wait(timeout=10)


if __name__=='__main__': main()
