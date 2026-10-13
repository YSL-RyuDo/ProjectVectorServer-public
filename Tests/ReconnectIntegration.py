"""Real TCP reconnect regression, isolated accounts/files/port. No live data mutations."""
import os, sys, socket, struct, subprocess, tempfile, shutil, time
from pathlib import Path
sys.dont_write_bytecode=True
from LobbySocialIntegration import Client, fixed

PORT=19013
class Peer(Client):
    def __init__(self,index,register=True):
        self.user='reconnect'+str(index);self.pending=[]
        self.sock=socket.create_connection(('127.0.0.1',PORT),3)
        if register:
            pw=fixed('OnlyLocalQA123',64)
            self.send(0,fixed(self.user,20)+fixed('Reconnect '+str(index),20)+pw+pw)
            assert self.wait(1)[0]==0
        self.send(2,fixed(self.user,20)+fixed('OnlyLocalQA123',64));assert self.wait(3)[0]==0
        if register:
            self.send(4);self.id=struct.unpack_from('<I',self.wait(5),52)[0]
        else:
            offer=self.wait(390);self.id=struct.unpack_from('<I',offer)[0]

def restore(peer):
    # A forged early acknowledgement cannot unlock gameplay.
    peer.send(395);peer.send(110,struct.pack('<fffffBB',900,900,900,0,0,0,0))
    peer.send(391,b'\1');assert peer.wait(392)[0]==1
    ready=peer.wait(49);assert ready[38]==6
    peer.send(391,b'\2');assert peer.wait(392)[0]==2
    reset=peer.wait(131);poses=peer.wait(393);peer.wait(394)
    assert reset[3]==6 and poses[0]<=6
    members={struct.unpack_from('<I',reset,4+i*85)[0]:reset[4+i*85:4+(i+1)*85] for i in range(6)}
    positions={struct.unpack_from('<I',poses,1+i*24)[0]:struct.unpack_from('<fffff',poses,5+i*24) for i in range(poses[0])}
    peer.send(395);peer.wait(396)
    return members,positions

def main():
    root=Path(tempfile.mkdtemp(prefix='VectorReconnectTcp-'))
    source=Path(__file__).resolve().parents[1]/'ProjectVectorServer'/'ProjectVectorServer'
    for name in ['career_seasons.csv','career_rank_tiers.csv','career_items.csv','level_exp_table.csv','weapon_skins.csv','shop_catalog.csv','SkillPriceTable.csv','CreditRewardTable.csv']:
        if (source/name).exists():shutil.copy2(source/name,root/name)
    with socket.socket() as probe:probe.bind(('127.0.0.1',PORT))
    env={k.upper():v for k,v in os.environ.items() if not k.upper().startswith('VECTOR_VIVOX_')};env['VECTOR_QA_PORT']=str(PORT)
    peers=[]
    print('Isolated reconnect tests:',root,flush=True)
    with (root/'server.log').open('wb') as log:
        server=subprocess.Popen([str(Path(sys.argv[1]).resolve())],cwd=root,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            time.sleep(1)
            peers=[Peer(i) for i in range(6)]
            for p in peers:p.send(16,b'\1')
            token=0
            for p in peers:token=struct.unpack_from('<I',p.wait(357,lambda b:b[4]==0,15))[0]
            for p in peers:p.send(356,struct.pack('<IB',token,1))
            for p in peers:
                try:p.wait(17,lambda b:b[1]==1,timeout=15)
                except Exception:
                    print('Ready diagnostic',p.user,[(k,b.hex() if k in (17,353,357) else len(b)) for k,b in p.pending],flush=True)
                    raise
                p.send(48)
            roster=peers[0].wait(49,timeout=15)
            for p in peers[1:]:p.wait(49,timeout=15)
            teams={struct.unpack_from('<I',roster,39+i*110)[0]:roster[39+i*110+44] for i in range(6)}
            a=peers[0];original=a.id;enemy=next(p for p in peers if teams[p.id]!=teams[a.id])
            a.send(110,struct.pack('<fffffBB',11,2,13,75,4,0,0));a.wait(111,lambda b:struct.unpack_from('<I',b)[0]==a.id)
            a.send(126);ammo=a.wait(123,lambda b:struct.unpack_from('<I',b)[0]==a.id)
            enemy.send(120,struct.pack('<IB',a.id,0));health=struct.unpack_from('<i',enemy.wait(121,lambda b:struct.unpack_from('<I',b)[0]==a.id and struct.unpack_from('<i',b,4)[0]<100),4)[0]
            assert 0<health<100
            a.sock.close();time.sleep(.3)
            # Other clients continue receiving server round timing while offline.
            peers[1].wait(50,timeout=5)
            resumed=Peer(0,False);peers[0]=resumed
            assert resumed.id==original
            members,positions=restore(resumed);me=members[original]
            assert struct.unpack_from('<i',me,7)[0]==health,('health reset',health,me)
            assert struct.unpack_from('<ii',me,27)==struct.unpack_from('<ii',ammo,8),'ammo reset'
            assert positions[original][:4]==(11.,2.,13.,75.),positions[original]
            print('PASS: same identity/room, 6 players, position, damaged HP, spent ammo, early-ACK/input blocked',flush=True)
            resumed.sock.close();time.sleep(.3)
            # The offline actor remains damageable; disconnect does not avoid death.
            for _ in range(5):enemy.send(120,struct.pack('<IB',original,0))
            enemy.wait(121,lambda b:struct.unpack_from('<Ii',b)==(original,0))
            resumed=Peer(0,False);peers[0]=resumed
            members,_=restore(resumed)
            assert struct.unpack_from('<i',members[original],7)[0]==0
            print('PASS: second reconnect after offline death remains dead; no fresh actor or resurrection',flush=True)
            # Re-login on another live transport is rejected, not a session takeover.
            with socket.create_connection(('127.0.0.1',PORT),3) as duplicate:
                duplicate.sendall(struct.pack('<HH',88,2)+fixed(resumed.user,20)+fixed('OnlyLocalQA123',64))
                duplicate.settimeout(3)
                length,kind=struct.unpack('<HH',duplicate.recv(4));body=duplicate.recv(length-4)
                assert kind==3 and body[0]!=0
            print('PASS: duplicate authenticated login rejected; original production server/data untouched',flush=True)
            if '--round-transition' in sys.argv:
                resumed.sock.close();time.sleep(.3)
                # Advance a normal-rules match while one teammate is offline.
                enemy.wait(50,lambda b:b[0]==0,timeout=25)
                for target in peers:
                    if target.id!=original and teams[target.id]==teams[original]:
                        for _ in range(5):enemy.send(120,struct.pack('<IB',target.id,0))
                enemy.wait(131,lambda b:b[0]>=2,timeout=20)
                resumed=Peer(0,False);peers[0]=resumed
                members,positions=restore(resumed)
                assert struct.unpack_from('<i',members[original],7)[0]==100
                assert original not in positions,'previous-round position must not override the new map spawn'
                print('PASS: offline round transition uses new-round health/loadout and map spawn, not stale pose',flush=True)
        finally:
            for p in peers:p.sock.close()
            server.terminate();server.wait(timeout=10)

if __name__=='__main__':main()
