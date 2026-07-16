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
constexpr int ROOM_MAX_MEMBER_COUNT =
ROOM_A_TEAM_COUNT + ROOM_B_TEAM_COUNT + ROOM_OBSERVER_COUNT;

constexpr int MATCH_MEMBER_NICKNAME_LEN = 20;
constexpr int MATCH_TEAM_MEMBER_COUNT = 3;

constexpr int GAME_MEMBER_NICKNAME_LEN = 20;
constexpr int USER_ID_LEN = 20;
constexpr int GAME_MEMBER_COUNT = 6;
constexpr int GAME_READY_MEMBER_COUNT = 6;
constexpr int GAME_READY_NICKNAME_LEN = 20;
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
    uint8_t slotIndex;
    char nickname[20];
    int playerIconId;
    int logoBorderId;
    int rankImageId;
};

struct SC_TempGameRoomEnter
{
    PacketHeader header;
    uint8_t result;
    uint8_t playerCount;
    TempGamePlayerSlotInfo players[TEMP_GAME_MAX_PLAYER_COUNT];
};

struct CS_TempGameStart
{
    PacketHeader header;
};

struct SC_TempGameStartResult
{
    PacketHeader header;
    uint8_t result;
};

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

enum class RoomEnterResult : uint8_t
{
    SUCCESS = 0,
    ROOM_NOT_FOUND = 1,
    ROOM_FULL = 2,
    ALREADY_IN_ROOM = 3,
    PLAYER_DATA_NOT_FOUND = 4,
    NOT_IN_LOBBY = 5
};

enum class RoomLeaveResult : uint8_t
{
    SUCCESS = 0,
    NOT_IN_ROOM = 1,
    ROOM_NOT_FOUND = 2
};

enum class RoomSlotTeam : uint8_t
{
    A = 0,
    B = 1,
    OBSERVER = 2
};

struct RoomMemberInfo
{
    uint32_t playerId;
    char nickname[20];

    int level;
    int playerIconId;

    uint8_t team;
    uint8_t slotIndex;
    uint8_t isLeader;
};

struct CS_RoomCreate
{
    PacketHeader header;
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

struct CS_RoomEnter
{
    PacketHeader header;
    uint32_t roomId;
};

struct SC_RoomEnterResult
{
    PacketHeader header;
    uint8_t result;
    uint32_t roomId;
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

enum class RoomGameStartResult : uint8_t
{
    SUCCESS = 0,
    NOT_IN_ROOM = 1,
    ROOM_NOT_FOUND = 2,
    NOT_LEADER = 3,
    NOT_ENOUGH_PLAYERS = 4,
    ALREADY_PLAYING = 5,
    FAILED = 6,
    NOT_ALL_RETURNED_FROM_RESULT = 7
};

struct CS_RoomGameStart
{
    PacketHeader header;
};

struct SC_RoomGameStartResult
{
    PacketHeader header;
    uint8_t result;
};

enum class RoomSlotChangeResult : uint8_t
{
    SUCCESS = 0,
    REQUEST_SENT = 1,
    NOT_IN_ROOM = 2,
    ROOM_NOT_FOUND = 3,
    INVALID_SLOT = 4,
    SAME_SLOT = 5,
    TARGET_NOT_FOUND = 6,
    REQUEST_ALREADY_EXISTS = 7,
    REJECTED = 8,
    FAILED = 9
};

struct CS_RoomSlotChange
{
    PacketHeader header;
    uint8_t targetTeam;
    uint8_t targetSlotIndex;
};

struct SC_RoomSlotChangeResult
{
    PacketHeader header;

    uint8_t result;

    uint8_t targetTeam;
    uint8_t targetSlotIndex;

    char targetNickname[20];
};

struct SC_RoomSlotSwapRequest
{
    PacketHeader header;

    uint32_t requesterPlayerId;
    char requesterNickname[20];

    uint8_t requesterTeam;
    uint8_t requesterSlotIndex;

    uint8_t targetTeam;
    uint8_t targetSlotIndex;
};

struct CS_RoomSlotSwapResponse
{
    PacketHeader header;

