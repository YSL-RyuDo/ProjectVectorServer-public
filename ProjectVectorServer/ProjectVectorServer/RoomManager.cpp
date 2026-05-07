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

uint32_t RoomManager::CreateRoom(
    const std::string& roomName,
    const std::string& mapName,
    const std::string& hostName,
    const RoomMember& leaderMember,
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
    room.leaderPlayerId = leaderMember.playerId;

    room.members.push_back(leaderMember);

    rooms_[room.roomId] = room;

    return room.roomId;
}

bool RoomManager::GetRoom(uint32_t roomId, Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    outRoom = it->second;
    return true;
}

int RoomManager::CountTeamMemberUnlocked(const Room& room, RoomTeam team)
{
    int count = 0;

    for (const RoomMember& member : room.members)
    {
        if (member.team == team)
            ++count;
    }

    return count;
}

bool RoomManager::CanStartGame(uint32_t roomId)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    const Room& room = it->second;

    int aCount = CountTeamMemberUnlocked(room, RoomTeam::A);
    int bCount = CountTeamMemberUnlocked(room, RoomTeam::B);

    return aCount == 3 && bCount == 3;
}

bool RoomManager::IsLeader(uint32_t roomId, uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    return it->second.leaderPlayerId == playerId;
}

bool RoomManager::LeaveRoom(uint32_t roomId, uint32_t playerId, bool& outRoomDeleted)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    outRoomDeleted = false;

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    auto memberIt = std::remove_if(
        room.members.begin(),
        room.members.end(),
        [playerId](const RoomMember& member)
        {
            return member.playerId == playerId;
        });

    if (memberIt == room.members.end())
        return false;

    bool wasLeader = false;

    for (const RoomMember& member : room.members)
    {
        if (member.playerId == playerId && member.isLeader)
        {
            wasLeader = true;
            break;
        }
    }

    room.members.erase(memberIt, room.members.end());
    room.currentPlayerCount = static_cast<uint16_t>(room.members.size());

    if (room.members.empty())
    {
        rooms_.erase(it);
        outRoomDeleted = true;
        return true;
    }

    if (wasLeader)
    {
        room.members[0].isLeader = true;
        room.leaderPlayerId = room.members[0].playerId;
    }

    return true;
}