#include "RoomManager.h"

#include <algorithm>

namespace
{
    constexpr uint8_t ROOM_A_SLOT_COUNT = 3;
    constexpr uint8_t ROOM_B_SLOT_COUNT = 3;
    constexpr uint8_t ROOM_OBSERVER_SLOT_COUNT = 2;

    constexpr int PICK_CHARACTER_COUNT = 4;
    constexpr int PICK_CHARACTER_MIN_ID = 0;
    constexpr int PICK_CHARACTER_MAX_ID = 3;

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

    enum PickLockResultValue : uint8_t
    {
        PICK_SUCCESS = 0,
        PICK_NOT_IN_GAME = 1,
        PICK_ROOM_NOT_FOUND = 2,
        PICK_INVALID_CHARACTER = 3,
        PICK_ALREADY_LOCKED = 4,
        PICK_CHARACTER_ALREADY_TAKEN = 5,
        PICK_FAILED = 6
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

uint32_t RoomManager::CreateRoom(const std::string& roomName,const std::string& mapName,const std::string& hostName,const RoomMember& leaderMember,uint16_t maxPlayerCount)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    Room room;
    room.roomId = nextRoomId_.fetch_add(1);
    room.roomName = roomName;
    room.mapName = mapName;
    room.hostName = hostName;
    room.maxPlayerCount = maxPlayerCount;
    room.state = RoomState::WAITING;
    room.pickRemainingSeconds = 20;
    room.pickFinished = false;

    RoomMember leader = leaderMember;
    leader.team = RoomTeam::A;
    leader.slotIndex = 0;
    leader.isLeader = true;

    leader.hoverCharacterId = -1;
    leader.lockedCharacterId = -1;
    leader.isPickLocked = false;

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

bool RoomManager::EnterRoom(uint32_t roomId,const RoomMember& member,Room& outRoom)
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

    newMember.hoverCharacterId = -1;
    newMember.lockedCharacterId = -1;
    newMember.isPickLocked = false;

    room.members.push_back(newMember);
    room.currentPlayerCount = static_cast<uint16_t>(room.members.size());

    outRoom = room;

    return true;
}

bool RoomManager::LeaveRoom(uint32_t roomId,uint32_t playerId,bool& outRoomDeleted,Room& outRoom)
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

bool RoomManager::StartGame(uint32_t roomId,uint32_t leaderPlayerId,Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.state != RoomState::WAITING)
        return false;

    if (room.leaderPlayerId != leaderPlayerId)
        return false;

    int aCount = CountTeamMemberUnlocked(room, RoomTeam::A);
    int bCount = CountTeamMemberUnlocked(room, RoomTeam::B);

    if (aCount != 3 || bCount != 3)
        return false;

    room.state = RoomState::PLAYING;

    room.pickRemainingSeconds = 20;
    room.pickFinished = false;
    room.pickTimerStarted = false;

    room.gamePrepareRemainingSeconds = 20;
    room.gamePrepareTimerStarted = false;

    for (RoomMember& member : room.members)
    {
        member.hoverCharacterId = -1;
        member.lockedCharacterId = -1;
        member.isPickLocked = false;
        member.isPickReady = false;
        member.isGameLoadReady = false;
    }

    outRoom = room;
    return true;
}

bool RoomManager::IsLeader(uint32_t roomId, uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    return it->second.leaderPlayerId == playerId;
}

bool RoomManager::GetMember(uint32_t roomId,uint32_t playerId,RoomMember& outMember)
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

bool RoomManager::GetMemberBySlot(uint32_t roomId,RoomTeam team,uint8_t slotIndex,RoomMember& outMember)
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

bool RoomManager::MoveMemberToSlot(uint32_t roomId,uint32_t playerId,RoomTeam targetTeam,uint8_t targetSlotIndex,Room& outRoom)
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

bool RoomManager::SwapMembers(uint32_t roomId,uint32_t playerAId,uint32_t playerBId,Room& outRoom)
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

bool RoomManager::TransferLeader(uint32_t roomId,uint32_t currentLeaderPlayerId,uint32_t newLeaderPlayerId,Room& outRoom)
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

bool RoomManager::KickMember(uint32_t roomId,uint32_t leaderPlayerId,uint32_t targetPlayerId,bool& outRoomDeleted,Room& outRoom)
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

bool RoomManager::SetPickHover(uint32_t roomId,uint32_t playerId,int characterId,Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.state != RoomState::PLAYING)
        return false;

    if (room.pickFinished)
        return false;

    if (!IsValidCharacterIdUnlocked(characterId))
        return false;

    RoomMember* targetMember = nullptr;

    for (RoomMember& member : room.members)
    {
        if (member.playerId == playerId)
        {
            targetMember = &member;
            break;
        }
    }

    if (targetMember == nullptr)
        return false;

    if (targetMember->team == RoomTeam::OBSERVER)
        return false;

    if (targetMember->isPickLocked)
        return false;

    if (IsCharacterLockedByTeamUnlocked(room, targetMember->team, characterId))
        return false;

    targetMember->hoverCharacterId = characterId;

    outRoom = room;
    return true;
}