    uint32_t requesterPlayerId;
    uint8_t accept;
};

enum class RoomAdminResult : uint8_t
{
    SUCCESS = 0,
    NOT_IN_ROOM = 1,
    ROOM_NOT_FOUND = 2,
    NOT_LEADER = 3,
    TARGET_NOT_FOUND = 4,
    CANNOT_TARGET_SELF = 5,
    FAILED = 6
};

struct CS_RoomTransferLeader
{
    PacketHeader header;
    uint32_t targetPlayerId;
};

struct SC_RoomTransferLeaderResult
{
    PacketHeader header;
    uint8_t result;
    uint32_t targetPlayerId;
    char targetNickname[20];
};

struct CS_RoomKick
{
    PacketHeader header;
    uint32_t targetPlayerId;
};

struct SC_RoomKickResult
{
    PacketHeader header;
    uint8_t result;
    uint32_t targetPlayerId;
    char targetNickname[20];
};

struct SC_RoomKicked
{
    PacketHeader header;
    uint32_t roomId;
    char kickerNickname[20];
};

enum class PickRole : uint8_t
{
    ATTACK = 0,
    DEFENSE = 1
};

enum class PickLockResult : uint8_t
{
    SUCCESS = 0,
    NOT_IN_GAME = 1,
    ROOM_NOT_FOUND = 2,
    INVALID_CHARACTER = 3,
    ALREADY_LOCKED = 4,
    CHARACTER_ALREADY_TAKEN = 5,
    FAILED = 6
};

struct CS_PickEnter
{
    PacketHeader header;
};

struct CS_PickHover
{
    PacketHeader header;
    int characterId;
};

struct CS_PickLock
{
    PacketHeader header;
    int characterId;
};

struct SC_PickLockResult
{
    PacketHeader header;
    uint8_t result;
    int characterId;
};

struct PickTeamMemberInfo
{
    uint32_t playerId;
    char nickname[20];

    uint8_t slotIndex;

    int hoverCharacterId;
    int lockedCharacterId;

    uint8_t isLocked;
    uint8_t isMe;
};

struct SC_PickState
{
    PacketHeader header;

    uint32_t roomId;
    char mapName[MAP_NAME_LEN];

    uint8_t myTeam;
    uint8_t firstRole;
    uint8_t secondRole;

    uint16_t remainingSeconds;

    uint8_t blueScore;
    uint8_t redScore;

    uint8_t memberCount;
    PickTeamMemberInfo members[3];
};

enum class PickPhase : uint8_t
{
    PICKING = 0,
    PREPARE = 1,
    COMPLETE = 2
};

struct SC_PickTimer
{
    PacketHeader header;
    uint8_t phase;
    uint16_t remainingSeconds;
};

struct MatchLoadingMemberInfo
{
    uint32_t playerId;
    char nickname[MATCH_MEMBER_NICKNAME_LEN];
    uint8_t slotIndex;
    int32_t characterId;
    int32_t rankImageId;
};

struct SC_MatchLoadingInfo
{
    PacketHeader header;

    uint32_t roomId;

    uint8_t attackTeam;   // 0 = A, 1 = B
    uint8_t defenseTeam;  // 0 = A, 1 = B

    uint8_t attackCount;
    uint8_t defenseCount;

    MatchLoadingMemberInfo attackMembers[MATCH_TEAM_MEMBER_COUNT];
    MatchLoadingMemberInfo defenseMembers[MATCH_TEAM_MEMBER_COUNT];
};

struct SC_MatchStart
{
    PacketHeader header;
    uint32_t roomId;
};

struct CS_GameLoadReady
{
    PacketHeader header;
};

struct GameReadyMemberInfo
{
    uint32_t playerId;
    char userId[USER_ID_LEN];
    char nickname[GAME_MEMBER_NICKNAME_LEN];

    uint8_t team;       // 0 = A, 1 = B
    uint8_t slotIndex;  // 0, 1, 2

    int32_t characterId;
	int32_t rankImageId;
};

struct SC_GameAllReady
{
    PacketHeader header;

    uint32_t roomId;
    uint16_t prepareSeconds;

    uint8_t memberCount;
    GameReadyMemberInfo members[GAME_MEMBER_COUNT];
};

struct SC_GamePrepareTimer
{
    PacketHeader header;
    uint16_t remainingSeconds;
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
    float pitch;

    uint8_t animIndex;
    uint8_t isCrouching;
};

struct SC_MoveState
{
    PacketHeader header;

    uint32_t playerId;

    float x;
    float y;
    float z;

    float yaw;
    float pitch;

