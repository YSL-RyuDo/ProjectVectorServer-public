// Packet.h
#pragma once
#include <cstdint>

constexpr int ROOM_NAME_LEN = 32;
constexpr int MAP_NAME_LEN = 32;
constexpr int HOST_NAME_LEN = 20;

constexpr int MAX_ROOM_LIST_COUNT = 32;
constexpr int MAX_LOBBY_CHARACTER_COUNT = 4;
constexpr int MAX_WEAPON_SKIN_LOADOUT_COUNT = 14;
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
constexpr int CAREER_SEASON_NAME_LEN = 32;
constexpr int CAREER_TIER_NAME_LEN = 32;
constexpr int CAREER_RANKING_COUNT = 10;
constexpr int CAREER_RANKING_QUERY_LEN = 64;
constexpr int CAREER_MATCH_HISTORY_COUNT = 5;
constexpr int CAREER_MATCH_PLAYER_COUNT = 6;
constexpr int CAREER_DETAIL_SEASON_COUNT = 8;
constexpr int CAREER_DETAIL_CHARACTER_COUNT = 4;
constexpr int CAREER_INVENTORY_COUNT = 20;
constexpr int CAREER_ITEM_NAME_LEN = 32;
constexpr int CAREER_ITEM_DESCRIPTION_LEN = 96;
constexpr int ROOM_CHAT_MESSAGE_LEN = 128;
constexpr int FRIEND_CHAT_MESSAGE_LEN = 128;
constexpr int FRIEND_USER_ID_LEN = 20;
constexpr int FRIEND_NICKNAME_LEN = 20;
constexpr int FRIEND_MEMO_LEN = 32;
constexpr int FRIEND_SEARCH_QUERY_LEN = 40;
constexpr int FRIEND_MAX_CONTACT_COUNT = 50;
constexpr int FRIEND_MAX_SEARCH_COUNT = 10;
constexpr int FRIEND_RECOMMEND_COUNT = 3;
constexpr int FRIEND_MAX_REQUEST_COUNT = 30;
constexpr int FRIEND_MAX_RECENT_COUNT = 20;
constexpr int PROFILE_VISITOR_COUNT = 8;
constexpr int PROFILE_CHARACTER_COUNT = 4;
constexpr int PROFILE_DATE_LEN = 16;
constexpr int PROFILE_REGION_LEN = 20;
constexpr int PROFILE_BATTLE_STAT_COUNT = 7;
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

    int freeCurrency;
    int victoryCurrency;
    // Appended: existing lobby fields retain their wire offsets.
    uint32_t selfPlayerId;
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

struct CS_RoomChat
{
    PacketHeader header;
    char message[ROOM_CHAT_MESSAGE_LEN];
};

struct SC_RoomChat
{
    PacketHeader header;
    uint32_t roomId;
    uint32_t playerId;
    char nickname[20];
    char message[ROOM_CHAT_MESSAGE_LEN];
};

struct CS_FriendChat
{
    PacketHeader header;
    uint32_t targetPlayerId;
    char message[FRIEND_CHAT_MESSAGE_LEN];
};

struct SC_FriendChat
{
    PacketHeader header;
    uint32_t senderPlayerId;
    uint32_t targetPlayerId;
    char senderNickname[20];
    char message[FRIEND_CHAT_MESSAGE_LEN];
};

enum class FriendOperation : uint8_t
{
    SEND_REQUEST = 0,
    CANCEL_REQUEST = 1,
    ACCEPT_REQUEST = 2,
    REJECT_REQUEST = 3,
    BLOCK = 4,
    UNBLOCK = 5,
    DELETE_FRIEND = 6
};

struct FriendUserInfo
{
    uint32_t playerId;
    char userId[FRIEND_USER_ID_LEN];
    char nickname[FRIEND_NICKNAME_LEN];
    int32_t level;
    int32_t playerIconId;
    int32_t rankImageId;
    uint8_t presenceStatus;
    char memo[FRIEND_MEMO_LEN];
};

struct CS_FriendOverviewRequest { PacketHeader header; };

