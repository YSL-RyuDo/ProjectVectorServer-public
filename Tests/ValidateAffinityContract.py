"""Read-only checks of the server contract against existing Unity source assets.
Usage: python -B Tests/ValidateAffinityContract.py <Unity project root>
Does not execute the server, write account data, or modify Unity.
"""
import ast
import csv
import json
from pathlib import Path
import re
import struct
import sys

server = Path(__file__).resolve().parents[1] / 'ProjectVectorServer' / 'ProjectVectorServer'
unity = Path(sys.argv[1]).resolve()
expected = {}
mapping = [(0,'H','Initiator','한별'),(1,'S','Controller','세린'),(2,'B','Sentinel','하은'),(3,'D','Entry','도희')]
for character,prefix,role,name in mapping:
    asset=(unity/f'Assets/KSW/Data/Background Story Data/B_{role}.asset').read_text(encoding='utf-8')
    assert int(re.search(r'characterId: (\d+)',asset)[1])==character
    serialized_name=re.search(r'characterName: (.+)',asset)[1]
    assert json.loads(serialized_name)==name
    with (unity/f'Assets/Resources/Dialogue/{prefix}_dialogue.csv').open(encoding='utf-8-sig',newline='') as source:
        for row in csv.DictReader(source):
            key=(character,row['DialogueID'])
            record=(int(row['FavorLevel']),tuple(row[f'Choice{i}_NextID'] or '' for i in (1,2,3)))
            if key in expected:
                assert expected[key][0]==record[0]
                if any(record[1]):
                    assert not any(expected[key][1]) or expected[key][1]==record[1]
                    expected[key]=record
            else: expected[key]=record
catalog=(server/'AffinityCatalog.h').read_text(encoding='utf-8')
actual={}
pattern=r'\{(\d+), "([A-Za-z0-9_]+)", (\d+), \{"([^"]*)", "([^"]*)", "([^"]*)"\}\}'
for character,id_,level,a,b,c in re.findall(pattern,catalog):
    key=(int(character),id_)
    assert key not in actual
    actual[key]=(int(level),(a,b,c))
assert actual==expected
for (character,id_),(level,branches) in actual.items():
    assert len(id_)<32 and 0<=level<=5
    for branch in branches:
        if branch: assert (character,branch) in actual
for character in range(4):
    assert 31+33*sum(key[0]==character for key in actual)<4096
def ids(path, encoding):
    values=re.findall(r'\b(PKT_\w+)\s*=\s*(\d+)',path.read_text(encoding=encoding))
    return dict((name,int(value)) for name,value in values)
sv=ids(server/'PacketType.h','cp949')
cv=ids(unity/'Assets/Scripts/Network/PacketType.cs','utf-8')
for name in ('PKT_CS_FAVOR_LEVEL_REQUEST','PKT_SC_FAVOR_LEVEL_RESPONSE','PKT_CS_PROFILE_BIRTHDAY_SET',
             'PKT_SC_PROFILE_BIRTHDAY_SET_RESULT','PKT_SC_MATCH_READY_CARDS'):
    assert sv[name]==cv[name]
for number in (324,325,363,364,365,366,367,368,369): assert list(sv.values()).count(number)==1
assert struct.calcsize('<HHIBIiiiiH')==31 and struct.calcsize('<HHIIi32sB')==49
for test in ('AffinityIntegration.py','ValidateAffinityContract.py'):
    ast.parse((server.parents[1]/'Tests'/test).read_text(encoding='utf-8'))
print(f'PASS: {len(actual)} catalog blocks match Unity; 4 character mappings; all choice targets; packet size/bounds; existing IDs unchanged; new IDs unique; test syntax')