    uint8_t animIndex;
    uint8_t isCrouching;
};

struct CS_Hit
{
    PacketHeader header;
    uint32_t targetId;
};

struct SC_HPUpdate
{
    PacketHeader header;
    uint32_t playerId;

    int hp;
    int barrier;

    int maxHp;
    int maxBarrier;
};

struct SC_Die
{
    PacketHeader header;

    uint32_t killerPlayerId;
	uint32_t deadPlayerId;

    uint8_t killerTeam;
	uint8_t deadTeam;

    uint32_t killerCharacterId;
    uint32_t deadCharacterId;

	char killerNickname[20];
	char deadNickname[20];
};

struct SC_AmmoUpdate
{
    PacketHeader header;
    uint32_t playerId;
    int32_t weaponId;
    int32_t magAmmo;
    int32_t reserveAmmo;
};

struct CS_WeaponSwitch
{
    PacketHeader header;
    int weaponId;
};

struct SC_RoundResult
{
    PacketHeader header;

    uint8_t winnerTeam;     // 0 = A, 1 = B
    uint8_t scoreA;
    uint8_t scoreB;
    uint8_t currentRound;

    uint8_t matchEnd;       // 0 = 다음 라운드 있음, 1 = 매치 종료
    uint8_t winReason;
    uint16_t delaySeconds;  // 예: 10초
};

struct ScoreboardMemberInfo
{
    uint32_t playerId;

    uint8_t team;
    uint8_t slotIndex;

    int32_t characterId;

    char nickname[20];

    int32_t kills;
    int32_t deaths;
    int32_t assists;

    int32_t credits;
};

struct SC_ScoreboardUpdate
{
    PacketHeader header;

    uint32_t roomId;
    uint8_t myTeam;
    uint8_t memberCount;

    ScoreboardMemberInfo members[GAME_MEMBER_COUNT];
};

struct RoundResetMemberInfo
{
    uint32_t playerId;

    uint8_t actualTeam;
    uint8_t spawnTeam;
    uint8_t slotIndex;

    int hp;
    int barrier;
    int maxHp;
    int maxBarrier;

    int currentWeaponId;
    int magAmmo;
    int reserveAmmo;

    // 라운드 리셋 시 클라 인벤토리 복구용
    int primaryWeaponId;
    int primaryMagAmmo;
    int primaryReserveAmmo;

    int pistolWeaponId;
    int pistolMagAmmo;
    int pistolReserveAmmo;

    int knifeWeaponId;
    int knifeMagAmmo;
    int knifeReserveAmmo;
};

struct SC_RoundReset
{
    PacketHeader header;

    uint8_t currentRound;
    uint8_t scoreA;
    uint8_t scoreB;

    uint8_t memberCount;
    RoundResetMemberInfo members[GAME_MEMBER_COUNT];
};

struct SC_MatchResult
{
    PacketHeader header;

    uint8_t winnerTeam; // 0 = A, 1 = B
    uint8_t scoreA;
    uint8_t scoreB;
};

struct CS_Reload
{
    PacketHeader header;
};

struct CS_Fire
{
    PacketHeader header;
};

struct MatchResultMemberInfo
{
    uint32_t playerId;

    uint8_t team;
    uint8_t slotIndex;
    int characterId;

    char nickname[20];

    int kills;
    int deaths;
    int assists;

    int firstHalfKills;
    int firstHalfDeaths;
    int firstHalfAssists;

    int secondHalfKills;
    int secondHalfDeaths;
    int secondHalfAssists;

    int overtimeKills;
    int overtimeDeaths;
    int overtimeAssists;

    uint8_t isMvp;
};

struct SC_MatchResultDetail
{
    PacketHeader header;

    uint8_t winnerTeam;
    uint8_t scoreA;
    uint8_t scoreB;

    uint8_t memberCount;
    MatchResultMemberInfo members[6];
};

struct SC_ResultLeaveDone
{
    PacketHeader header;
    uint32_t roomId;
};

struct CS_RoomStateRequest
{
    PacketHeader header;
    uint32_t roomId;
};

enum class RoundWinReason : uint8_t
{
    NONE = 0,

    ATTACK_ELIMINATION = 1,     // 공격팀 승리: 수비팀 전멸
    ATTACK_SPIKE_EXPLODE = 2,   // 공격팀 승리: 스파이크 폭파

