#pragma once
#include "Packet.h"
#include <cstdint>

// Little-endian. No native bool/string/padding on the wire.
#pragma pack(push, 1)
struct CS_FavorLevelRequest { PacketHeader header; };
struct FavorLevelEntry { int32_t characterId; int32_t level; };
struct SC_FavorLevels { PacketHeader header; uint8_t count; FavorLevelEntry characters[4]; };
struct CS_AffinityStateRequest { PacketHeader header; uint32_t requestId; int32_t characterId; };
struct CS_AffinityDialogueComplete
{
    PacketHeader header; uint32_t requestId; uint32_t expectedRevision;
    int32_t characterId; char dialogueId[32]; uint8_t choice;
};
struct AffinityDialogueEntry { char dialogueId[32]; uint8_t choice; };
// Followed by dialogueCount AffinityDialogueEntry objects, only for characterId.
// Used by both SC_AFFINITY_STATE and SC_AFFINITY_DIALOGUE_RESULT.
struct AffinityStateHeader
{
    PacketHeader header; uint32_t requestId; uint8_t result; uint32_t revision;
    int32_t characterId; int32_t level; int32_t points; int32_t nextLevelPoints;
    uint16_t dialogueCount;
};
#pragma pack(pop)
static_assert(sizeof(CS_FavorLevelRequest) == 4);
static_assert(sizeof(SC_FavorLevels) == 37);
static_assert(sizeof(CS_AffinityStateRequest) == 12);
static_assert(sizeof(CS_AffinityDialogueComplete) == 49);
static_assert(sizeof(AffinityStateHeader) == 31);
static_assert(sizeof(AffinityDialogueEntry) == 33);
