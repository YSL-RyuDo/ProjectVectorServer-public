#pragma once
#include "Packet.h"
#include <cstddef>

namespace CombatReport
{
    constexpr uint16_t RequestType = 370;
    constexpr uint16_t ResponseType = 371;
    constexpr size_t PageSize = 8;
    constexpr size_t MaxEntries = 128;
    // CS_Hit stays 9 bytes: its hitZone now accepts Body/Head/Legs only.
    // None is emitted by the server for skill report events, never by CS_Hit.
    enum class Region : uint8_t { Body = 0, Head = 1, Legs = 2, None = 3 };
    enum class Source : uint8_t { Weapon = 0, Skill = 1 };
    enum class Direction : uint8_t { Dealt = 0, Received = 1 };
    enum class Status : uint8_t { Ok = 0, Unavailable = 1, Stale = 2, InvalidPage = 3 };
    // Stable report-only IDs, not skill packet IDs or weapon IDs.
    enum class Skill : int32_t { DroneTaser = 1, InitiatorUltimate = 2, EntryBlade = 3, SentinelUltimate = 4 };

#pragma pack(push, 1)
    struct Request
    {
        PacketHeader header;
        uint32_t requestId;
        uint32_t roomId;
        uint32_t lifeId; // 0 only for initial page 0; copy response token for later pages.
        uint16_t pageIndex;
    };
    struct Entry
    {
        uint32_t opponentId;
        int32_t characterId;
        char nickname[20]; // Same encoding as SC_Die; NUL terminated.
        uint8_t sourceKind;
        uint8_t direction;
        uint16_t reserved;
        int32_t sourceId;
        uint32_t hpDamage;
        uint32_t barrierDamage;
        uint32_t hits[4]; // body, head, legs, unlocalized. Skill ticks are events, not bullets.
    };
    struct Response
    {
        PacketHeader header;
        uint32_t requestId;
        uint8_t version;
        uint8_t status;
        uint8_t flags; // bit 0: frozen; bit 1: capacity/counter truncation.
        uint8_t entryCount;
        uint32_t roomId;
        uint32_t lifeId;
        uint32_t killerId;
        uint16_t pageIndex;
        uint16_t pageCount;
        uint16_t totalEntries;
        uint16_t reserved;
        Entry entries[PageSize];
    };
#pragma pack(pop)
    static_assert(sizeof(Request) == 18);
    static_assert(sizeof(Entry) == 60);
    static_assert(sizeof(Response) == 512);
    static_assert(offsetof(Response, entries) == 32);
    static_assert(offsetof(Entry, hits) == 44);
}