    DEFENSE_ELIMINATION = 3,    // 수비팀 승리: 공격팀 전멸
    DEFENSE_TIME_OUT = 4,       // 수비팀 승리: 시간 초과
    DEFENSE_SPIKE_DEFUSE = 5    // 수비팀 승리: 스파이크 해체
};

struct SC_RoundTimer
{
    PacketHeader header;
    uint16_t remainingSeconds;
};

struct CS_SpikePickupRequest
{
    PacketHeader header;
};

struct SC_SpikePickupNotify
{
    PacketHeader header;
    uint32_t carrierPlayerId;
    uint8_t showCarrierInfo; // 공격팀=1, 수비팀=0
    char carrierNickname[20];
};

struct CS_SpikePlantComplete
{
    PacketHeader header;
    float x;
    float y;
    float z;
};

struct SC_SpikePlanted
{
    PacketHeader header;
    float x;
    float y;
    float z;
};

struct SC_SpikeDropped
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float dirX;
    float dirY;
    float dirZ;

    uint32_t dropperPlayerId;
};

struct CS_SpikeDefuseStart
{
    PacketHeader header;
};

struct CS_SpikeDefuseCancel
{
    PacketHeader header;
};

struct CS_SpikeDefuseHalf
{
    PacketHeader header;
};

struct SC_SpikeDefuseState
{
    PacketHeader header;

    uint8_t state;
    // 0 = cancel/idle
    // 1 = start
    // 2 = half

    uint8_t halfDefused;
    uint32_t defuserPlayerId;
};

struct CS_SpikeDefuseComplete
{
    PacketHeader header;
};

struct SC_SpikeDefused
{
    PacketHeader header;
    uint32_t defuserPlayerId;
};

struct CS_SpikeDropRequest
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float dirX;
    float dirY;
    float dirZ;
};

struct SC_WeaponSwitchNotify
{
    PacketHeader header;

    uint32_t playerId;
    int weaponId;
};

struct CS_ADSState
{
    PacketHeader header;

    uint8_t isADS;
};

struct SC_ADSState
{
    PacketHeader header;

    uint32_t playerId;
    uint8_t isADS;
};

struct CS_WeaponDropRequest
{
    PacketHeader header;

    int32_t weaponId;

    float x;
    float y;
    float z;

    float dirX;
    float dirY;
    float dirZ;
};

struct SC_WeaponDropped
{
    PacketHeader header;

    uint32_t dropId;
    uint32_t dropperPlayerId;

    int32_t weaponId;
    int32_t magAmmo;
    int32_t reserveAmmo;

    float x;
    float y;
    float z;

    float dirX;
    float dirY;
    float dirZ;
};

struct CS_WeaponPickupRequest
{
    PacketHeader header;

    uint32_t dropId;
};

struct SC_WeaponPickupNotify
{
    PacketHeader header;

    uint32_t dropId;
    uint32_t pickerPlayerId;

    int32_t weaponId;
    int32_t magAmmo;
    int32_t reserveAmmo;
};

enum class StoreItemType : uint8_t
{
    WEAPON = 0,
    ARMOR = 1
};

enum class StoreBuyResult : uint8_t
{
    SUCCESS = 0,
    NOT_IN_GAME = 1,
    NOT_BUY_PHASE = 2,
    INVALID_ITEM = 3,
    NOT_ENOUGH_CREDIT = 4,
    ALREADY_OWNED = 5,
    FAILED = 6
};

struct CS_StoreBuyRequest
{
    PacketHeader header;

    uint8_t itemType;   // 0 = weapon, 1 = armor
    int32_t itemId;     // weaponId 또는 armorId
};


struct SC_StoreBuyResult
{
    PacketHeader header;

    uint8_t result;
    uint8_t itemType;

    int32_t itemId;
    int32_t price;
    int32_t remainCredits;

    int32_t weaponId;
    int32_t magAmmo;
    int32_t reserveAmmo;

    int32_t barrier;
    int32_t maxBarrier;
};

struct CS_KnifeAttack
{
    PacketHeader header;
};

struct SC_KnifeAttackNotify
{
    PacketHeader header;
    uint32_t playerId;
};

struct SC_FireNotify
{
    PacketHeader header;

    uint32_t playerId;
    int32_t weaponId;
};

struct CS_ReloadVisual
{
    PacketHeader header;

