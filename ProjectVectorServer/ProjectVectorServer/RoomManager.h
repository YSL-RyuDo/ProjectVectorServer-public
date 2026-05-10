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

    bool EnterRoom(
        uint32_t roomId,
        const RoomMember& member,
        Room& outRoom
    );

    bool LeaveRoom(
        uint32_t roomId,
        uint32_t playerId,
        bool& outRoomDeleted,
        Room& outRoom
    );

    bool CanStartGame(uint32_t roomId);

    bool StartGame(
        uint32_t roomId,
        uint32_t leaderPlayerId,
        Room& outRoom
    );

    bool IsLeader(uint32_t roomId, uint32_t playerId);

    bool GetMember(
        uint32_t roomId,
        uint32_t playerId,
        RoomMember& outMember
    );

    bool GetMemberBySlot(
        uint32_t roomId,
        RoomTeam team,
        uint8_t slotIndex,
        RoomMember& outMember
    );

    bool MoveMemberToSlot(
        uint32_t roomId,
        uint32_t playerId,
        RoomTeam targetTeam,
        uint8_t targetSlotIndex,
        Room& outRoom
    );

    bool SwapMembers(
        uint32_t roomId,
        uint32_t playerAId,
        uint32_t playerBId,
        Room& outRoom
    );

    bool TransferLeader(
        uint32_t roomId,
        uint32_t currentLeaderPlayerId,
        uint32_t newLeaderPlayerId,
        Room& outRoom
    );

    bool KickMember(
        uint32_t roomId,
        uint32_t leaderPlayerId,
        uint32_t targetPlayerId,
        bool& outRoomDeleted,
        Room& outRoom
    );

    bool SetPickHover(
        uint32_t roomId,
        uint32_t playerId,
        int characterId,
        Room& outRoom
    );

    bool LockPick(
        uint32_t roomId,
        uint32_t playerId,
        int characterId,
        Room& outRoom,
        uint8_t& outResult
    );

    bool SetPickRemainingSeconds(
        uint32_t roomId,
        uint16_t remainingSeconds,
        Room& outRoom
    );

    bool ForceLockMissingPicks(
        uint32_t roomId,
        Room& outRoom
    );

    bool SetPickReady(
        uint32_t roomId,
        uint32_t playerId,
        Room& outRoom
    );

    bool IsAllPickReady(
        uint32_t roomId
    );

    bool TryStartPickTimer(
        uint32_t roomId,
        Room& outRoom
    );

private:
    int CountTeamMemberUnlocked(const Room& room, RoomTeam team);

    bool IsPlayerInRoomUnlocked(const Room& room, uint32_t playerId);

    bool FindNextSlotUnlocked(
        const Room& room,
        RoomTeam& outTeam,
        uint8_t& outSlotIndex
    );

    RoomMember* FindFirstMemberByLeaderOrderUnlocked(Room& room);
    bool IsValidSlotUnlocked(RoomTeam team, uint8_t slotIndex);

    bool IsValidCharacterIdUnlocked(int characterId);

    bool IsCharacterLockedByTeamUnlocked(
        const Room& room,
        RoomTeam team,
        int characterId
    );

    void ClearSameHoverCharacterUnlocked(
        Room& room,
        RoomTeam team,
        int characterId,
        uint32_t exceptPlayerId
    );

    int FindLowestAvailableCharacterUnlocked(
        const bool usedCharacters[4]
    );

private:
    std::unordered_map<uint32_t, Room> rooms_;
    std::mutex roomManagerMutex_;
    std::atomic<uint32_t> nextRoomId_{ 1 };
};