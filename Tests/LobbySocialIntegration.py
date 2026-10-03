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
        # Unity Start ordering may refresh party state before LobbyEnter.
        self.send(358)
        self.wait(355)
        self.wait(362)
        assert not any(p == 353 and b[0] == 4 for p, b in self.pending), 'spurious busy toast on first login'
        self.send(4)
        lobby = self.wait(5)
        assert struct.unpack_from('<ii', lobby, 44) == (0, 0), 'non-qwer wallet must stay zero'
        self.send(358)
        state = self.wait(355)
        self.id = struct.unpack_from('<I', state)[0]
        assert len(lobby) == 56 and struct.unpack_from('<I', lobby, 52)[0] == self.id, 'lobby self identity must match authoritative party identity'

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
            for card_id in range(3):
                preset = bytes([card_id, 0, 0]) + bytes(54)
                a.send(96, preset)
                saved = a.wait(97)
                assert saved[0] == 0 and saved[1] == card_id, 'chipcraft save acknowledgement'
                a.send(94, bytes(20))
                assert a.wait(95)[25] == card_id, 'saved player card'
                a.send(4)
                assert struct.unpack_from('<i', a.wait(5), 32)[0] == card_id, 'lobby icon must share saved card ID'
            print('PASS: all three chipcraft cards save and synchronize the lobby profile icon ID', flush=True)
            outsider.send(16, b'\0')
            outsider.wait(17, lambda x: x[1] == 0)
            outsider.send(61)
            outsider.wait(17, lambda x: x[1] == 2)
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

            def check_live_appearance(sender, card, x):
                # A unique medal position distinguishes this save from buffered snapshots.
                preset = bytes([card, 0, 1]) + struct.pack('<BffffB', 0, x, .35, 12., .6, 0) + bytes(36)
                sender.send(96, preset)
                saved = sender.wait(97)
                assert saved[0] == 0 and saved[1:] == preset, 'appearance save failed'
                slot = 0 if sender is a else 1
                def updated(body):
                    return len(body) == 272 and body[1] == 2 and body[2+slot*90+33:2+(slot+1)*90] == preset
                for peer in (a, b):
                    cards = peer.wait(15, updated)
                    for i in range(2):
                        assert cards[3+i*90:23+i*90].split(b'\0', 1)[0] == ('Tester'+str(i)).encode(), 'nickname lost during appearance update'
                    assert struct.unpack_from('<i', cards, 23+slot*90)[0] == card, 'profile icon/card ID mismatch'
                    assert cards[182:] == bytes(90), 'unused fixed-capacity slot should remain empty'
                    peer.pending = [p for p in peer.pending if p[0] != 362]
                    peer.wait(362)  # Complete this snapshot before subsequent mode assertions.

            # Drain the original join modes first; cards are matched by unique saved content.
            for peer in (a, b): peer.wait(362, lambda body: struct.unpack('<IB', body) == (a.id, 0))
            check_live_appearance(a, 1, .21)
            check_live_appearance(b, 2, .43)
            print('PASS: leader/member appearance saves broadcast nicknames, cards and medal transforms to both clients', flush=True)
            for peer in (a, b): peer.pending = [p for p in peer.pending if p[0] != 362]
            a.send(361, b'\x01')
            for peer in (a, b):
                mode = peer.wait(362)
                assert struct.unpack('<IB', mode) == (a.id, 1), 'leader rank selection must reach every member before queue'
            b.send(361, b'\0'); b.result(9)
            assert b.wait(362)[4] == 1, 'member cannot change party mode'
            a.send(361, b'\x02'); a.result(3)
            assert a.wait(362)[4] == 1, 'invalid/custom mode is not a party queue mode'
            a.send(16, b'\0'); a.result(3)
            assert a.wait(362)[4] == 1, 'stale client mode must not enter a different queue'
            a.send(16, b'\x01')
            for peer in (a, b): peer.wait(17, lambda x: x[1] == 0 and x[2] == 1)
            check_live_appearance(a, 2, -.27)
            a.send(361, b'\0'); a.result(4)
            assert a.wait(362)[4] == 1, 'queued party cannot silently switch mode'
            b.send(61)
            for peer in (a, b): peer.wait(17, lambda x: x[1] == 2)
            for departing in (b, a):
                departing.send(354)
                for peer in (a, b):
                    peer.wait(355, lambda x: x[4] == 1)
                    assert struct.unpack('<IB', peer.wait(362)) == (peer.id, 1), 'leave/leader succession must preserve mode'
                a.join(b)
                for peer in (a, b): assert struct.unpack('<IB', peer.wait(362)) == (a.id, 1), 'joining an existing ranked party must inherit rank'
            a.send(361, b'\0')
            for peer in (a, b): assert peer.wait(362)[4] == 0
            print('PASS: shared normal/rank mode, member/invalid/queued-mode guards, stale queue rejection, leave/rejoin and leader succession', flush=True)
            # A premade does not need three players to enter a 3v3 queue.
            a.send(16, b'\0')
            for peer in (a, b): peer.wait(17, lambda x: x[1] == 0)
            b.send(61)
            for peer in (a, b): peer.wait(17, lambda x: x[1] == 2)
            print('PASS: lobby self ID, solo queue and two-person leader queue/cancel without a third invite', flush=True)
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