struct SC_FriendOverview
{
    PacketHeader header;
    uint8_t presenceMode;
    uint8_t friendCount;
    uint8_t blockedCount;
    FriendUserInfo friends[FRIEND_MAX_CONTACT_COUNT];
    FriendUserInfo blocked[FRIEND_MAX_CONTACT_COUNT];
};

struct CS_FriendPresenceSet
{
    PacketHeader header;
    uint8_t presenceMode;
};

struct SC_FriendPresenceSetResult
{
    PacketHeader header;
    uint8_t result;
    uint8_t presenceMode;
};

struct CS_FriendSearch
{
    PacketHeader header;
    char query[FRIEND_SEARCH_QUERY_LEN];
};

struct SC_FriendSearch
{
    PacketHeader header;
    uint8_t count;
    FriendUserInfo users[FRIEND_MAX_SEARCH_COUNT];
};

struct CS_FriendRecommendRequest { PacketHeader header; };

struct SC_FriendRecommend
{
    PacketHeader header;
    uint8_t count;
    FriendUserInfo users[FRIEND_RECOMMEND_COUNT];
};

struct CS_FriendOperation
{
    PacketHeader header;
    uint8_t operation;
    char targetUserId[FRIEND_USER_ID_LEN];
};

struct SC_FriendOperationResult
{
    PacketHeader header;
    uint8_t operation;
    uint8_t result;
    char targetUserId[FRIEND_USER_ID_LEN];
};

struct CS_FriendRequestListRequest { PacketHeader header; };

struct FriendRequestInfo
{
    uint8_t direction;
    FriendUserInfo user;
};

struct SC_FriendRequestList
{
    PacketHeader header;
    uint8_t count;
    FriendRequestInfo requests[FRIEND_MAX_REQUEST_COUNT];
};

struct CS_FriendRecentRequest { PacketHeader header; };

struct SC_FriendRecent
{
    PacketHeader header;
    uint8_t count;
    FriendUserInfo users[FRIEND_MAX_RECENT_COUNT];
};

struct CS_FriendMemoSet
{
    PacketHeader header;
    char targetUserId[FRIEND_USER_ID_LEN];
    char memo[FRIEND_MEMO_LEN];
};

