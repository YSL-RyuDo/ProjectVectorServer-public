#pragma once
#include "Room.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>

class RoomManager
{
public:
    static RoomManager& Instance();

    std::vector<Room> GetRoomList();

    uint32_t CreateRoom(
        const std::string& roomName,
        const std::string& mapName,
        const std::string& hostName,
        const RoomMember& leaderMember,
        uint16_t maxPlayerCount = 8
    );

    bool GetRoom(uint32_t roomId, Room& outRoom);
    bool CanStartGame(uint32_t roomId);
    bool IsLeader(uint32_t roomId, uint32_t playerId);

    bool LeaveRoom(uint32_t roomId, uint32_t playerId, bool& outRoomDeleted);
private:
    int CountTeamMemberUnlocked(const Room& room, RoomTeam team);

private:
    std::unordered_map<uint32_t, Room> rooms_;
    std::mutex roomManagerMutex_;
    std::atomic<uint32_t> nextRoomId_{ 1 };
};