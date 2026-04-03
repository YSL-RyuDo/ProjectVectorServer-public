#pragma once
#include <string>
#include <cstdint>

enum class RoomState : uint8_t
{
    WAITING = 0,
    PLAYING = 1
};

struct Room
{
    uint32_t roomId = 0;
    std::string roomName;
    std::string mapName;
    std::string hostName;
    uint16_t currentPlayerCount = 1;
    uint16_t maxPlayerCount = 8;
    RoomState state = RoomState::WAITING;
};