struct SC_FriendMemoSetResult
{
    PacketHeader header;
    uint8_t result;
    char targetUserId[FRIEND_USER_ID_LEN];
    char memo[FRIEND_MEMO_LEN];
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
struct CS_WeaponSkinLoadoutRequest
{
    PacketHeader header;
    int32_t characterId;
};

struct WeaponSkinLoadoutSlot
{
    int32_t weaponId;
    int32_t skinId;
};

struct SC_WeaponSkinLoadout
{
    PacketHeader header;
    int32_t characterId;
    uint16_t count;
    WeaponSkinLoadoutSlot slots[MAX_WEAPON_SKIN_LOADOUT_COUNT];
};

struct CS_WeaponSkinSelect
{
    PacketHeader header;
    int32_t characterId;
    int32_t weaponId;
    int32_t skinId;
};

struct SC_WeaponSkinSelectResult
{
    PacketHeader header;
    uint8_t result;
    int32_t characterId;
    int32_t weaponId;
    int32_t skinId;
};

struct CS_TempGameRoomEnter
{
    PacketHeader header;
};

struct CS_PartyInvite { PacketHeader header; uint32_t targetPlayerId; };
struct SC_PartyInvite { PacketHeader header; uint32_t token; uint32_t senderPlayerId; char nickname[20]; uint16_t seconds; };
struct CS_PartyInviteReply { PacketHeader header; uint32_t token; uint8_t accept; };
struct SC_SocialResult { PacketHeader header; uint8_t result; uint32_t targetPlayerId; };
struct SC_PartyState { PacketHeader header; uint32_t leader; uint8_t count; uint32_t players[3]; };
struct CS_PartyMode { PacketHeader header; uint8_t mode; };
struct SC_PartyMode { PacketHeader header; uint32_t leader; uint8_t mode; };
static_assert(sizeof(CS_PartyMode) == 5 && sizeof(SC_PartyMode) == 9, "Party mode wire layout");
struct SC_PartyAvailability { PacketHeader header; uint8_t count; uint8_t lobbyMask; };
static_assert(sizeof(SC_PartyAvailability) == 6, "Party availability wire layout");
struct CS_MatchReady { PacketHeader header; uint32_t token; uint8_t accept; };
struct SC_MatchReady { PacketHeader header; uint32_t token; uint8_t phase; uint16_t seconds; uint8_t total; uint8_t accepted; uint8_t selfAccepted; };
static_assert(sizeof(CS_PartyInvite) == 8 && sizeof(SC_PartyInvite) == 34, "Party invite wire layout");
static_assert(sizeof(CS_PartyInviteReply) == 9 && sizeof(SC_SocialResult) == 9, "Party reply wire layout");
static_assert(sizeof(SC_PartyState) == 21 && sizeof(CS_MatchReady) == 9 && sizeof(SC_MatchReady) == 14, "Party/ready wire layout");

struct ChipCraftMedalPacket
{
    uint8_t medalId;
    float x;
    float y;
    float rotation;
    float scale;
    uint8_t flipped;
};
struct ChipCraftPresetPacket
{
    uint8_t cardId;
    uint8_t caseId;
    uint8_t medalCount;
    ChipCraftMedalPacket medals[3];
};
// Optional result cosmetics; the existing 118-byte result member contract is unchanged.
struct MatchAppearanceMember
{
    uint32_t playerId;
    int32_t playerIconId; // Also the player-card ID; never an independent selection.
    int32_t logoBorderId;
    int32_t rankImageId;
    int32_t level;
    ChipCraftPresetPacket preset;
};
struct SC_MatchAppearance
{
    PacketHeader header;
    uint32_t roomId;
    uint8_t memberCount;
    MatchAppearanceMember members[6];
};
static_assert(sizeof(MatchAppearanceMember) == 77 && sizeof(SC_MatchAppearance) == 471, "Result appearance wire layout");

struct TempGamePlayerSlotInfo
{
    uint8_t slotIndex;
    char nickname[20];
    int playerIconId;
    int logoBorderId;
    int rankImageId;
    ChipCraftPresetPacket preset;
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
    uint8_t mode; // 0: normal, 1: ranked
};

struct SC_TempGameStartResult
{
    PacketHeader header;
    uint8_t result;
    uint8_t state; // 0: queued, 1: found, 2: cancelled, 3: rejected
    uint8_t mode;
    uint16_t elapsedSeconds;
    uint8_t tierRange;
    uint32_t roomId;
};

struct CS_MatchmakingCancel
{
    PacketHeader header;
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

enum class RoomMapSetResult : uint8_t
{
    SUCCESS = 0,
    NOT_IN_ROOM = 1,
    ROOM_NOT_FOUND = 2,
    NOT_LEADER = 3,
    INVALID_MAP = 4,
    ROOM_PLAYING = 5,
    PRIVATE_ROOM_ONLY = 6,
    FAILED = 7
};

enum class GameMapId : uint8_t
{
    TEST_MAP = 0,
    DESERT = 1
};

struct CS_RoomMapSet
{
    PacketHeader header;
    uint8_t mapId;
};

struct SC_RoomMapSetResult
{
    PacketHeader header;
    uint8_t result;
    uint8_t mapId;
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
    int32_t weaponSkinIds[MAX_WEAPON_SKIN_LOADOUT_COUNT];
};

struct SC_GameAllReady
{
    PacketHeader header;

    uint32_t roomId;
    char mapName[MAP_NAME_LEN];
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
    uint8_t hitZone; // 0 body, 1 head
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
    int primarySkinId;

    int pistolWeaponId;
    int pistolMagAmmo;
    int pistolReserveAmmo;
    int pistolSkinId;

    int knifeWeaponId;
    int knifeMagAmmo;
    int knifeReserveAmmo;
    int knifeSkinId;

