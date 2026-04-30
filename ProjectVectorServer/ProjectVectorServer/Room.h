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

struct Room
{
    uint32_t roomId;
    std::string roomName;
    std::string mapName;
    std::string hostName;

    std::vector<Session*> teamA;      // 최대 3
    std::vector<Session*> teamB;      // 최대 3
    std::vector<Session*> spectators; // 최대 2

    RoomState state = RoomState::WAITING;
};