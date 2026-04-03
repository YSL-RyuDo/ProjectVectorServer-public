// RoomManager.cpp
#include "RoomManager.h"

RoomManager& RoomManager::Instance()
{
    static RoomManager inst;
    return inst;
}

std::vector<Room> RoomManager::GetRoomList()
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    std::vector<Room> result;
    result.reserve(rooms_.size());

    for (auto& pair : rooms_)
    {
        result.push_back(pair.second);
    }

    return result;
}

uint32_t RoomManager::CreateRoom(const std::string& roomName,
    const std::string& mapName,
    const std::string& hostName,
    uint16_t maxPlayerCount)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);
    
    Room room;
    room.roomId = nextRoomId_.fetch_add(1);
    room.roomName = roomName;
    room.mapName = mapName;
    room.hostName = hostName;
    room.currentPlayerCount = 1;
    room.maxPlayerCount = maxPlayerCount;
    room.state = RoomState::WAITING;

    rooms_[room.roomId] = room;
    return room.roomId;
}