    uint8_t purchasedSkillMask;
    uint8_t usedSkillMask;
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
    int32_t freeCurrencyReward;
    int32_t victoryCurrencyReward;
    int32_t rankPointDelta;
    int32_t rankPointsBefore;
    int32_t rankPointsAfter;
    int32_t rankTierIdBefore;
    int32_t rankTierIdAfter;
    uint16_t rankExpectedWinRatePermille;
    uint8_t rankModifierFlags;
    int32_t totalFreeCurrency;
    int32_t totalVictoryCurrency;
};

static_assert(
    sizeof(MatchResultMemberInfo) == 118,
    "MatchResultMemberInfo wire size must match the Unity parser.");

struct SC_MatchResultDetail
{
    PacketHeader header;

    uint8_t winnerTeam;
    uint8_t scoreA;
    uint8_t scoreB;
    uint8_t matchType; // 0: normal/private, 1: ranked

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
    int32_t skinId;

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
    int32_t skinId;
};

enum class StoreItemType : uint8_t
{
    WEAPON = 0,
    ARMOR = 1,
    SKILL = 2
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

    uint8_t purchasedSkillMask;
    uint8_t usedSkillMask;
};

struct SC_SkillState
{
    PacketHeader header;

    uint8_t purchasedSkillMask;
    uint8_t usedSkillMask;
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

struct CS_BeastStealthCast
{
    PacketHeader header;
};

struct CS_BeastStealthCancel
{
    PacketHeader header;
};

struct SC_BeastStealthState
{
    PacketHeader header;

    uint32_t playerId;

    uint8_t active;