bool RoomManager::LockPick(uint32_t roomId,uint32_t playerId,int characterId,Room& outRoom,uint8_t& outResult)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    outResult = PICK_FAILED;
    outRoom = Room{};

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
    {
        outResult = PICK_ROOM_NOT_FOUND;
        return false;
    }

    Room& room = it->second;

    if (room.state != RoomState::PLAYING)
    {
        outResult = PICK_NOT_IN_GAME;
        return false;
    }

    if (room.pickFinished)
    {
        outResult = PICK_NOT_IN_GAME;
        return false;
    }

    if (!IsValidCharacterIdUnlocked(characterId))
    {
        outResult = PICK_INVALID_CHARACTER;
        return false;
    }

    RoomMember* targetMember = nullptr;

    for (RoomMember& member : room.members)
    {
        if (member.playerId == playerId)
        {
            targetMember = &member;
            break;
        }
    }

    if (targetMember == nullptr || targetMember->team == RoomTeam::OBSERVER)
    {
        outResult = PICK_NOT_IN_GAME;
        return false;
    }

    if (targetMember->isPickLocked)
    {
        outResult = PICK_ALREADY_LOCKED;
        return false;
    }

    if (IsCharacterLockedByTeamUnlocked(room, targetMember->team, characterId))
    {
        outResult = PICK_CHARACTER_ALREADY_TAKEN;
        return false;
    }

    targetMember->hoverCharacterId = characterId;
    targetMember->lockedCharacterId = characterId;
    targetMember->isPickLocked = true;

    ClearSameHoverCharacterUnlocked(
        room,
        targetMember->team,
        characterId,
        playerId
    );

    outResult = PICK_SUCCESS;
    outRoom = room;

    return true;
}

bool RoomManager::SetPickRemainingSeconds(uint32_t roomId,uint16_t remainingSeconds,Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.state != RoomState::PLAYING)
        return false;

    if (room.pickFinished)
        return false;

    room.pickRemainingSeconds = remainingSeconds;

    outRoom = room;
    return true;
}

bool RoomManager::ForceLockMissingPicks(uint32_t roomId,Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.state != RoomState::PLAYING)
        return false;

    if (room.pickFinished)
    {
        outRoom = room;
        return true;
    }

    RoomTeam teams[2] = { RoomTeam::A, RoomTeam::B };

    for (RoomTeam team : teams)
    {
        bool usedCharacters[PICK_CHARACTER_COUNT] = { false, false, false, false };

        for (const RoomMember& member : room.members)
        {
            if (member.team != team)
                continue;

            if (!member.isPickLocked)
                continue;

            if (IsValidCharacterIdUnlocked(member.lockedCharacterId))
                usedCharacters[member.lockedCharacterId] = true;
        }

        std::vector<RoomMember*> teamMembers;

        for (RoomMember& member : room.members)
        {
            if (member.team != team)
                continue;

            if (member.slotIndex >= 3)
                continue;

            teamMembers.push_back(&member);
        }

        std::sort(
            teamMembers.begin(),
            teamMembers.end(),
            [](const RoomMember* a, const RoomMember* b)
            {
                return a->slotIndex < b->slotIndex;
            }
        );

        for (RoomMember* member : teamMembers)
        {
            if (member == nullptr)
                continue;

            if (member->isPickLocked)
                continue;

            int hoverId = member->hoverCharacterId;

            if (!IsValidCharacterIdUnlocked(hoverId))
                continue;

            if (usedCharacters[hoverId])
                continue;

            member->hoverCharacterId = hoverId;
            member->lockedCharacterId = hoverId;
            member->isPickLocked = true;

            usedCharacters[hoverId] = true;
        }

        for (RoomMember* member : teamMembers)
        {
            if (member == nullptr)
                continue;

            if (member->isPickLocked)
                continue;

            int pickId = static_cast<int>(member->slotIndex);

            if (!IsValidCharacterIdUnlocked(pickId) || usedCharacters[pickId])
            {
                pickId = FindLowestAvailableCharacterUnlocked(usedCharacters);
            }

            if (!IsValidCharacterIdUnlocked(pickId))
                continue;

            member->hoverCharacterId = pickId;
            member->lockedCharacterId = pickId;
            member->isPickLocked = true;

            usedCharacters[pickId] = true;
        }
    }

    room.pickRemainingSeconds = 0;
    room.pickFinished = true;

    outRoom = room;
    return true;
}

bool RoomManager::SetPickReady(uint32_t roomId,uint32_t playerId,Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.state != RoomState::PLAYING)
        return false;

    for (RoomMember& member : room.members)
    {
        if (member.playerId == playerId)
        {
            member.isPickReady = true;
            outRoom = room;
            return true;
        }
    }

    return false;
}

bool RoomManager::IsAllPickReady(uint32_t roomId)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    const Room& room = it->second;

    int readyCount = 0;
    int playMemberCount = 0;

    for (const RoomMember& member : room.members)
    {
        if (member.team == RoomTeam::OBSERVER)
            continue;

        playMemberCount++;

        if (member.isPickReady)
            readyCount++;
    }

    return playMemberCount == 6 && readyCount == 6;
}

