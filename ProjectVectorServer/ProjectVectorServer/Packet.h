// Packet.h
#pragma once
#include <cstdint>

constexpr int ROOM_NAME_LEN = 32;
constexpr int MAP_NAME_LEN = 32;
constexpr int HOST_NAME_LEN = 20;
constexpr int MAX_ROOM_LIST_COUNT = 32;
constexpr int MAX_LOBBY_CHARACTER_COUNT = 4;

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

// 로비 입장
struct CS_LobbyEnter
{
    PacketHeader header;
};

struct SC_LobbyEnter
{
    PacketHeader header;
    char nickname[20];
    int level;
    int playerIconId;
    int lobbyCharacterModelId;
    int lobbyBackgroundId;
};

// 로비 채팅
struct CS_LobbyChat
{
    PacketHeader header;
    char message[128];
};

struct SC_LobbyChat
{
    PacketHeader header;
    uint32_t playerId;
    char nickname[20];
    char message[128];
};

// 로비 캐릭터 선택
struct CS_LobbyCharacterSelect
{
    PacketHeader header;
    int lobbyCharacterModelId;
};

struct SC_LobbyCharacterSelectResult
{
    PacketHeader header;
    uint8_t result;
    int lobbyCharacterModelId;
};

// 로비 배경 선택
struct CS_LobbyBackgroundSelect
{
    PacketHeader header;
    int lobbyBackgroundId;
};

struct SC_LobbyBackgroundSelectResult
{
    PacketHeader header;
    uint8_t result;
    int lobbyBackgroundId;
};

// 로비 캐릭터 리스트
struct CS_LobbyCharacterList
{
	PacketHeader header;
};

struct LobbyCharacterSlot
{
    int characterId;
	uint8_t owned;
};

struct SC_LobbyCharacterList
{
    PacketHeader header;
    uint16_t count;
	int currentCharacterModelId;
	LobbyCharacterSlot characters[MAX_LOBBY_CHARACTER_COUNT];
};

// 룸 리스트
struct CS_RoomList
{
    PacketHeader header;
};

struct RoomInfo
{
    uint32_t roomId;
    char roomName[ROOM_NAME_LEN];
    char mapName[MAP_NAME_LEN];
    char hostName[HOST_NAME_LEN];
    uint16_t currentPlayerCount;
    uint16_t maxPlayerCount;
    uint8_t state;
};

struct SC_RoomList
{
    PacketHeader header;
    uint16_t roomCount;
    RoomInfo rooms[MAX_ROOM_LIST_COUNT];
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