    float duration;
    float speedMultiplier;
    float revealRange;
};

struct CS_EntrySmokeThrow
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct SC_EntrySmokeSpawn
{
    PacketHeader header;

    uint32_t smokeId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct CS_EntrySmokeAttach
{
    PacketHeader header;

    uint32_t smokeId;

    float x;
    float y;
    float z;

    float nx;
    float ny;
    float nz;
};

struct SC_EntrySmokeActivate
{
    PacketHeader header;

    uint32_t smokeId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float nx;
    float ny;
    float nz;

    float radius;
    float duration;
};

struct SC_EntrySmokeDestroy
{
    PacketHeader header;

    uint32_t smokeId;
};

struct CS_EntrySmokeState
{
    PacketHeader header;

    uint32_t smokeId;

    float x;
    float y;
    float z;

    float rotX;
    float rotY;
    float rotZ;
    float rotW;
};

struct SC_EntrySmokeState
{
    PacketHeader header;

    uint32_t smokeId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float rotX;
    float rotY;
    float rotZ;
    float rotW;
};

struct CS_EntryDashCast
{
    PacketHeader header;

    float dirX;
    float dirY;
    float dirZ;
};

struct SC_EntryDashCast
{
    PacketHeader header;

    uint32_t playerId;

    float dirX;
    float dirY;
    float dirZ;

    float dashForce;
    float dashDuration;
};

struct SC_EntryDashEnd
{
    PacketHeader header;

    uint32_t playerId;
};

struct CS_EntryBladeActivate
{
    PacketHeader header;
};

struct SC_EntryBladeState
{
    PacketHeader header;

    uint32_t playerId;

    uint8_t active;
    uint8_t bladeCount;

    float duration;
};

struct CS_EntryBladeFire
{
    PacketHeader header;

    float dirX;
    float dirY;
    float dirZ;

    uint32_t targetPlayerId;

    float hitX;
    float hitY;
    float hitZ;
};

struct SC_EntryBladeFire
{
    PacketHeader header;

    uint32_t shooterPlayerId;
    uint32_t targetPlayerId;

    uint8_t hit;
    uint8_t remainingBlades;

    float startX;
    float startY;
    float startZ;

    float endX;
    float endY;
    float endZ;
};

struct CS_ControllerSwampThrow
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct SC_ControllerSwampSpawn
{
    PacketHeader header;

    uint32_t trapId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct CS_ControllerSwampActivate
{
    PacketHeader header;

    uint32_t trapId;

    float x;
    float y;
    float z;

    float normalX;
    float normalY;
    float normalZ;
};

struct SC_ControllerSwampActivate
{
    PacketHeader header;

    uint32_t trapId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float normalX;
    float normalY;
    float normalZ;

    float radius;
    float duration;
};

struct SC_ControllerSwampDestroy
{
    PacketHeader header;

    uint32_t trapId;
};

struct CS_ControllerSmokeDrop
{
    PacketHeader header;
    float x;
    float y;
    float z;
};

struct SC_ControllerSmokeSpawn
{
    PacketHeader header;
    uint32_t smokeId;
    uint32_t ownerPlayerId;
    float x;
    float y;
    float z;
    float radius;
    float duration;
};

struct SC_ControllerSmokeDestroy
{
    PacketHeader header;
    uint32_t smokeId;
};

struct CS_ControllerDecoyThrow
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct SC_ControllerDecoySpawn
{
    PacketHeader header;

    uint32_t decoyId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct CS_ControllerDecoyActivate
{
    PacketHeader header;

    uint32_t decoyId;

    float x;
    float y;
    float z;

    float normalX;
    float normalY;
    float normalZ;
};

struct SC_ControllerDecoyActivate
{
    PacketHeader header;

    uint32_t decoyId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float normalX;
    float normalY;
    float normalZ;

    float duration;
};

struct SC_ControllerDecoyDestroy
{
    PacketHeader header;

    uint32_t decoyId;
};

struct CS_ControllerUltimateCast
{
    PacketHeader header;

    float x;
    float y;
    float z;
};

struct SC_ControllerUltimateSpawn
{
    PacketHeader header;

    uint32_t ultimateId;
    uint32_t ownerPlayerId;

    float x;
    float y;
    float z;

    float radius;
    float duration;
};

struct SC_ControllerUltimateDestroy
{
    PacketHeader header;

    uint32_t ultimateId;
};

struct CS_SentinelCThrow
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;
};

struct SC_SentinelCSpawn
{
    PacketHeader header;

    uint32_t trapId;
    uint32_t ownerPlayerId;
    uint8_t ownerTeam;

    float x;
    float y;
    float z;

    float vx;
    float vy;
    float vz;

    float hp;
};

struct CS_SentinelCAnchor
{
    PacketHeader header;

    uint32_t trapId;

    float x;
    float y;
    float z;

    float nx;
    float ny;
    float nz;
};

struct SC_SentinelCAnchor
{
    PacketHeader header;

    uint32_t trapId;

    float x;
    float y;
    float z;

    float nx;
    float ny;
    float nz;
};

struct CS_SentinelCTrigger
{
    PacketHeader header;

    uint32_t trapId;

    float x;
    float y;
    float z;

    float radius;
    float revealDuration;
};

struct SC_SentinelCTrigger
{
    PacketHeader header;

    uint32_t trapId;
    uint32_t ownerPlayerId;
    uint8_t ownerTeam;

    float x;
    float y;
    float z;

    float radius;
    float revealDuration;
};

struct CS_SentinelCHit
{
    PacketHeader header;

    uint32_t trapId;
    float damage;
};

struct SC_SentinelCDestroy
{
    PacketHeader header;

    uint32_t trapId;
};

struct CS_ProfileRequest
{
    PacketHeader header;
    char targetUserId[USER_ID_LEN];
};

struct ProfileCharacterPacketEntry
{
    int32_t characterId;
    int32_t matches;
    int32_t wins;
    int32_t maxKills;
    uint8_t owned;
};

struct ProfileVisitorPacketEntry
{
    char userId[USER_ID_LEN];
    char nickname[MATCH_MEMBER_NICKNAME_LEN];
    int32_t playerIconId;
    int64_t visitedAtUnix;
};

struct SC_Profile
{
    PacketHeader header;
    uint8_t result;
    uint8_t isSelf;
    uint8_t privacyEnabled;
    char userId[USER_ID_LEN];
    char nickname[MATCH_MEMBER_NICKNAME_LEN];
    int32_t level;
    int32_t playerIconId;
    int32_t logoBorderId;
    char joinDate[PROFILE_DATE_LEN];
    char region[PROFILE_REGION_LEN];
    char birthday[PROFILE_DATE_LEN];
    int32_t likeCount;
    int32_t seasonId;
    int32_t currentRankImageId;
    char currentTierName[CAREER_TIER_NAME_LEN];
    int32_t seasonHighestRankImageId;
    int32_t allTimeHighestRankImageId;
    int32_t allTimeHighestSeasonId;
    int32_t playCount;
    int32_t wins;
    int32_t roundsPlayed;
    int32_t totalKills;
    int32_t totalAssists;
    int32_t totalDamage;
    int32_t mvpCount;
    int32_t headshotKills;
    int32_t spikePlants;
    int32_t spikeDefuses;
    int32_t weaponSkinOwnedCount;
    int32_t weaponSkinTotalCount;
    int32_t characterSkinOwnedCount;
    int32_t characterSkinTotalCount;
    uint8_t battleStatIds[PROFILE_BATTLE_STAT_COUNT];
    ProfileCharacterPacketEntry characters[PROFILE_CHARACTER_COUNT];
    uint8_t visitorCount;
    ProfileVisitorPacketEntry visitors[PROFILE_VISITOR_COUNT];
};

struct CS_ProfilePrivacySet { PacketHeader header; uint8_t enabled; };
struct SC_ProfilePrivacySetResult { PacketHeader header; uint8_t result; uint8_t enabled; };
struct CS_ProfileLike { PacketHeader header; char targetUserId[USER_ID_LEN]; };
struct SC_ProfileLikeResult { PacketHeader header; uint8_t result; char targetUserId[USER_ID_LEN]; int32_t likeCount; };
struct CS_ProfileBattleStatsSet { PacketHeader header; uint8_t battleStatIds[PROFILE_BATTLE_STAT_COUNT]; };
struct SC_ProfileBattleStatsSetResult { PacketHeader header; uint8_t result; uint8_t battleStatIds[PROFILE_BATTLE_STAT_COUNT]; };
struct CS_ChipCraftRequest { PacketHeader header; char targetUserId[USER_ID_LEN]; };
struct SC_ChipCraft
{
    PacketHeader header;
    uint8_t result;
    uint8_t isSelf;
    char userId[USER_ID_LEN];
    uint8_t ownedCardMask;
    uint8_t ownedCaseMask;
    uint8_t ownedMedalMask;
    ChipCraftPresetPacket preset;
};
struct CS_ChipCraftSave { PacketHeader header; ChipCraftPresetPacket preset; };
struct SC_ChipCraftSaveResult { PacketHeader header; uint8_t result; ChipCraftPresetPacket preset; };
struct CS_LevelRewardRequest { PacketHeader header; };
struct SC_LevelRewardState { PacketHeader header; uint8_t result; int32_t level; int32_t currentExp; int32_t requiredExp; int32_t claimedLevel; int32_t freeCurrency; int32_t victoryCurrency; };
struct CS_LevelRewardClaim { PacketHeader header; int32_t targetLevel; };
struct SC_LevelRewardClaimResult { PacketHeader header; uint8_t result; int32_t claimedLevel; int32_t freeCurrencyAwarded; int32_t victoryCurrencyAwarded; int32_t freeCurrency; int32_t victoryCurrency; };
struct CS_HonorRequest { PacketHeader header; uint8_t page; };
struct HonorHistoryPacketEntry { int64_t appliedAtUnix; int32_t delta; uint8_t reason; };
struct SC_HonorState
{
    PacketHeader header;
    uint8_t result;
    int32_t score;
    int32_t claimedGrade;
    int32_t freeCurrency;
    int32_t victoryCurrency;
    uint8_t page;
    uint8_t pageCount;
    uint8_t entryCount;
    HonorHistoryPacketEntry entries[10];
};
struct CS_HonorClaim { PacketHeader header; int32_t targetGrade; };
struct SC_HonorClaimResult
{
    PacketHeader header;
    uint8_t result;
    int32_t claimedGrade;
    int32_t freeCurrencyAwarded;
    int32_t victoryCurrencyAwarded;
    int32_t freeCurrency;
    int32_t victoryCurrency;
};
struct CS_CareerOverviewRequest
{
    PacketHeader header;
};

struct SC_CareerOverview
{
    PacketHeader header;
    int32_t seasonId;
    char seasonName[CAREER_SEASON_NAME_LEN];
    int64_t seasonEndUnix;
    int32_t remainingSeasonDays;
    int32_t tierId;
    char tierName[CAREER_TIER_NAME_LEN];
    int32_t rankImageId;
    int32_t tierMinPoints;
    int32_t tierMaxPoints;
    int32_t rankPoints;
    int32_t rankedMatches;
    int32_t wins;
    int32_t roundsPlayed;
    int32_t kills;
    int32_t totalDamage;
    int32_t mvpCount;
};
struct CS_CareerDetailRequest
{
    PacketHeader header;
    char targetUserId[USER_ID_LEN];
    int32_t seasonId;
    uint8_t modeType;
    int32_t characterId;
};

struct CareerDetailSeasonPacketEntry
{
    int32_t seasonId;
    char seasonName[CAREER_SEASON_NAME_LEN];
    uint8_t isActive;
};

struct CareerDetailCharacterPacketEntry
{
    int32_t characterId;
    int32_t performanceScore;
    int32_t matches;
    int32_t wins;
    int32_t kills;
    int32_t deaths;
    int32_t maxKills;
    int32_t maxDamage;
};

struct SC_CareerDetail
{
    PacketHeader header;
    uint8_t result;
    uint8_t hasData;
    int32_t seasonId;
    uint8_t modeType;
    int32_t selectedCharacterId;
    uint8_t seasonCount;
    CareerDetailSeasonPacketEntry seasons[CAREER_DETAIL_SEASON_COUNT];
    CareerDetailCharacterPacketEntry characters[CAREER_DETAIL_CHARACTER_COUNT];
    int32_t playCount;
    int32_t wins;
    int32_t roundsPlayed;
    int32_t totalDamage;
    int32_t kills;
    int32_t deaths;
    int32_t assists;
    int32_t headshotKills;
    int32_t meleeKills;
    int32_t spikePlants;
    int32_t spikeDefuses;
    int32_t maxAssists;
    int32_t maxRoundDamageHundredths;
    int32_t maxRoundKillsHundredths;
};
struct CS_CareerRankingRequest
{
    PacketHeader header;
    uint8_t rankingType;
    int32_t characterId;
    uint8_t page;
    char searchQuery[CAREER_RANKING_QUERY_LEN];
};

struct CareerRankingPacketEntry
{
    int32_t position;
    char userId[USER_ID_LEN];
    char nickname[MATCH_MEMBER_NICKNAME_LEN];
    int32_t playerIconId;
    int32_t tierId;
    char tierName[CAREER_TIER_NAME_LEN];
    int32_t rankImageId;
    int32_t rankPoints;
    int32_t rankedMatches;
    int32_t wins;
};

struct SC_CareerRanking
{
    PacketHeader header;
    uint8_t rankingType;
    int32_t characterId;
    uint8_t page;
    uint8_t totalPages;
    int32_t totalEntries;
    uint8_t count;
    CareerRankingPacketEntry entries[CAREER_RANKING_COUNT];
};

struct CS_CareerMatchHistoryRequest
{
    PacketHeader header;
    uint8_t filterType;
};

struct CareerMatchPlayerPacketEntry
{
    char userId[USER_ID_LEN];
    char nickname[MATCH_MEMBER_NICKNAME_LEN];
    uint8_t team;
    int32_t characterId;
    int32_t combatScore;
    int32_t attackScore;
    int32_t skillScore;
    int32_t teamworkScore;
    int32_t kills;
    int32_t deaths;
    int32_t assists;
    int32_t damage;
    uint8_t isMvp;
};

struct CareerMatchPacketEntry
{
    uint64_t matchId;
    char mapName[MAP_NAME_LEN];
    int64_t startedAtUnix;
    int32_t durationSeconds;
    uint8_t scoreA;
    uint8_t scoreB;
    uint8_t winnerTeam;
    uint8_t myTeam;
    uint8_t playerCount;
    CareerMatchPlayerPacketEntry players[CAREER_MATCH_PLAYER_COUNT];
};

struct SC_CareerMatchHistory
{
    PacketHeader header;
    uint8_t filterType;
    uint8_t count;
    CareerMatchPacketEntry matches[CAREER_MATCH_HISTORY_COUNT];
};

struct CS_CareerInventoryRequest
{
    PacketHeader header;
};

struct CareerInventoryPacketEntry
{
    int32_t itemId;
    char itemName[CAREER_ITEM_NAME_LEN];
    char description[CAREER_ITEM_DESCRIPTION_LEN];
    int32_t iconId;
    int32_t quantity;
    int64_t acquiredAtUnix;
};

struct SC_CareerInventory
{
    PacketHeader header;
    uint8_t count;
    CareerInventoryPacketEntry entries[CAREER_INVENTORY_COUNT];
};

static_assert(sizeof(SC_CareerOverview) <= 4096);
static_assert(sizeof(CareerRankingPacketEntry) == 100);
static_assert(sizeof(SC_CareerRanking) <= 4096);
static_assert(sizeof(SC_CareerMatchHistory) <= 4096);
static_assert(sizeof(SC_CareerInventory) <= 4096);

struct CS_SentinelQPlace
{
    PacketHeader header;

    float x;
    float y;
    float z;
};

struct SC_SentinelQSpawn
{
    PacketHeader header;

    uint32_t trapId;
    uint32_t ownerPlayerId;
    uint8_t ownerTeam;

    float x;
    float y;
    float z;

    float hp;
    float detectionRadius;
    float zoneRadius;
};

struct CS_SentinelQActivate
{
    PacketHeader header;

    uint32_t trapId;
};

struct SC_SentinelQActivate
{
    PacketHeader header;

    uint32_t trapId;
    float duration;
    float slowMultiplier;
};

struct CS_SentinelQHit
{
    PacketHeader header;

    uint32_t trapId;
};

struct SC_SentinelQDestroy
{
    PacketHeader header;

    uint32_t trapId;
};

struct CS_SentinelEPlace
{
    PacketHeader header;

    float x;
    float y;
    float z;

    float nx;
    float ny;
    float nz;
};

struct SC_SentinelESpawn
{
    PacketHeader header;

    uint32_t trapId;
    uint32_t ownerPlayerId;
    uint8_t ownerTeam;

    float x;
    float y;
    float z;

    float nx;
    float ny;
    float nz;

    float hp;
    float radius;
};

struct CS_SentinelEActivate
{
    PacketHeader header;

    uint32_t trapId;
};

struct SC_SentinelEActivate
{
    PacketHeader header;

    uint32_t trapId;
    float radius;
    float slowDuration;
    float slowMultiplier;
    float stunDuration;
};

struct CS_SentinelEHit
{
    PacketHeader header;

    uint32_t trapId;
};

struct CS_SentinelERecall
{
    PacketHeader header;

    uint32_t trapId;
};

struct SC_SentinelEDestroy
{
    PacketHeader header;

    uint32_t trapId;
    uint8_t reason;
    float cooldownSeconds;
};

struct CS_SentinelXCast
{
    PacketHeader header;

    float x;
    float y;
    float z;
};

struct SC_SentinelXSpawn
{
    PacketHeader header;

    uint32_t ultimateId;
    uint32_t ownerPlayerId;
    uint8_t ownerTeam;

    float x;
    float y;
    float z;

    float radius;
    float duration;
    float slowMultiplier;
    float protectionDuration;
};

struct SC_SentinelXDestroy
{
    PacketHeader header;

    uint32_t ultimateId;
};
#pragma pack(pop)
