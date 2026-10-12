"""Read-only protocol/wiring checks; never starts a server or touches account data."""
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parents[1]
server = root / 'ProjectVectorServer' / 'ProjectVectorServer'
types = (server / 'PacketType.h').read_text(encoding='cp949')
dispatcher = (server / 'PacketDispatcher.cpp').read_text(encoding='cp949')
session = (server / 'Session.cpp').read_text(encoding='cp949')
session_header = (server / 'Session.h').read_text(encoding='utf-8')
packets = (server / 'CombatReportPackets.h').read_text(encoding='utf-8')
service = (server / 'CombatReportService.h').read_text(encoding='utf-8')
docs = (root / 'Docs' / 'CombatReportProtocol.md').read_text(encoding='utf-8')
values = re.findall(r'(PKT_\w+)\s*=\s*(\d+)', types)
assert len({int(value) for _, value in values}) == len(values), 'packet ID collision'
assert dict(values)['PKT_CS_COMBAT_REPORT_REQUEST'] == '370'
assert dict(values)['PKT_SC_COMBAT_REPORT'] == '371'
request = struct.Struct('<HHIIIH')
entry = struct.Struct('<Ii20sBBHiII4I')
response_header = struct.Struct('<HHIBBBBIIIHHHH')
assert request.size == 18 and entry.size == 60 and response_header.size == 32
assert response_header.size + entry.size * 8 == 512
golden = request.pack(18,370,1,42,0,0)
assert golden.hex(' ').upper() == '12 00 72 01 01 00 00 00 2A 00 00 00 00 00 00 00 00 00'
assert golden.hex(' ').upper() in docs
assert 'sizeof(Request) == 18' in packets and 'sizeof(Response) == 512' in packets
assert 'offsetof(Entry, hits) == 44' in packets
assert dispatcher.count('CombatReport::ApplyDamage(') == 5, 'all five existing damage paths must be wired'
assert 'target->Damage(' not in dispatcher, 'new damage path bypasses report recorder'
for skill in ('DroneTaser','InitiatorUltimate','EntryBlade','SentinelUltimate'):
    assert f'CombatReport::Skill::{skill}' in dispatcher
assert dispatcher.count('case PacketType::PKT_CS_COMBAT_REPORT_REQUEST:') == 1
assert 'header->size != sizeof(CS_Hit)' in dispatcher and 'pkt->hitZone > 2' in dispatcher
assert session.count('combatReport_.Reset(currentRoomId_)') == 2
assert 'combatReport_.Reset(roomId)' in session_header
assert 'session->Send(&response)' in service and 'BroadcastPacket' not in service
assert 'session->IsInGame() && session->IsDead()' in service
assert 'member.team != RoomTeam::OBSERVER' in service
assert 'const int applied = target->Damage(amount)' in service
assert 'Source::Skill) region = Region::None' in service
print('COMBAT_REPORT_CONTRACT_PASS: IDs, 18/60/32/512-byte layouts, golden request, five damage paths, owner-only delivery, reset wiring')
