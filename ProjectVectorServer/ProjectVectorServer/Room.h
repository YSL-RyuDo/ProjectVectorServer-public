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

    RoomTeam team = RoomTeam::A;
    uint8_t slotIndex = 0;

    bool isLeader = false;

    int hoverCharacterId = -1;
    int lockedCharacterId = -1;
    bool isPickLocked = false;
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

    uint32_t leaderPlayerId = 0;
    std::vector<RoomMember> members;
};