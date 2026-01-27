#pragma once
#include "Common.h"

#pragma pack(push, 1)
struct PacketHeader
{
    uint16_t size;
    uint16_t type;
};

struct SC_SpawnPlayer
{
    PacketHeader header;
    uint32_t playerId;
    float x;
    float y;
    float z;
};

struct CS_Move
{
    PacketHeader header;
    float inputX;
    float inputZ;
    float yaw;
};

struct SC_Move
{
    PacketHeader header;
    uint32_t playerId;
    float x;
    float y;
    float z;
    float yaw;
};

struct CS_Rotate
{
    PacketHeader header;
    float yaw;
};

struct SC_Rotate
{
    PacketHeader header;
    uint32_t playerId;
    float yaw;
};

#pragma pack(pop)