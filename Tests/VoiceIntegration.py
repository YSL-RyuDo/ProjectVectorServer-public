"""Isolated real-TCP authorization regression; never connects to Vivox or real accounts.
python -B Tests/VoiceIntegration.py <server.exe>
"""
import base64
import hashlib
import hmac
import json
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

KEY = 'test-only-not-a-vivox-production-key'
serial = 0

def voice(client, action=0, channel='', status=0):
    global serial
    serial += 1
    time.sleep(.14)
    client.send(380, struct.pack('<IB', serial, action) + fixed(channel, 64))
    body = client.wait(381, lambda data: struct.unpack_from('<I', data)[0] == serial)
    assert len(body) == 2726
    assert body[4] == status, (body[4], status)
    def string(offset, size): return body[offset:offset+size].split(b'\0', 1)[0].decode('utf8')
    return dict(scope=body[5], server=string(6,256), domain=string(262,128), issuer=string(390,128),
                account=string(518,64), channel=string(582,64), nickname=string(646,32), token=string(678,2048))

def token(reply, action):
    header, payload, signature = reply['token'].split('.')
    assert header == 'e30'
    expected = base64.urlsafe_b64encode(hmac.new(KEY.encode(), (header+'.'+payload).encode(), hashlib.sha256).digest()).rstrip(b'=')
    assert hmac.compare_digest(signature.encode(), expected)
    claims = json.loads(base64.urlsafe_b64decode(payload + '=' * (-len(payload)%4)))
    assert claims['iss'] == 'vector-test' and claims['vxa'] == action
    assert 0 < claims['exp'] - time.time() <= 60
    assert claims['f'] == 'sip:.vector-test.' + reply['account'] + '.@test.invalid'
    if action == 'join': assert claims['t'] == 'sip:confctl-g-vector-test.' + reply['channel'] + '@test.invalid'
    else: assert 't' not in claims
    return claims

def main():
    exe = Path(sys.argv[1]).resolve()
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(('0.0.0.0', 9000))
    root = Path(tempfile.mkdtemp(prefix='VectorVoiceTcp-'))
    source = Path(__file__).resolve().parents[1]/'ProjectVectorServer'/'ProjectVectorServer'
    for name in ['career_seasons.csv','career_rank_tiers.csv','career_items.csv','level_exp_table.csv',
                 'weapon_skins.csv','shop_catalog.csv','SkillPriceTable.csv','CreditRewardTable.csv']:
        if (source/name).exists(): shutil.copy2(source/name,root/name)
    environment = {k.upper():v for k,v in os.environ.items() if not k.upper().startswith('VECTOR_VIVOX_')}
    environment.update(VECTOR_VIVOX_SERVER='https://test.invalid/api2', VECTOR_VIVOX_DOMAIN='test.invalid',
                       VECTOR_VIVOX_ISSUER='vector-test', VECTOR_VIVOX_SIGNING_KEY=KEY)
    print('Isolated voice tests:',root,flush=True)
    clients=[]
    with (root/'server.log').open('wb') as log:
        server=subprocess.Popen([str(exe)],cwd=root,stdout=log,stderr=subprocess.STDOUT,env=environment,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            time.sleep(1.5)
            # Unauthenticated requests must not mint any token.
            with socket.create_connection(('127.0.0.1',9000),3) as unauth:
                unauth.sendall(struct.pack('<HHIB',73,380,123,1)+bytes(64))
                unauth.settimeout(.5)
                received=b''
                try:
                    while True: received+=unauth.recv(4096)
                except socket.timeout: pass
                offset=0
                while offset+4<=len(received):
                    size,kind=struct.unpack_from('<HH',received,offset)
                    assert kind!=381,'unauthenticated voice token'
                    offset+=size
            clients=[Client(910+i) for i in range(7)]
            a,b,c,d,e,f,outsider=clients
            solo=voice(a); assert solo['scope']==0 and not solo['channel']
            login1=token(voice(a,1),'login');login2=token(voice(a,1),'login'); assert login1['vxi']!=login2['vxi']
            voice(a,2,'forged-channel',2);voice(a,255,status=2)
            # Malformed body must not disturb subsequent legacy or voice packets.
            a.send(380,b'x');assert voice(a)['account']==solo['account']
            a.friend(b);a.friend(c);d.friend(e);d.friend(f)
            a.join(b); a.join(c);d.join(e);d.join(f)
            before=voice(a);assert before['scope']==1
            assert voice(b)['channel']==before['channel']==voice(c)['channel']
            other=voice(d);assert other['channel']!=before['channel']
            token(voice(a,2,before['channel']),'join')
            voice(outsider,2,before['channel'],2);voice(d,2,before['channel'],2)
            # Leave and immediate rejoin of an identical roster cannot reuse old admission tokens.
            b.send(354);b.wait(355,lambda x:x[4]==1)
            voice(b,2,before['channel'],2)
            a.join(b)
            after=voice(a);assert after['channel']!=before['channel']
            voice(a,2,before['channel'],2)
            # Private rooms intentionally have no voice scope.
            outsider.send(22);assert outsider.wait(23)[0]==0
            assert voice(outsider)['scope']==0
            outsider.send(27);outsider.wait(28)
            print('PASS: auth, JWT signature/expiry/identity, solo, party, outsider, stale token, private-room policy',flush=True)
            for leader in (a,d):
                leader.send(361,b'\x01')
                leader.send(16,b'\x01')
            ready=[]
            for peer in clients[:6]:
                peer.wait(17,lambda x:x[1]==0)
                ready.append(struct.unpack_from('<I',peer.wait(357,lambda x:x[4]==0,timeout=15))[0])
            assert len(set(ready))==1
            for peer in clients[:6]:peer.send(356,struct.pack('<IB',ready[0],1))
            for peer in clients[:6]:peer.wait(17,lambda x:x[1]==1)
            team_a=voice(a);team_b=voice(d)
            assert team_a['scope']==team_b['scope']==2 and team_a['channel']!=team_b['channel']
            assert voice(b)['channel']==voice(c)['channel']==team_a['channel']
            assert voice(e)['channel']==voice(f)['channel']==team_b['channel']
            token(voice(a,2,team_a['channel']),'join')
            voice(a,2,team_b['channel'],2);voice(d,2,team_a['channel'],2);voice(a,2,after['channel'],2)
            for peer in clients[:6]:peer.send(48)
            for peer in clients[:6]:peer.wait(49,timeout=12)
            assert voice(a)['channel']==team_a['channel']
            print('PASS: ranked 6-player acceptance/game load unaffected; own-team channel allowed, enemy/old-party denied',flush=True)
        finally:
            for client in clients:client.sock.close()
            server.terminate();server.wait(timeout=10)
        disabled={k:v for k,v in environment.items() if not k.startswith('VECTOR_VIVOX_')}
        server=subprocess.Popen([str(exe)],cwd=root,stdout=log,stderr=subprocess.STDOUT,env=disabled,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            time.sleep(1.5)
            client=Client(999)
            voice(client,status=1)
            client.send(16,b'\0');client.wait(17,lambda x:x[1]==0)
            client.send(61);client.wait(17,lambda x:x[1]==2)
            client.sock.close()
            print('PASS: missing voice configuration does not block login or matchmaking',flush=True)
        finally:server.terminate();server.wait(timeout=10)

if __name__=='__main__':main()
