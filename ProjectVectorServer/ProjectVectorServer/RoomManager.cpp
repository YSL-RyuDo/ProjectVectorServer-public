#include "RoomManager.h"

#include <algorithm>

namespace
{
    constexpr uint8_t ROOM_A_SLOT_COUNT = 3;
    constexpr uint8_t ROOM_B_SLOT_COUNT = 3;
    constexpr uint8_t ROOM_OBSERVER_SLOT_COUNT = 2;

    struct RoomSlotPosition
    {
        RoomTeam team;
        uint8_t slotIndex;
    };

    const RoomSlotPosition ENTER_SLOT_ORDER[] =
    {
        { RoomTeam::A, 0 },
        { RoomTeam::B, 0 },
        { RoomTeam::A, 1 },
        { RoomTeam::B, 1 },
        { RoomTeam::A, 2 },
        { RoomTeam::B, 2 },
        { RoomTeam::OBSERVER, 0 },
        { RoomTeam::OBSERVER, 1 },
    };

    const RoomSlotPosition LEADER_SLOT_ORDER[] =
    {
        { RoomTeam::A, 0 },
        { RoomTeam::B, 0 },
        { RoomTeam::A, 1 },
        { RoomTeam::B, 1 },
        { RoomTeam::A, 2 },
        { RoomTeam::B, 2 },
        { RoomTeam::OBSERVER, 0 },
        { RoomTeam::OBSERVER, 1 },
    };
}

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

    std::sort(
        result.begin(),
        result.end(),
        [](const Room& a, const Room& b)
        {
            return a.roomId < b.roomId;
        }
    );

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
    room.maxPlayerCount = maxPlayerCount;
    room.state = RoomState::WAITING;

    RoomMember leader = leaderMember;
    leader.team = RoomTeam::A;
    leader.slotIndex = 0;
    leader.isLeader = true;

    room.leaderPlayerId = leader.playerId;
    room.members.push_back(leader);
    room.currentPlayerCount = static_cast<uint16_t>(room.members.size());

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

bool RoomManager::EnterRoom(
    uint32_t roomId,
    const RoomMember& member,
    Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.state != RoomState::WAITING)
        return false;

    if (room.members.size() >= room.maxPlayerCount)
        return false;

    if (IsPlayerInRoomUnlocked(room, member.playerId))
        return false;

    RoomTeam team = RoomTeam::A;
    uint8_t slotIndex = 0;

    bool foundSlot = FindNextSlotUnlocked(room, team, slotIndex);
    if (!foundSlot)
        return false;

    RoomMember newMember = member;
    newMember.team = team;
    newMember.slotIndex = slotIndex;
    newMember.isLeader = false;

    room.members.push_back(newMember);
    room.currentPlayerCount = static_cast<uint16_t>(room.members.size());

    outRoom = room;

    return true;
}

bool RoomManager::LeaveRoom(
    uint32_t roomId,
    uint32_t playerId,
    bool& outRoomDeleted,
    Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    outRoomDeleted = false;
    outRoom = Room{};

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    auto memberIt = std::find_if(
        room.members.begin(),
        room.members.end(),
        [playerId](const RoomMember& member)
        {
            return member.playerId == playerId;
        }
    );

    if (memberIt == room.members.end())
        return false;

    bool wasLeader = memberIt->isLeader || room.leaderPlayerId == playerId;

    room.members.erase(memberIt);
    room.currentPlayerCount = static_cast<uint16_t>(room.members.size());

    if (room.members.empty())
    {
        rooms_.erase(it);
        outRoomDeleted = true;
        return true;
    }

    if (wasLeader)
    {
        for (RoomMember& member : room.members)
        {
            member.isLeader = false;
        }

        RoomMember* newLeader = FindFirstMemberByLeaderOrderUnlocked(room);
        if (newLeader != nullptr)
        {
            newLeader->isLeader = true;
            room.leaderPlayerId = newLeader->playerId;
        }
    }

    outRoom = room;

    return true;
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

bool RoomManager::IsPlayerInRoomUnlocked(const Room& room, uint32_t playerId)
{
    for (const RoomMember& member : room.members)
    {
        if (member.playerId == playerId)
            return true;
    }

    return false;
}

bool RoomManager::FindNextSlotUnlocked(
    const Room& room,
    RoomTeam& outTeam,
    uint8_t& outSlotIndex)
{
    for (const RoomSlotPosition& pos : ENTER_SLOT_ORDER)
    {
        bool occupied = false;

        for (const RoomMember& member : room.members)
        {
            if (member.team == pos.team && member.slotIndex == pos.slotIndex)
            {
                occupied = true;
                break;
            }
        }

        if (!occupied)
        {
            outTeam = pos.team;
            outSlotIndex = pos.slotIndex;
            return true;
        }
    }

    return false;
}

RoomMember* RoomManager::FindFirstMemberByLeaderOrderUnlocked(Room& room)
{
    for (const RoomSlotPosition& pos : LEADER_SLOT_ORDER)
    {
        for (RoomMember& member : room.members)
        {
            if (member.team == pos.team && member.slotIndex == pos.slotIndex)
                return &member;
        }
    }

    return nullptr;
}

bool RoomManager::GetMember(
    uint32_t roomId,
    uint32_t playerId,
    RoomMember& outMember)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    const Room& room = it->second;

    for (const RoomMember& member : room.members)
    {
        if (member.playerId == playerId)
        {
            outMember = member;
            return true;
        }
    }

    return false;
}

