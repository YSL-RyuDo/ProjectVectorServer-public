#pragma once
#include "Packet.h"
#include "PacketType.h"
#include <map>
#include <vector>
#include <chrono>
#include <cstring>
#include <algorithm>

// Latest persistent presentation state, never a replay of damage, rewards or
// player commands. Access is serialized by Session's outbound transaction lock.
class ReconnectJournal
{
    struct Entry { std::vector<char> bytes; std::chrono::steady_clock::time_point at; int durationOffset = -1; };
    std::map<std::pair<uint16_t, uint32_t>, Entry> entries;
    void Remove(uint16_t type, uint32_t id) { entries.erase({type,id}); }
public:
    void Clear() { entries.clear(); }
    void Observe(void* packet)
    {
        auto* h = static_cast<PacketHeader*>(packet);
        uint16_t t = h->type; uint32_t id = 0; int duration = -1;
        auto key = [&]() { uint32_t n=0; if(h->size>=8) memcpy(&n,static_cast<char*>(packet)+4,4); return n; };
        auto erase = [&](std::initializer_list<uint16_t> types) { for(auto type:types) Remove(type,key()); };
        switch(t)
        {
        case 49: case 131: Clear(); return; // New match/round invalidates all world objects.
        case 50: Remove(138,0); break;
        case 138: Remove(50,0); break;
        case 130: case 133: case 137: case 360: break;
        case 140: Remove(142,0); Remove(143,0); break;
        case 142: Remove(140,0); Remove(143,0); break;
        case 143: Remove(140,0); Remove(142,0); break;
        case 147: break;
        case 149: Remove(140,0); Remove(142,0); Remove(143,0); Remove(147,0); return;
        case 163: erase({161}); return;
        case 175: erase({173,174,189}); return;
        case 195: erase({191,193}); return;
        case 234: erase({231,233,236}); return;
        case 264: erase({261,263}); return;
        case 272: erase({271}); return;
        case 284: erase({281,283}); return;
        case 292: erase({291}); return;
        case 307: erase({301,303,305}); return;
        case 313: erase({309,311}); return;
        case 320: erase({315,317}); return;
        case 323: erase({322}); return;
        case 161: case 173: case 174: case 189: case 191: case 193:
        case 231: case 236: case 261: case 281: case 301: case 303:
        case 309: case 315: id=key(); break;
#define TIMED_PACKET(number, structure, field) case number: id=key(); duration=offsetof(structure,field); break
        TIMED_PACKET(202,SC_ElectricStatus,stunSeconds);
        TIMED_PACKET(222,SC_BeastStealthState,duration);
        TIMED_PACKET(233,SC_EntrySmokeActivate,duration);
        TIMED_PACKET(251,SC_EntryBladeState,duration);
        TIMED_PACKET(263,SC_ControllerSwampActivate,duration);
        TIMED_PACKET(271,SC_ControllerSmokeSpawn,duration);
        TIMED_PACKET(283,SC_ControllerDecoyActivate,duration);
        TIMED_PACKET(291,SC_ControllerUltimateSpawn,duration);
        TIMED_PACKET(305,SC_SentinelCTrigger,revealDuration);
        TIMED_PACKET(311,SC_SentinelQActivate,duration);
        TIMED_PACKET(317,SC_SentinelEActivate,slowDuration);
        TIMED_PACKET(322,SC_SentinelXSpawn,duration);
#undef TIMED_PACKET
        default: return;
        }
        if(entries.size()>2048) return; // Bounded independently of connection duration.
        Entry e; e.bytes.assign(static_cast<char*>(packet),static_cast<char*>(packet)+h->size);
        e.at=std::chrono::steady_clock::now(); e.durationOffset=duration;
        entries[{t,id}]=std::move(e);
    }
    std::vector<std::vector<char>> Snapshot() const
    {
        std::vector<std::vector<char>> result;
        for(const auto& pair:entries)
        {
            auto bytes=pair.second.bytes;
            if(pair.second.durationOffset>=0)
            {
                float original=0; memcpy(&original,bytes.data()+pair.second.durationOffset,4);
                float age=std::chrono::duration<float>(std::chrono::steady_clock::now()-pair.second.at).count();
                float remaining=std::max(0.f,original-age);
                memcpy(bytes.data()+pair.second.durationOffset,&remaining,4);
                // Zero-duration status packets also clear stale local states.
            }
            result.push_back(std::move(bytes));
        }
        return result;
    }
};
