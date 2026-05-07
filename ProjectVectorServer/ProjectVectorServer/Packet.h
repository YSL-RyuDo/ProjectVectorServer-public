// Packet.h
#pragma once
#include <cstdint>

constexpr int ROOM_NAME_LEN = 32;
constexpr int MAP_NAME_LEN = 32;
constexpr int HOST_NAME_LEN = 20;
constexpr int MAX_ROOM_LIST_COUNT = 32;
constexpr int MAX_LOBBY_CHARACTER_COUNT = 4;
constexpr int TEMP_GAME_MAX_PLAYER_COUNT = 3;

constexpr int ROOM_A_TEAM_COUNT = 3;
constexpr int ROOM_B_TEAM_COUNT = 3;
constexpr int ROOM_OBSERVER_COUNT = 2;
constexpr int ROOM_MAX_MEMBER_COUNT = ROOM_A_TEAM_COUNT + ROOM_B_TEAM_COUNT + ROOM_OBSERVER_COUNT;

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
    int currentExp;
    int requiredExp;

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

struct CS_TempGameRoomEnter
{
    PacketHeader header;
};

struct TempGamePlayerSlotInfo
{
    uint8_t slotIndex;      // 0,1,2
    char nickname[20];
    int playerIconId;       // 로고 이미지
    int logoBorderId;       // 임시값
    int rankImageId;        // 임시값
};

struct SC_TempGameRoomEnter
{
    PacketHeader header;
    uint8_t result;         // 0 = 성공, 1 = 실패
    uint8_t playerCount;    // 현재 방 인원 수
    TempGamePlayerSlotInfo players[TEMP_GAME_MAX_PLAYER_COUNT];
};

// 임시 게임 시작
struct CS_TempGameStart
{
    PacketHeader header;
};

struct SC_TempGameStartResult
{
    PacketHeader header;
    uint8_t result;         // 0 = 성공, 1 = 실패
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

enum class RoomCreateResult : uint8_t
{
    SUCCESS = 0,
    NOT_IN_LOBBY = 1,
    ALREADY_IN_ROOM = 2,
    PLAYER_DATA_NOT_FOUND = 3,
    CREATE_FAILED = 4
};

enum class RoomSlotTeam : uint8_t
{
    A = 0,
    B = 1,
    OBSERVER = 2
};

struct CS_RoomCreate
{
    PacketHeader header;
};

struct RoomMemberInfo
{
    uint32_t playerId;
    char nickname[20];
    int level;
    int playerIconId;

    uint8_t team;
    uint8_t slotIndex;
    uint8_t isLeader; // 1 = 리더, 0 = 일반 유저
};

struct SC_RoomCreateResult
{
    PacketHeader header;

    uint8_t result;

    uint32_t roomId;
    char roomName[ROOM_NAME_LEN];
    char mapName[MAP_NAME_LEN];

    uint8_t isLeader;
    uint8_t canStart;

    uint8_t memberCount;
    RoomMemberInfo members[ROOM_MAX_MEMBER_COUNT];
};

struct SC_RoomState
{
    PacketHeader header;
    uint32_t roomId;
    char roomName[ROOM_NAME_LEN];
    char mapName[MAP_NAME_LEN];

    uint8_t canStart;

    uint8_t memberCount;
    RoomMemberInfo members[ROOM_MAX_MEMBER_COUNT];
};

struct CS_RoomGameStart
{
    PacketHeader header;
};

struct SC_RoomGameStartResult
{
    PacketHeader header;
    uint8_t result; // 0 = 성공, 1 = 리더 아님, 2 = 인원 부족, 3 = 방 없음
};

enum class RoomLeaveResult : uint8_t
{
    SUCCESS = 0,
    NOT_IN_ROOM = 1,
    ROOM_NOT_FOUND = 2
};

struct CS_RoomLeave
{
    PacketHeader header;
};

struct SC_RoomLeaveResult
{
    PacketHeader header;
    uint8_t result;
    uint32_t roomId;
    uint8_t roomDeleted;
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