bool RoomManager::GetMemberBySlot(
    uint32_t roomId,
    RoomTeam team,
    uint8_t slotIndex,
    RoomMember& outMember)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    const Room& room = it->second;

    if (!IsValidSlotUnlocked(team, slotIndex))
        return false;

    for (const RoomMember& member : room.members)
    {
        if (member.team == team && member.slotIndex == slotIndex)
        {
            outMember = member;
            return true;
        }
    }

    return false;
}

bool RoomManager::MoveMemberToSlot(
    uint32_t roomId,
    uint32_t playerId,
    RoomTeam targetTeam,
    uint8_t targetSlotIndex,
    Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (!IsValidSlotUnlocked(targetTeam, targetSlotIndex))
        return false;

    RoomMember* movingMember = nullptr;

    for (RoomMember& member : room.members)
    {
        if (member.playerId == playerId)
        {
            movingMember = &member;
            break;
        }
    }

    if (movingMember == nullptr)
        return false;

    for (const RoomMember& member : room.members)
    {
        if (member.team == targetTeam && member.slotIndex == targetSlotIndex)
            return false;
    }

    movingMember->team = targetTeam;
    movingMember->slotIndex = targetSlotIndex;

    outRoom = room;
    return true;
}

bool RoomManager::SwapMembers(
    uint32_t roomId,
    uint32_t playerAId,
    uint32_t playerBId,
    Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    RoomMember* memberA = nullptr;
    RoomMember* memberB = nullptr;

    for (RoomMember& member : room.members)
    {
        if (member.playerId == playerAId)
            memberA = &member;

        if (member.playerId == playerBId)
            memberB = &member;
    }

    if (memberA == nullptr || memberB == nullptr)
        return false;

    RoomTeam tempTeam = memberA->team;
    uint8_t tempSlotIndex = memberA->slotIndex;

    memberA->team = memberB->team;
    memberA->slotIndex = memberB->slotIndex;

    memberB->team = tempTeam;
    memberB->slotIndex = tempSlotIndex;

    outRoom = room;
    return true;
}

bool RoomManager::IsValidSlotUnlocked(RoomTeam team, uint8_t slotIndex)
{
    if (team == RoomTeam::A)
        return slotIndex < ROOM_A_SLOT_COUNT;

    if (team == RoomTeam::B)
        return slotIndex < ROOM_B_SLOT_COUNT;

    if (team == RoomTeam::OBSERVER)
        return slotIndex < ROOM_OBSERVER_SLOT_COUNT;

    return false;
}

bool RoomManager::TransferLeader(
    uint32_t roomId,
    uint32_t currentLeaderPlayerId,
    uint32_t newLeaderPlayerId,
    Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.leaderPlayerId != currentLeaderPlayerId)
        return false;

    RoomMember* newLeader = nullptr;

    for (RoomMember& member : room.members)
    {
        if (member.playerId == newLeaderPlayerId)
        {
            newLeader = &member;
            break;
        }
    }

    if (newLeader == nullptr)
        return false;

    for (RoomMember& member : room.members)
        member.isLeader = false;

    newLeader->isLeader = true;
    room.leaderPlayerId = newLeaderPlayerId;

    outRoom = room;
    return true;
}

bool RoomManager::KickMember(
    uint32_t roomId,
    uint32_t leaderPlayerId,
    uint32_t targetPlayerId,
    bool& outRoomDeleted,
    Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    outRoomDeleted = false;
    outRoom = Room{};

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.leaderPlayerId != leaderPlayerId)
        return false;

    if (leaderPlayerId == targetPlayerId)
        return false;

    auto targetIt = std::find_if(
        room.members.begin(),
        room.members.end(),
        [targetPlayerId](const RoomMember& member)
        {
            return member.playerId == targetPlayerId;
        }
    );

    if (targetIt == room.members.end())
        return false;

    room.members.erase(targetIt);
    room.currentPlayerCount = static_cast<uint16_t>(room.members.size());

    if (room.members.empty())
    {
        rooms_.erase(it);
        outRoomDeleted = true;
        return true;
    }

    outRoom = room;
    return true;
}