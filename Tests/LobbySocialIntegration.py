"""Real TCP regression test. All generated account data lives in a fresh temp dir.
Usage: python Tests/LobbySocialIntegration.py path/to/ProjectVectorServer.exe
Never starts if the project's port is already occupied.
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

def fixed(value, size):
    return value.encode('utf8').ljust(size, b'\0')

class Client:
    def __init__(self, index):
        self.sock = socket.create_connection(('127.0.0.1', 9000), 3)
        self.pending = []
        self.user = 'socialtest' + str(index)
        password = fixed('TestOnly123', 64)
        self.send(0, fixed(self.user, 20) + fixed('Tester' + str(index), 20) + password + password)
        assert self.wait(1)[0] == 0, 'register'
        self.send(2, fixed(self.user, 20) + password)
        assert self.wait(3)[0] == 0, 'login'
        self.send(4)
        lobby = self.wait(5)
        assert struct.unpack_from('<ii', lobby, 44) == (0, 0), 'non-qwer wallet must stay zero'
        self.send(358)
        state = self.wait(355)
        self.id = struct.unpack_from('<I', state)[0]

    def send(self, kind, body=b''):
        self.sock.sendall(struct.pack('<HH', len(body) + 4, kind) + body)

    def exact(self, size):
        result = b''
        while len(result) < size:
            part = self.sock.recv(size-len(result))
            if not part: raise RuntimeError('disconnected')
            result += part
        return result

    def wait(self, kind, predicate=lambda b: True, timeout=5):
        end = time.monotonic() + timeout
        while True:
            for i, (packet, body) in enumerate(self.pending):
                if packet == kind and predicate(body):
                    self.pending.pop(i)
                    return body
            remaining = end - time.monotonic()
            if remaining <= 0: raise TimeoutError(f'{self.user} waiting for {kind}; buffered {[p[0] for p in self.pending]}')
            self.sock.settimeout(remaining)
            length, packet = struct.unpack('<HH', self.exact(4))
            body = self.exact(length-4)
            if packet == kind and predicate(body): return body
            self.pending.append((packet, body))

    def result(self, expected):
        body = self.wait(353)
        assert body[0] == expected, (self.user, body[0], expected)

    def friend(self, other):
        self.send(74, b'\0' + fixed(other.user, 20))
        assert self.wait(75)[1] == 0
        other.send(74, b'\x02' + fixed(self.user, 20))
        assert other.wait(75)[1] == 0

    def invite(self, other):
        self.send(350, struct.pack('<I', other.id))
        self.result(0)
        return struct.unpack_from('<I', other.wait(351))[0]

    def join(self, other):
        token = self.invite(other)
        other.send(352, struct.pack('<IB', token, 1))
        other.result(1)
        self.result(1)
        return token

def main():
    exe = Path(sys.argv[1]).resolve()
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(('0.0.0.0', 9000))
    root = Path(tempfile.mkdtemp(prefix='VectorSocialTest-'))
    source = Path(__file__).resolve().parents[1] / 'ProjectVectorServer' / 'ProjectVectorServer'
    for name in ['career_seasons.csv', 'career_rank_tiers.csv', 'career_items.csv', 'level_exp_table.csv', 'weapon_skins.csv', 'shop_catalog.csv', 'SkillPriceTable.csv', 'CreditRewardTable.csv']:
        if (source/name).exists(): shutil.copy2(source/name, root/name)
    print('Isolated test data:', root, flush=True)
    clients = []
    with (root/'server.log').open('wb') as log:
        server = subprocess.Popen([str(exe)], cwd=root, stdout=log, stderr=subprocess.STDOUT,
                                  env={k.upper(): v for k,v in os.environ.items()}, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            time.sleep(1.5)
            for i in range(7): clients.append(Client(i))
            a,b,c,d,e,f,outsider = clients
            a.send(350, struct.pack('<I', outsider.id)); a.result(5)
            a.friend(b); a.friend(c); d.friend(e); d.friend(f)
            b.send(68, b'\x01'); b.wait(69)
            a.send(350, struct.pack('<I', b.id)); a.result(6)
            b.send(68, b'\0'); b.wait(69)
            token = a.invite(b)
            a.send(350, struct.pack('<I', b.id)); a.result(4)
            outsider.send(352, struct.pack('<IB', token, 1)); outsider.result(7)
            b.send(352, struct.pack('<IB', token, 0)); a.result(2)
            token = a.join(b)
            b.send(352, struct.pack('<IB', token, 1)); b.result(7)
            b.send(350, struct.pack('<I', c.id)); b.result(9)
            a.join(c)
            a.send(350, struct.pack('<I', outsider.id)); a.result(8)
            d.join(e); d.join(f)
            a.send(14)
            cards = a.wait(15, lambda x: x[1] == 3)
            assert len(cards) == 272, 'existing card/preset packet layout preserved'
            assert [cards[2+i*90] for i in range(3)] == [0,1,2]
            b.send(16,b'\0'); b.result(9)
            a.send(16,b'\0')
            for peer in (a,b,c): peer.wait(17, lambda x: x[1] == 0)
            c.send(354); c.result(4)
            c.send(61)
            for peer in (a,b,c): peer.wait(17, lambda x: x[1] == 2)
            print('PASS: friend/DND gates, duplicate/forged/replayed invite, capacity, 3 cards, leader-only start, party-wide cancellation', flush=True)

            def ready_group():
                a.send(16,b'\0'); d.send(16,b'\0')
                for peer in clients[:6]: peer.wait(17, lambda x: x[1] == 0)
                tokens = [struct.unpack_from('<I', peer.wait(357, lambda x: x[4] == 0, 12))[0] for peer in clients[:6]]
                assert len(set(tokens)) == 1
                return tokens[0]

            token = ready_group()
            outsider.send(356,struct.pack('<IB', token, 1)); outsider.result(3)
            a.send(22); a.result(4)
            b.send(356,struct.pack('<IB', token, 0))
            for peer in clients[:6]:
                peer.wait(357, lambda x: x[4] == 2)
                peer.wait(17, lambda x: x[1] == 2)
                peer.pending = [p for p in peer.pending if p[0] != 357]
            token = ready_group()
            a.send(356,struct.pack('<IB', token, 1)); a.send(356,struct.pack('<IB', token, 1))
            ack = a.wait(357, lambda x: x[8] == 1 and x[9] == 1)
            assert ack[7] == 6
            for peer in clients[:6]:
                peer.wait(357, lambda x: x[4] == 2, timeout=34)
                peer.wait(17, lambda x: x[1] == 2)
                peer.pending = [p for p in peer.pending if p[0] != 357]
            print('PASS: six-player ready gate, forged reply, room-entry exclusion, decline, duplicate confirmation, 30-second timeout', flush=True)
            token = ready_group()
            for peer in clients[:6]: peer.send(356, struct.pack('<IB', token, 1))
            room_ids = []
            for peer in clients[:6]:
                peer.wait(357, lambda x: x[4] == 1)
                found = peer.wait(17, lambda x: x[1] == 1)
                room_ids.append(struct.unpack_from('<I', found, 6)[0])
            assert len(set(room_ids)) == 1 and room_ids[0] > 0
            for peer in clients[:6]: peer.send(48)
            team_maps = []
            for peer in clients[:6]:
                loaded = peer.wait(49, timeout=12)
                assert loaded[38] == 6 and len(loaded) == 39 + 6 * 110
                teams = {struct.unpack_from('<I', loaded, 39+i*110)[0]: loaded[39+i*110+44] for i in range(6)}
                assert teams[a.id] == teams[b.id] == teams[c.id], 'party A split across teams'
                assert teams[d.id] == teams[e.id] == teams[f.id], 'party B split across teams'
                assert teams[a.id] != teams[d.id], 'opposing parties must occupy different teams'
                team_maps.append(teams)
            assert all(teams == team_maps[0] for teams in team_maps)
            print('PASS: actual game-load packets on all six clients keep each invited party on the same team', flush=True)
            print('PASS: all six confirmations create exactly one existing 3v3 match; non-qwer currency unchanged', flush=True)
        finally:
            for client in clients: client.sock.close()
            if server.poll() is not None:
                print('Server exited:', hex(server.returncode & 0xffffffff), flush=True)
            else:
                server.terminate()
            server.wait(timeout=10)
    print('PASS: test server stopped; production accounts untouched. Logs:', root, flush=True)

if __name__ == '__main__': main()
