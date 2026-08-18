#pragma once
#include <string>
#include <cstdint>
#include <vector>

class Session;

enum class RoomState : uint8_t
{
    WAITING = 0,
    PLAYING = 1
};

enum class RoomTeam : uint8_t
{
    A = 0,
    B = 1,
    OBSERVER = 2
};

struct RoomMember
{
    uint32_t playerId = 0;
    std::string userId;
    std::string nickname;

    int level = 1;
    int playerIconId = 0;
    int rankImageId = 0;
    uint64_t matchmakingPartyId = 0;
    uint8_t matchmakingPartySize = 1;

    RoomTeam team = RoomTeam::A;
    uint8_t slotIndex = 0;

    bool isLeader = false;

    int hoverCharacterId = -1;
    int lockedCharacterId = -1;
    bool isPickLocked = false;
    bool isPickReady = false;

	bool isGameLoadReady = false;
};

struct Room
{
    uint32_t roomId = 0;

    std::string roomName;
    std::string mapName;
    std::string hostName;

    uint16_t currentPlayerCount = 0;
    uint16_t maxPlayerCount = 8;

    RoomState state = RoomState::WAITING;

    // 0: normal/private, 1: ranked. Matchmade rooms grant rewards;
    // manually created private rooms do not.
    int matchType = 0;
    bool isMatchmade = false;

    uint32_t leaderPlayerId = 0;
    std::vector<RoomMember> members;

    uint16_t pickRemainingSeconds = 20;
    bool pickFinished = false;
    bool pickTimerStarted = false;

    uint16_t gamePrepareRemainingSeconds = 20;
    bool gamePrepareTimerStarted = false;
};