    int32_t weaponId;
    int32_t isReloading;
};

struct SC_ReloadVisual
{
    PacketHeader header;

    uint32_t playerId;
    int32_t weaponId;
    int32_t isReloading;
};

constexpr int IN_GAME_CHAT_MESSAGE_LEN = 256;

enum class InGameChatChannel : uint8_t
{
    TEAM = 0,
    MATCH = 1
};

struct CS_InGameChat
{
    PacketHeader header;

    uint8_t channel;

    char message[
        IN_GAME_CHAT_MESSAGE_LEN
    ];
};

struct SC_InGameChat
{
    PacketHeader header;

    uint32_t senderPlayerId;

    uint8_t channel;
    uint8_t senderTeam;

    char nickname[
        GAME_MEMBER_NICKNAME_LEN
    ];

    char message[
        IN_GAME_CHAT_MESSAGE_LEN
    ];
};

struct CS_DroneDeploy
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float yaw;
    float pitch;
};

struct CS_DroneState
{
    PacketHeader header;

    uint32_t droneId;

    float x;
    float y;
    float z;

    float yaw;
    float pitch;
};

struct CS_DroneExit
{
    PacketHeader header;

    uint32_t droneId;
};

struct SC_DroneSpawn
{
    PacketHeader header;

    uint32_t droneId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float yaw;
    float pitch;
};

struct SC_DroneState
{
    PacketHeader header;

    uint32_t droneId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float yaw;
    float pitch;
};

struct SC_DroneDestroy
{
    PacketHeader header;

    uint32_t droneId;
    uint32_t ownerPlayerId;

    uint8_t reason;
};

struct CS_DroneHit
{
    PacketHeader header;
    uint32_t droneId;
};

struct CS_DroneTaserFire
{
    PacketHeader header;

    uint32_t droneId;
    uint32_t targetPlayerId; // 0이면 빗나감
};

struct SC_DroneTaserFire
{
    PacketHeader header;

    uint32_t droneId;
    uint32_t ownerPlayerId;
    uint32_t targetPlayerId;

    uint8_t hit;    // 0 = miss / blocked, 1 = hit
    uint8_t stack;  // 감전 스택

    float duration;
    int32_t damage;
};

struct SC_DroneHpUpdate
{
    PacketHeader header;
    uint32_t droneId;
    uint32_t ownerPlayerId;
    int32_t hp;
    int32_t maxHp;
};

struct CS_ScannerThrow
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct SC_ScannerSpawn
{
    PacketHeader header;

    uint32_t scannerId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct CS_ScannerAnchor
{
    PacketHeader header;

    uint32_t scannerId;

    float x;
    float y;
    float z;
};

struct SC_ScannerAnchored
{
    PacketHeader header;

    uint32_t scannerId;

    float x;
    float y;
    float z;
};

struct SC_ScannerZap
{
    PacketHeader header;

    uint32_t scannerId;
    uint32_t targetPlayerId;

    uint8_t stack;
    float duration;
};

struct SC_ScannerDestroy
{
    PacketHeader header;

    uint32_t scannerId;
};

struct CS_ScannerHit
{
    PacketHeader header;
    uint32_t scannerId;
};

constexpr int EMP_MAX_TARGET_COUNT = GAME_MEMBER_COUNT;

struct CS_EmpCast
{
    PacketHeader header;
};

struct SC_EmpCast
{
    PacketHeader header;

    uint32_t casterPlayerId;
    uint8_t targetCount;

    uint32_t targetPlayerIds[EMP_MAX_TARGET_COUNT];
};

struct SC_ElectricStatus
{
    PacketHeader header;

    uint32_t playerId;

    uint8_t shockStack;
    uint8_t stunned;

    float stunSeconds;
};

constexpr int ULTIMATE_MAX_TARGET_COUNT = GAME_MEMBER_COUNT;

struct CS_UltimateCast
{
    PacketHeader header;

    float dirX;
    float dirY;
    float dirZ;
};

struct SC_UltimateCast
{
    PacketHeader header;

    uint32_t casterPlayerId;

    float dirX;
    float dirY;
    float dirZ;

    uint8_t targetCount;
    uint32_t targetPlayerIds[ULTIMATE_MAX_TARGET_COUNT];

    int32_t damage;
    uint8_t stack;
};
#pragma pack(pop)