bool RoomManager::TryStartPickTimer(uint32_t roomId,Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.state != RoomState::PLAYING)
        return false;

    if (room.pickFinished)
        return false;

    if (room.pickTimerStarted)
        return false;

    int readyCount = 0;
    int playMemberCount = 0;

    for (const RoomMember& member : room.members)
    {
        if (member.team == RoomTeam::OBSERVER)
            continue;

        playMemberCount++;

        if (member.isPickReady)
            readyCount++;
    }

    if (playMemberCount != 6 || readyCount != 6)
        return false;

    room.pickTimerStarted = true;
    room.pickRemainingSeconds = 20;

    outRoom = room;
    return true;
}

bool RoomManager::SetGameLoadReady(uint32_t roomId, uint32_t playerId, Room& outRoom)
{
	std::lock_guard<std::mutex> lock(roomManagerMutex_);

	auto it = rooms_.find(roomId);
	if (it == rooms_.end())
		return false;

	Room& room = it->second;

	if (room.state != RoomState::PLAYING)
		return false;

    for (RoomMember& member : room.members)
    {
		if (member.playerId != playerId)
			continue;

		if (member.team == RoomTeam::OBSERVER)
			return false;

		member.isGameLoadReady = true;
		outRoom = room;
		return true;
    }

    return false;
}

bool RoomManager::TryStartGamePrepare(uint32_t roomId, Room& outRoom)
{
	std::lock_guard<std::mutex> lock(roomManagerMutex_);

	auto it = rooms_.find(roomId);
	if (it == rooms_.end())
		return false;

	Room& room = it->second;

	if (room.state != RoomState::PLAYING)
		return false;

	if (room.gamePrepareTimerStarted)
		return false;

	int readyCount = 0;
	int playMemberCount = 0;

	for (const RoomMember& member : room.members)
	{
		if (member.team == RoomTeam::OBSERVER)
			continue;

		playMemberCount++;

		if (member.isGameLoadReady)
			readyCount++;
	}

	if (playMemberCount != 6 || readyCount != 6)
		return false;

	room.gamePrepareTimerStarted = true;
	room.gamePrepareRemainingSeconds = 20;

	outRoom = room;
	return true;
}

bool RoomManager::SetGamePrepareRemainingSeconds(uint32_t roomId, uint16_t remainingSeconds, Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    if (room.state != RoomState::PLAYING)
        return false;

    if (!room.gamePrepareTimerStarted)
        return false;

    room.gamePrepareRemainingSeconds = remainingSeconds;

    outRoom = room;
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

bool RoomManager::IsPlayerInRoomUnlocked(const Room& room, uint32_t playerId)
{
    for (const RoomMember& member : room.members)
    {
        if (member.playerId == playerId)
            return true;
    }

    return false;
}

bool RoomManager::FindNextSlotUnlocked(const Room& room,RoomTeam& outTeam,uint8_t& outSlotIndex)
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

bool RoomManager::IsValidCharacterIdUnlocked(int characterId)
{
    return characterId >= PICK_CHARACTER_MIN_ID &&
        characterId <= PICK_CHARACTER_MAX_ID;
}

bool RoomManager::IsCharacterLockedByTeamUnlocked(const Room& room,RoomTeam team,int characterId)
{
    for (const RoomMember& member : room.members)
    {
        if (member.team != team)
            continue;

        if (!member.isPickLocked)
            continue;

        if (member.lockedCharacterId == characterId)
            return true;
    }

    return false;
}

void RoomManager::ClearSameHoverCharacterUnlocked(Room& room,RoomTeam team,int characterId,uint32_t exceptPlayerId)
{
    for (RoomMember& member : room.members)
    {
        if (member.playerId == exceptPlayerId)
            continue;

        if (member.team != team)
            continue;

        if (member.isPickLocked)
            continue;

        if (member.hoverCharacterId == characterId)
            member.hoverCharacterId = -1;
    }
}

int RoomManager::FindLowestAvailableCharacterUnlocked(const bool usedCharacters[4])
{
    for (int i = 0; i < PICK_CHARACTER_COUNT; ++i)
    {
        if (!usedCharacters[i])
            return i;
    }

    return -1;
}

bool RoomManager::FinishGameAndReturnRoom(uint32_t roomId, Room& outRoom)
{
    std::lock_guard<std::mutex> lock(roomManagerMutex_);

    auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;

    Room& room = it->second;

    room.state = RoomState::WAITING;

    room.pickRemainingSeconds = 20;
    room.pickFinished = false;
    room.pickTimerStarted = false;

    room.gamePrepareRemainingSeconds = 20;
    room.gamePrepareTimerStarted = false;

    for (RoomMember& member : room.members)
    {
        member.hoverCharacterId = -1;
        member.lockedCharacterId = -1;
        member.isPickLocked = false;
        member.isPickReady = false;
        member.isGameLoadReady = false;
    }

    outRoom = room;
    return true;
}