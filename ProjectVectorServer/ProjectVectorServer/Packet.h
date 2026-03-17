// Packet.h
#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct PacketHeader
{
    uint16_t size;
    uint16_t type;
};

struct CS_Register
{
    PacketHeader header;
    char id[20];
    char nickname[20];
    char password[64];
    char confirmPassword[64];
};

struct SC_RegisterResult
{
    PacketHeader header;
    uint8_t result;
};

struct CS_Login
{
    PacketHeader header;
    char id[20];
    char password[64];
};

struct SC_LoginResult
{
    PacketHeader header;
    uint8_t result;
};

struct SC_Hello
{
    PacketHeader header;
    uint32_t myPlayerId;
};

struct SC_SpawnPlayer
{
    PacketHeader header;
    uint32_t playerId;
    float x;
    float y;
    float z;
    int hp;
};

struct SC_DespawnPlayer
{
    PacketHeader header;
    uint32_t playerId;
};

struct CS_MoveState
{
    PacketHeader header;
    float x;
    float y;
    float z;
    float yaw;
};

struct SC_MoveState
{
    PacketHeader header;
    uint32_t playerId;
    float x;
    float y;
    float z;
    float yaw;
};

struct CS_Hit
{
    PacketHeader header;
    uint32_t targetId;
    int damage;
};

struct SC_HPUpdate
{
    PacketHeader header;
    uint32_t playerId;
    int hp;
};

struct SC_Die
{
    PacketHeader header;
    uint32_t playerId;
};

#pragma pack(pop)
