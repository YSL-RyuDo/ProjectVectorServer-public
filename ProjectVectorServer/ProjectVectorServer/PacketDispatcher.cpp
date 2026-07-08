#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "RoomManager.h"
#include "ServerWeaponData.h"

#include<fstream>
#include<sstream>
#include <cstring>
#include <windows.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <algorithm>
#include <vector>
#include <unordered_set>

namespace
{
    constexpr uint16_t PICK_TIME_SECONDS = 20;
    constexpr uint16_t PREPARE_TIME_SECONDS = 10;
    constexpr uint16_t GAME_PREPARE_SECONDS = 20;

    constexpr uint16_t ROUND_END_DELAY_SECONDS = 6;
    constexpr uint16_t ROUND_PLAY_SECONDS = 100;
    constexpr uint16_t SPIKE_EXPLODE_SECONDS = 45;

    // 정규전 기준 6선승
    constexpr uint8_t MATCH_WIN_ROUND_COUNT = 6;
    // 5라운드 종료 후 공수 전환
    constexpr uint8_t SIDE_SWAP_AFTER_ROUND = 5;
    //5:5 이후 11라운드부터 듀스
    constexpr uint8_t OVERTIME_START_ROUND = 11;

    constexpr uint8_t OVERTIME_TRIGGER_SCORE = MATCH_WIN_ROUND_COUNT - 1;
    constexpr uint8_t SECOND_HALF_END_ROUND = OVERTIME_START_ROUND - 1;
    constexpr int OVERTIME_ROUND_CREDIT = 6000;

    bool IsOvertimeScore(uint8_t scoreA, uint8_t scoreB)
    {
        return scoreA == OVERTIME_TRIGGER_SCORE &&
            scoreB == OVERTIME_TRIGGER_SCORE;
    }

    bool IsFirstHalfRound(uint8_t currentRound)
    {
        return currentRound <= SIDE_SWAP_AFTER_ROUND;
    }

    bool IsSecondHalfRound(uint8_t currentRound)
    {
        return currentRound > SIDE_SWAP_AFTER_ROUND &&
            currentRound < OVERTIME_START_ROUND;
    }

    bool IsOvertimeRound(uint8_t currentRound)
    {
        return currentRound >= OVERTIME_START_ROUND;
    }

    struct MatchRuntimeState
    {
        uint8_t currentRound = 1;
        uint8_t scoreA = 0;
        uint8_t scoreB = 0;

        uint8_t loseStreakA = 0;
        uint8_t loseStreakB = 0;

        uint32_t spikeCarrierPlayerId = 0;
        bool spikePlanted = false;

        uint32_t spikeDefuserPlayerId = 0;
        bool spikeHalfDefused = false;

        // 설치할 때마다 증가하는 고유 번호.
        // 이전 라운드의 분리된 타이머 스레드가 다음 라운드 스파이크를
        // 잘못 폭발시키는 것을 막는다.
        uint64_t spikePlantGeneration = 0;

        bool buyPhase = true;

        bool isOvertime = false;
        bool roundResolving = false;
        bool matchEnded = false;
    };

    std::mutex g_matchStateMutex;
    std::unordered_map<uint32_t, MatchRuntimeState> g_matchStates;

    std::mutex g_gameLoadReadyMutex;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> g_gameLoadReadyPlayers;
    std::unordered_set<uint32_t> g_gamePrepareStartedRooms;

    std::mutex g_resultLeaveMutex;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> g_resultLeavePlayers;

    std::unordered_map<std::string, int> g_creditRewards;

    struct StoreWeaponData
    {
        int weaponId = 0;
        int price = 0;
        bool purchasable = true;
    };

    struct StoreArmorData
    {
        int armorId = 0;
        int price = 0;
        int barrier = 0;
    };

    const std::unordered_map<int, StoreWeaponData> g_storeWeapons =
    {
        // 보조무기
        { 0,  { 0,  0,    true } }, // P92 기본 권총
        { 1,  { 1,  400,  true } }, // P1911
        { 2,  { 2,  800,  true } }, // R1895

        // SMG
        { 3,  { 3,  1000, true } }, // UZI
        { 4,  { 4,  1500, true } }, // UMP

        // Shotgun
        { 5,  { 5,  900,  true } }, // S1897
        { 6,  { 6,  1800, true } }, // AA-12

        // DMR
        { 7,  { 7,  2000, true } }, // Dragunov

        // AR
        { 8,  { 8,  2500, true } }, // M416
        { 9,  { 9,  2500, true } }, // AK47

        // LMG
        { 10, { 10, 3000, true } }, // M249

        // SR
        { 11, { 11, 1000, true } }, // Kar98
        { 12, { 12, 4000, true } }, // AWM

        // Knife는 상점 구매 불가
        { 13, { 13, 0,    false } }
    };

    const std::unordered_map<int, StoreArmorData> g_storeArmors =
    {
        { 0, { 0, 500,  50 } },  // 경량 베리어
        { 1, { 1, 1000, 100 } }  // 중량 베리어    
    };

    std::once_flag g_creditRewardLoadOnce;

    constexpr const char* CREDIT_REWARD_CSV_PATH = "CreditRewardTable.csv";

    void CreateDefaultCreditRewardCsvIfMissing()
    {
        std::ifstream check(CREDIT_REWARD_CSV_PATH);

        if (check.good())
            return;

        std::ofstream out(CREDIT_REWARD_CSV_PATH);

        out << "key,credit\n";
        out << "ROUND_START_HALF,800\n";
        out << "KILL,300\n";
        out << "SPIKE_PLANT,300\n";
        out << "ROUND_WIN,2500\n";
        out << "LOSE_STREAK_1,1800\n";
        out << "LOSE_STREAK_2,2100\n";
        out << "LOSE_STREAK_3_PLUS,2400\n";
        out << "SURVIVE_DEFEAT,1000\n";
        out << "OVERTIME,5000\n";

        std::cout << "[CREDIT CSV CREATED] path="
            << CREDIT_REWARD_CSV_PATH
            << std::endl;
    }

    void LoadCreditRewardTable()
    {
        CreateDefaultCreditRewardCsvIfMissing();

        std::ifstream file(CREDIT_REWARD_CSV_PATH);

        if (!file.is_open())
        {
            std::cout << "[CREDIT CSV LOAD FAILED] path="
                << CREDIT_REWARD_CSV_PATH
                << std::endl;
            return;
        }

        std::string line;

        std::getline(file, line);
        while (std::getline(file, line))
        {
            if (line.empty())
                continue;

            std::stringstream ss(line);

            std::string key;
            std::string creditText;

            std::getline(ss, key, ',');
            std::getline(ss, creditText, ',');

            if (key.empty() || creditText.empty())
                continue;

            int credit = std::stoi(creditText);
            g_creditRewards[key] = credit;

            std::cout << "[CREDIT REWARD LOADED] key="
                << key
                << " credit=" << credit
                << std::endl;
        }
    }

    int GetCreditReward(const std::string& key)
    {
        std::call_once(g_creditRewardLoadOnce, LoadCreditRewardTable);

        auto it = g_creditRewards.find(key);

        if (it != g_creditRewards.end())
            return it->second;

        std::cout << "[CREDIT REWARD MISSING] key="
            << key
            << std::endl;

        return 0;
    }

    void TryResolveRoundByElimination(uint32_t roomId);
    void StartRoundEndThread(uint32_t roomId, RoomTeam winnerTeam, bool matchEnd);
    void ResetRound(uint32_t roomId);

    bool IsKnifeWeapon(int weaponId);
    bool IsPistolWeapon(int weaponId);
    bool IsPrimaryWeapon(int weaponId);

    bool FindOwnedWeaponByPredicate(
        Session* session,
        bool (*predicate)(int),
        PlayerWeaponState& outWeapon);

    void FillRoundResetWeaponSlots(
        RoundResetMemberInfo& info,
        Session* session);

    void ClearDroppedWeaponsInRoom(uint32_t roomId);

    void StartRoundTimerThread(uint32_t roomId);
    void ResolveRoundByTimeOut(uint32_t roomId);
    RoomTeam GetDefenseTeam(uint8_t currentRound);

    RoomTeam GetAttackTeam(uint8_t currentRound);

    void BroadcastSpikePickupNotify(uint32_t roomId, uint32_t carrierPlayerId);
    void BroadcastSpikePlanted(uint32_t roomId, float x, float y, float z);

    void StartSpikeTimerThread(uint32_t roomId, uint64_t spikeGeneration);
    void ResolveRoundBySpikeExplode(uint32_t roomId, uint64_t spikeGeneration);

    void ApplyRoundEndCredits(uint32_t roomId, RoomTeam winnerTeam, RoundWinReason winReason);

    void SetAllPlayerCreditsInRoom(uint32_t roomId, int credit);

    void BroadcastSpikeDefuseState(uint32_t roomId, bool halfDefused, uint32_t defuserPlayerId);

    bool IsDroneActiveForPlayer(uint32_t playerId);
    void DestroyDroneForPlayer(uint32_t ownerPlayerId, uint8_t reason);
    void ClearDronesInRoom(uint32_t roomId, uint8_t reason);
    bool IsDroneBlockedAction(Session* session, const char* actionName);

    void ResetMatchRuntimeState(uint32_t roomId)
    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        MatchRuntimeState state{};
        state.currentRound = 1;
        state.scoreA = 0;
        state.scoreB = 0;

        state.loseStreakA = 0;
        state.loseStreakB = 0;

        state.spikeCarrierPlayerId = 0;
        state.spikePlanted = false;

        state.spikeDefuserPlayerId = 0;
        state.spikeHalfDefused = false;
        state.spikePlantGeneration = 0;

        state.buyPhase = true;

        state.isOvertime = false;
        state.roundResolving = false;
        state.matchEnded = false;

        g_matchStates[roomId] = state;

        std::cout << "[MATCH STATE INIT] roomId="
            << roomId
            << std::endl;
    }

    void BroadcastMatchLoadingInfo(const Room& room);
    void BroadcastMatchStart(const Room& room);

    void BroadcastGameAllReady(const Room& room);
    void BroadcastGamePrepareTimer(const Room& room, uint16_t remainingSeconds);
    void StartGamePrepareTimerThread(uint32_t roomId);

    void BroadcastPickTimer(const Room& room, PickPhase phase, uint16_t remainingSeconds)
    {
        SC_PickTimer out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PICK_TIMER);

        out.phase = static_cast<uint8_t>(phase);
        out.remainingSeconds = remainingSeconds;

        for (const RoomMember& member : room.members)
        {
            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[PICK TIMER BROADCAST] roomId="
            << room.roomId
            << " phase=" << static_cast<int>(phase)
            << " remaining=" << remainingSeconds
            << std::endl;
    }
    std::string WideToUTF8(const wchar_t* wstr)
    {
        if (wstr == nullptr)
            return "";

        int sizeNeeded = WideCharToMultiByte(
            CP_UTF8,
            0,
            wstr,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr
        );

        if (sizeNeeded <= 0)
            return "";

        std::string result;
        result.resize(sizeNeeded);

        int converted = WideCharToMultiByte(
            CP_UTF8,
            0,
            wstr,
            -1,
            &result[0],
            sizeNeeded,
            nullptr,
            nullptr
        );

        if (converted <= 0)
            return "";

        if (!result.empty() && result.back() == '\0')
            result.pop_back();

        return result;
    }

    void FillRoomMemberInfo(RoomMemberInfo& out, const RoomMember& member)
    {
        out.playerId = member.playerId;

        memset(out.nickname, 0, sizeof(out.nickname));
        strncpy_s(out.nickname, sizeof(out.nickname), member.nickname.c_str(), _TRUNCATE);

        out.level = member.level;
        out.playerIconId = member.playerIconId;

        out.team = static_cast<uint8_t>(member.team);
        out.slotIndex = member.slotIndex;
        out.isLeader = member.isLeader ? 1 : 0;
    }

    void FillRoomCreateResultPacket(SC_RoomCreateResult& out, const Room& room, uint8_t result, uint32_t receiverPlayerId)
    {
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_CREATE_RESULT);

        out.result = result;
        out.roomId = room.roomId;

        memset(out.roomName, 0, ROOM_NAME_LEN);
        memset(out.mapName, 0, MAP_NAME_LEN);

        strncpy_s(out.roomName, ROOM_NAME_LEN, room.roomName.c_str(), _TRUNCATE);
        strncpy_s(out.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);

        out.isLeader = room.leaderPlayerId == receiverPlayerId ? 1 : 0;
        out.canStart = RoomManager::Instance().CanStartGame(room.roomId) ? 1 : 0;

        uint8_t count = static_cast<uint8_t>(room.members.size());
        if (count > ROOM_MAX_MEMBER_COUNT)
            count = ROOM_MAX_MEMBER_COUNT;

        out.memberCount = count;

        for (uint8_t i = 0; i < count; ++i)
        {
            FillRoomMemberInfo(out.members[i], room.members[i]);
        }
    }

    void FillRoomStatePacket(SC_RoomState& out, const Room& room)
    {
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_STATE);

        out.roomId = room.roomId;

        memset(out.roomName, 0, ROOM_NAME_LEN);
        memset(out.mapName, 0, MAP_NAME_LEN);

        strncpy_s(out.roomName, ROOM_NAME_LEN, room.roomName.c_str(), _TRUNCATE);
        strncpy_s(out.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);

        out.canStart = RoomManager::Instance().CanStartGame(room.roomId) ? 1 : 0;

        uint8_t count = static_cast<uint8_t>(room.members.size());
        if (count > ROOM_MAX_MEMBER_COUNT)
            count = ROOM_MAX_MEMBER_COUNT;

        out.memberCount = count;

        for (uint8_t i = 0; i < count; ++i)
        {
            FillRoomMemberInfo(out.members[i], room.members[i]);
        }
    }

    void BroadcastRoomState(const Room& room)
    {
        SC_RoomState out{};
        FillRoomStatePacket(out, room);

        for (const RoomMember& member : room.members)
        {
            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[ROOM STATE BROADCAST] roomId=" << room.roomId
            << " memberCount=" << (int)out.memberCount
            << std::endl;
    }

    void SendRoomGameStartResult(Session* session, RoomGameStartResult result)
    {
        if (session == nullptr)
            return;

        SC_RoomGameStartResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_GAME_START_RESULT);
        out.result = static_cast<uint8_t>(result);

        session->Send(&out);
    }

    void BroadcastRoomGameStartResult(const Room& room, RoomGameStartResult result)
    {
        SC_RoomGameStartResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_GAME_START_RESULT);
        out.result = static_cast<uint8_t>(result);

        for (const RoomMember& member : room.members)
        {
            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[ROOM GAME START BROADCAST] roomId="
            << room.roomId
            << " result=" << static_cast<int>(result)
            << " memberCount=" << room.members.size()
            << std::endl;
    }

    struct PendingSlotSwap
    {
        uint32_t roomId = 0;

        uint32_t requesterPlayerId = 0;
        uint32_t targetPlayerId = 0;

        RoomTeam requesterTeam = RoomTeam::A;
        uint8_t requesterSlotIndex = 0;

        RoomTeam targetTeam = RoomTeam::A;
        uint8_t targetSlotIndex = 0;
    };

    std::mutex g_pendingSlotSwapMutex;
    std::unordered_map<uint32_t, PendingSlotSwap> g_pendingSlotSwapsByTarget;

    bool AddPendingSlotSwap(const PendingSlotSwap& request)
    {
        std::lock_guard<std::mutex> lock(g_pendingSlotSwapMutex);

        if (g_pendingSlotSwapsByTarget.count(request.targetPlayerId) > 0)
            return false;

        g_pendingSlotSwapsByTarget[request.targetPlayerId] = request;
        return true;
    }

    bool TakePendingSlotSwap(uint32_t targetPlayerId, uint32_t requesterPlayerId, PendingSlotSwap& outRequest)
    {
        std::lock_guard<std::mutex> lock(g_pendingSlotSwapMutex);

        auto it = g_pendingSlotSwapsByTarget.find(targetPlayerId);
        if (it == g_pendingSlotSwapsByTarget.end())
            return false;

        if (it->second.requesterPlayerId != requesterPlayerId)
            return false;

        outRequest = it->second;
        g_pendingSlotSwapsByTarget.erase(it);

        return true;
    }

    void SendRoomSlotChangeResult(Session* session, RoomSlotChangeResult result, RoomTeam targetTeam, uint8_t targetSlotIndex, const std::string& targetNickname = "")
    {
        if (session == nullptr)
            return;

        SC_RoomSlotChangeResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_SLOT_CHANGE_RESULT);

        out.result = static_cast<uint8_t>(result);
        out.targetTeam = static_cast<uint8_t>(targetTeam);
        out.targetSlotIndex = targetSlotIndex;

        memset(out.targetNickname, 0, sizeof(out.targetNickname));
        if (!targetNickname.empty())
            strncpy_s(out.targetNickname, sizeof(out.targetNickname), targetNickname.c_str(), _TRUNCATE);

        session->Send(&out);
    }

    void SendRoomTransferLeaderResult(Session* session, RoomAdminResult result, uint32_t targetPlayerId, const std::string& targetNickname = "")
    {
        if (session == nullptr)
            return;

        SC_RoomTransferLeaderResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_TRANSFER_LEADER_RESULT);

        out.result = static_cast<uint8_t>(result);
        out.targetPlayerId = targetPlayerId;

        memset(out.targetNickname, 0, sizeof(out.targetNickname));
        if (!targetNickname.empty())
            strncpy_s(out.targetNickname, sizeof(out.targetNickname), targetNickname.c_str(), _TRUNCATE);

        session->Send(&out);
    }

    void SendRoomKickResult(Session* session, RoomAdminResult result, uint32_t targetPlayerId, const std::string& targetNickname = "")
    {
        if (session == nullptr)
            return;

        SC_RoomKickResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_KICK_RESULT);

        out.result = static_cast<uint8_t>(result);
        out.targetPlayerId = targetPlayerId;

        memset(out.targetNickname, 0, sizeof(out.targetNickname));
        if (!targetNickname.empty())
            strncpy_s(out.targetNickname, sizeof(out.targetNickname), targetNickname.c_str(), _TRUNCATE);

        session->Send(&out);
    }

    void SendRoomKicked(Session* session, uint32_t roomId, const std::string& kickerNickname)
    {
        if (session == nullptr)
            return;

        SC_RoomKicked out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_KICKED);

        out.roomId = roomId;

        memset(out.kickerNickname, 0, sizeof(out.kickerNickname));
        strncpy_s(out.kickerNickname, sizeof(out.kickerNickname), kickerNickname.c_str(), _TRUNCATE);

        session->Send(&out);
    }

    void FillPickTeamMemberInfo(PickTeamMemberInfo& out, const RoomMember& member, uint32_t receiverPlayerId)
    {
        out.playerId = member.playerId;

        memset(out.nickname, 0, sizeof(out.nickname));
        strncpy_s(out.nickname, sizeof(out.nickname), member.nickname.c_str(), _TRUNCATE);

        out.slotIndex = member.slotIndex;

        out.hoverCharacterId = member.hoverCharacterId;
        out.lockedCharacterId = member.lockedCharacterId;

        out.isLocked = member.isPickLocked ? 1 : 0;
        out.isMe = member.playerId == receiverPlayerId ? 1 : 0;
    }

    void FillPickStatePacket(SC_PickState& out, const Room& room, const RoomMember& receiverMember, uint32_t receiverPlayerId)
    {
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PICK_STATE);

        out.roomId = room.roomId;

        memset(out.mapName, 0, MAP_NAME_LEN);
        strncpy_s(out.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);

        out.myTeam = static_cast<uint8_t>(receiverMember.team);

        if (receiverMember.team == RoomTeam::A)
        {
            out.firstRole = static_cast<uint8_t>(PickRole::ATTACK);
            out.secondRole = static_cast<uint8_t>(PickRole::DEFENSE);
        }
        else if (receiverMember.team == RoomTeam::B)
        {
            out.firstRole = static_cast<uint8_t>(PickRole::DEFENSE);
            out.secondRole = static_cast<uint8_t>(PickRole::ATTACK);
        }
        else
        {
            out.firstRole = static_cast<uint8_t>(PickRole::ATTACK);
            out.secondRole = static_cast<uint8_t>(PickRole::DEFENSE);
        }

        out.remainingSeconds = room.pickRemainingSeconds;;
        out.blueScore = 0;
        out.redScore = 0;

        uint8_t count = 0;

        for (uint8_t slot = 0; slot < 3; ++slot)
        {
            for (const RoomMember& member : room.members)
            {
                if (member.team != receiverMember.team)
                    continue;

                if (member.slotIndex != slot)
                    continue;

                if (count >= 3)
                    break;

                FillPickTeamMemberInfo(
                    out.members[count],
                    member,
                    receiverPlayerId
                );

                ++count;
                break;
            }
        }

        out.memberCount = count;
    }

    bool SendPickState(Session* session)
    {
        if (session == nullptr)
            return false;

        uint32_t roomId = session->GetCurrentRoomId();

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            std::cout << "[PICK STATE FAIL] room not found roomId="
                << roomId
                << std::endl;
            return false;
        }

        RoomMember receiverMember{};
        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            receiverMember))
        {
            std::cout << "[PICK STATE FAIL] member not found playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            return false;
        }

        SC_PickState out{};
        FillPickStatePacket(
            out,
            room,
            receiverMember,
            session->GetPlayerId()
        );

        session->Send(&out);

        std::cout << "[PICK STATE SENT] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << " myTeam=" << static_cast<int>(out.myTeam)
            << " memberCount=" << static_cast<int>(out.memberCount)
            << std::endl;

        return true;
    }

    void BroadcastPickStateToTeam(const Room& room, RoomTeam team)
    {
        for (const RoomMember& member : room.members)
        {
            if (member.team != team)
                continue;

            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            RoomMember receiverMember{};
            if (!RoomManager::Instance().GetMember(
                room.roomId,
                member.playerId,
                receiverMember))
            {
                continue;
            }

            SC_PickState out{};
            FillPickStatePacket(
                out,
                room,
                receiverMember,
                member.playerId
            );

            target->Send(&out);
        }

        std::cout << "[PICK STATE BROADCAST TEAM] roomId="
            << room.roomId
            << " team=" << static_cast<int>(team)
            << std::endl;
    }

    void SendPickLockResult(Session* session, PickLockResult result, int characterId)
    {
        if (session == nullptr)
            return;

        SC_PickLockResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PICK_LOCK_RESULT);

        out.result = static_cast<uint8_t>(result);
        out.characterId = characterId;

        session->Send(&out);
    }

    void BroadcastPickStateToRoom(const Room& room)
    {
        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            RoomMember receiverMember{};
            if (!RoomManager::Instance().GetMember(
                room.roomId,
                member.playerId,
                receiverMember))
            {
                continue;
            }

            SC_PickState out{};
            FillPickStatePacket(
                out,
                room,
                receiverMember,
                member.playerId
            );

            target->Send(&out);
        }

        std::cout << "[PICK STATE BROADCAST ROOM] roomId="
            << room.roomId
            << std::endl;
    }

    void StartPickTimerThread(uint32_t roomId)
    {
        std::thread([roomId]()
            {
                // 1) 20초 픽
                for (int remain = PICK_TIME_SECONDS; remain >= 0; --remain)
                {
                    Room updatedRoom{};

                    bool ok = RoomManager::Instance().SetPickRemainingSeconds(
                        roomId,
                        static_cast<uint16_t>(remain),
                        updatedRoom
                    );

                    if (!ok)
                        return;

                    BroadcastPickTimer(updatedRoom, PickPhase::PICKING, static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                // 2) 자동 픽 확정
                Room finalPickRoom{};
                bool forceOk = RoomManager::Instance().ForceLockMissingPicks(roomId, finalPickRoom);

                if (!forceOk)
                    return;

                BroadcastPickStateToRoom(finalPickRoom);

                std::cout << "[PICK AUTO LOCK DONE] roomId="
                    << roomId
                    << std::endl;

                // 3) 10초 준비 시간
                for (int remain = PREPARE_TIME_SECONDS; remain >= 0; --remain)
                {
                    Room room{};
                    if (!RoomManager::Instance().GetRoom(roomId, room))
                        return;

                    BroadcastPickTimer(
                        room,
                        PickPhase::PREPARE,
                        static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                // 4) 준비 끝나면 VS/로딩 화면 정보 전송
                Room loadingRoom{};
                if (!RoomManager::Instance().GetRoom(roomId, loadingRoom))
                    return;

                BroadcastMatchLoadingInfo(loadingRoom);

                std::cout << "[MATCH LOADING WAIT CLIENT LOAD] roomId=" << roomId << std::endl;

            }).detach();
    }

    void FillMatchLoadingMemberInfo(MatchLoadingMemberInfo& out, const RoomMember& member)
    {
        out.playerId = member.playerId;

        memset(out.nickname, 0, sizeof(out.nickname));
        strncpy_s(out.nickname, sizeof(out.nickname), member.nickname.c_str(), _TRUNCATE);

        out.slotIndex = member.slotIndex;
        out.characterId = member.lockedCharacterId;
        out.rankImageId = member.rankImageId;
    }

    void BroadcastMatchLoadingInfo(const Room& room)
    {
        SC_MatchLoadingInfo out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_MATCH_LOADING_INFO);

        out.roomId = room.roomId;

        out.attackTeam = 0;   // A
        out.defenseTeam = 1;  // B

        out.attackCount = 0;
        out.defenseCount = 0;

        std::vector<RoomMember> aMembers;
        std::vector<RoomMember> bMembers;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::A)
                aMembers.push_back(member);
            else if (member.team == RoomTeam::B)
                bMembers.push_back(member);
        }

        std::sort(
            aMembers.begin(),
            aMembers.end(),
            [](const RoomMember& a, const RoomMember& b)
            {
                return a.slotIndex < b.slotIndex;
            }
        );

        std::sort(
            bMembers.begin(),
            bMembers.end(),
            [](const RoomMember& a, const RoomMember& b)
            {
                return a.slotIndex < b.slotIndex;
            }
        );

        for (size_t i = 0; i < aMembers.size() && i < MATCH_TEAM_MEMBER_COUNT; ++i)
        {
            FillMatchLoadingMemberInfo(out.attackMembers[i], aMembers[i]);
            out.attackCount++;
        }

        for (size_t i = 0; i < bMembers.size() && i < MATCH_TEAM_MEMBER_COUNT; ++i)
        {
            FillMatchLoadingMemberInfo(out.defenseMembers[i], bMembers[i]);
            out.defenseCount++;
        }

        for (const RoomMember& member : room.members)
        {
            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[MATCH LOADING INFO BROADCAST] roomId="
            << room.roomId
            << " attackCount=" << static_cast<int>(out.attackCount)
            << " defenseCount=" << static_cast<int>(out.defenseCount)
            << std::endl;
    }

    void BroadcastMatchStart(const Room& room)
    {
        SC_MatchStart out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_MATCH_START);
        out.roomId = room.roomId;

        for (const RoomMember& member : room.members)
        {
            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[MATCH START BROADCAST] roomId="
            << room.roomId
            << std::endl;
    }

    void FillGameReadyMemberInfo(GameReadyMemberInfo& out, const RoomMember& member)
    {
        out.playerId = member.playerId;

        memset(out.userId, 0, sizeof(out.userId));
        strncpy_s(
            out.userId,
            sizeof(out.userId),
            member.userId.c_str(),
            _TRUNCATE
        );

        memset(out.nickname, 0, sizeof(out.nickname));
        strncpy_s(
            out.nickname,
            sizeof(out.nickname),
            member.nickname.c_str(),
            _TRUNCATE
        );

        out.team = static_cast<uint8_t>(member.team);
        out.slotIndex = member.slotIndex;
        out.characterId = member.lockedCharacterId;
        out.rankImageId = member.rankImageId;
    }

    void ResetGameLoadRuntimeState(uint32_t roomId)
    {
        std::lock_guard<std::mutex> lock(g_gameLoadReadyMutex);

        g_gameLoadReadyPlayers.erase(roomId);
        g_gamePrepareStartedRooms.erase(roomId);

        std::cout << "[GAME LOAD RUNTIME RESET] roomId="
            << roomId
            << std::endl;
    }

    bool MarkGameLoadReadyAndCheckAll(uint32_t roomId, uint32_t playerId, Room& outRoom)
    {
        if (!RoomManager::Instance().GetRoom(roomId, outRoom))
            return false;

        bool requesterExists = false;
        int playMemberCount = 0;

        for (const RoomMember& member : outRoom.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.slotIndex >= 3)
                continue;

            playMemberCount++;

            if (member.playerId == playerId)
                requesterExists = true;
        }

        if (!requesterExists)
            return false;

        if (playMemberCount != 6)
            return false;

        std::lock_guard<std::mutex> lock(g_gameLoadReadyMutex);

        if (g_gamePrepareStartedRooms.count(roomId) > 0)
            return false;

        g_gameLoadReadyPlayers[roomId].insert(playerId);

        for (const RoomMember& member : outRoom.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.slotIndex >= 3)
                continue;

            if (g_gameLoadReadyPlayers[roomId].count(member.playerId) == 0)
                return false;
        }

        g_gamePrepareStartedRooms.insert(roomId);
        return true;
    }

    void FillHPUpdatePacket(SC_HPUpdate& out, Session* target)
    {
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_HP_UPDATE);

        out.playerId = target->GetPlayerId();

        out.hp = target->GetHP();
        out.barrier = target->GetBarrier();

        out.maxHp = target->GetMaxHP();
        out.maxBarrier = target->GetMaxBarrier();
    }

    void BroadcastInitialHPToRoom(const Room& room)
    {
        for (const RoomMember& hpOwner : room.members)
        {
            if (hpOwner.team == RoomTeam::OBSERVER)
                continue;

            Session* ownerSession = SessionManager::Instance().Find(hpOwner.playerId);
            if (ownerSession == nullptr)
                continue;

            SC_HPUpdate hpPkt{};
            FillHPUpdatePacket(hpPkt, ownerSession);

            for (const RoomMember& receiver : room.members)
            {
                if (receiver.team == RoomTeam::OBSERVER)
                    continue;

                Session* receiverSession = SessionManager::Instance().Find(receiver.playerId);
                if (receiverSession == nullptr)
                    continue;

                receiverSession->Send(&hpPkt);
            }
        }

        std::cout << "[INITIAL HP BROADCAST] roomId="
            << room.roomId
            << " memberCount=" << room.members.size()
            << std::endl;
    }

    void BroadcastScoreboardUpdate(uint32_t roomId)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        for (const RoomMember& receiver : room.members)
        {
            if (receiver.team == RoomTeam::OBSERVER)
                continue;

            Session* receiverSession = SessionManager::Instance().Find(receiver.playerId);
            if (receiverSession == nullptr)
                continue;

            SC_ScoreboardUpdate out{};

            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SCOREBOARD_UPDATE);

            out.roomId = roomId;
            out.myTeam = static_cast<uint8_t>(receiver.team);

            uint8_t count = 0;

            for (const RoomMember& member : room.members)
            {
                if (member.team == RoomTeam::OBSERVER)
                    continue;

                if (member.slotIndex >= 3)
                    continue;

                if (count >= GAME_MEMBER_COUNT)
                    break;

                Session* memberSession = SessionManager::Instance().Find(member.playerId);
                if (memberSession == nullptr)
                    continue;

                const PlayerMatchStats& stats = memberSession->GetMatchStats();

                ScoreboardMemberInfo& info = out.members[count];

                info.playerId = member.playerId;
                info.team = static_cast<uint8_t>(member.team);
                info.slotIndex = member.slotIndex;
                info.characterId = member.lockedCharacterId;

                memset(info.nickname, 0, sizeof(info.nickname));
                strncpy_s(info.nickname, sizeof(info.nickname), member.nickname.c_str(), _TRUNCATE);

                info.kills = stats.kills;
                info.deaths = stats.deaths;
                info.assists = stats.assists;
                info.credits = memberSession->GetCredits();

                count++;
            }

            out.memberCount = count;

            receiverSession->Send(&out);
        }

        std::cout << "[SCOREBOARD UPDATE BROADCAST] roomId="
            << roomId
            << " memberCount=" << room.members.size()
            << std::endl;
    }

    void BroadcastGameAllReady(const Room& room)
    {
        ClearDronesInRoom(room.roomId, 3);

        ResetMatchRuntimeState(room.roomId);
        ClearDroppedWeaponsInRoom(room.roomId);

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            Session* playerSession = SessionManager::Instance().Find(member.playerId);
            if (playerSession == nullptr)
                continue;

            playerSession->ResetMatchStats();
            playerSession->ResetCredits();
            playerSession->SetCredits(GetCreditReward("ROUND_START_HALF"));

            playerSession->ClearLastDamager();
            playerSession->ClearAssistDamageRecords();
            playerSession->InitRoundState();
            playerSession->SetState(SessionState::IN_GAME);
        }

        SC_GameAllReady out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_GAME_ALL_READY);

        out.roomId = room.roomId;
        out.prepareSeconds = PREPARE_TIME_SECONDS;

        uint8_t count = static_cast<uint8_t>(room.members.size());

        if (count > GAME_READY_MEMBER_COUNT)
            count = GAME_READY_MEMBER_COUNT;

        out.memberCount = count;

        for (uint8_t i = 0; i < count; ++i)
        {
            FillGameReadyMemberInfo(out.members[i], room.members[i]);
        }

        for (const RoomMember& member : room.members)
        {
            Session* target = SessionManager::Instance().Find(member.playerId);

            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        BroadcastInitialHPToRoom(room);
        BroadcastScoreboardUpdate(room.roomId);
    }

    bool IsBuyPhase(uint32_t roomId)
    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);

        if (it == g_matchStates.end())
            return false;

        return it->second.buyPhase &&
            !it->second.roundResolving &&
            !it->second.matchEnded;
    }

    void SetBuyPhase(uint32_t roomId, bool value)
    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);

        if (it == g_matchStates.end())
            return;

        it->second.buyPhase = value;

        std::cout << "[BUY PHASE SET] roomId="
            << roomId
            << " value=" << value
            << std::endl;
    }

    void BroadcastGamePrepareTimer(const Room& room, uint16_t remainingSeconds)
    {
        SC_GamePrepareTimer out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_GAME_PREPARE_TIMER);
        out.remainingSeconds = remainingSeconds;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[GAME PREPARE TIMER BROADCAST] roomId="
            << room.roomId
            << " remaining=" << remainingSeconds
            << std::endl;
    }

    void BroadcastPacketToRoomInternal(
        uint32_t roomId,
        void* packet)
    {
        if (roomId == 0 || packet == nullptr)
            return;

        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            std::cout
                << "[BROADCAST ROOM FAIL] room not found roomId="
                << roomId
                << std::endl;

            return;
        }

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            Session* receiver =
                SessionManager::Instance().Find(member.playerId);

            if (receiver == nullptr)
                continue;

            receiver->Send(packet);
        }
    }

    void BroadcastRoundTimer(uint32_t roomId, uint16_t remainingSeconds)
    {
        SC_RoundTimer out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROUND_TIMER);
        out.remainingSeconds = remainingSeconds;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[ROUND TIMER BROADCAST] roomId="
            << roomId
            << " remaining=" << remainingSeconds
            << std::endl;
    }

    void StartGamePrepareTimerThread(uint32_t roomId)
    {
        std::thread([roomId]()
            {
                for (int remain = GAME_PREPARE_SECONDS; remain >= 0; --remain)
                {
                    Room room{};
                    if (!RoomManager::Instance().GetRoom(roomId, room))
                        return;

                    BroadcastGamePrepareTimer(
                        room,
                        static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                std::cout << "[GAME PREPARE DONE] roomId="
                    << roomId
                    << std::endl;

                SetBuyPhase(roomId, false);

                StartRoundTimerThread(roomId);
            }
        ).detach();
    }

    void SendAmmoUpdate(
        Session* session,
        const PlayerWeaponState& weaponState)
    {
        if (session == nullptr)
            return;

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            return;

        SC_AmmoUpdate out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_AMMO_UPDATE
                );

        out.playerId =
            session->GetPlayerId();

        out.weaponId =
            weaponState.weaponId;

        out.magAmmo =
            weaponState.magAmmo;

        out.reserveAmmo =
            weaponState.reserveAmmo;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );
    }

    void BroadcastRoundResult(uint32_t roomId, RoomTeam winnerTeam, uint8_t scoreA, uint8_t scoreB, uint8_t currentRound, bool matchEnd, RoundWinReason winReason)
    {
        SC_RoundResult out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROUND_RESULT);

        out.winnerTeam = static_cast<uint8_t>(winnerTeam);
        out.scoreA = scoreA;
        out.scoreB = scoreB;
        out.currentRound = currentRound;
        out.matchEnd = matchEnd ? 1 : 0;
        out.winReason = static_cast<uint8_t>(winReason);
        out.delaySeconds = ROUND_END_DELAY_SECONDS;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[ROUND RESULT] roomId="
            << roomId
            << " winnerTeam=" << static_cast<int>(out.winnerTeam)
            << " scoreA=" << static_cast<int>(scoreA)
            << " scoreB=" << static_cast<int>(scoreB)
            << " currentRound=" << static_cast<int>(currentRound)
            << " matchEnd=" << matchEnd
            << std::endl;
    }

    int GetKdaScore(const PlayerMatchStats& stats)
    {
        return stats.kills * 100 + stats.assists * 50 - stats.deaths * 20;
    }

    void BroadcastMatchResultDetail(uint32_t roomId, RoomTeam winnerTeam, uint8_t scoreA, uint8_t scoreB)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        SC_MatchResultDetail out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_MATCH_RESULT_DETAIL);

        out.winnerTeam = static_cast<uint8_t>(winnerTeam);
        out.scoreA = scoreA;
        out.scoreB = scoreB;

        int bestScore = -999999;
        uint32_t mvpPlayerId = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.team != winnerTeam)
                continue;

            Session* s = SessionManager::Instance().Find(member.playerId);
            if (s == nullptr)
                continue;

            int score = GetKdaScore(s->GetMatchStats());

            if (score > bestScore)
            {
                bestScore = score;
                mvpPlayerId = member.playerId;
            }
        }

        uint8_t count = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (count >= GAME_MEMBER_COUNT)
                break;

            Session* s = SessionManager::Instance().Find(member.playerId);
            if (s == nullptr)
                continue;

            const PlayerMatchStats& stats = s->GetMatchStats();

            MatchResultMemberInfo& info = out.members[count];

            info.playerId = member.playerId;
            info.team = static_cast<uint8_t>(member.team);
            info.slotIndex = member.slotIndex;
            info.characterId = member.lockedCharacterId;

            memset(info.nickname, 0, sizeof(info.nickname));
            strncpy_s(info.nickname, sizeof(info.nickname), member.nickname.c_str(), _TRUNCATE);

            info.kills = stats.kills;
            info.deaths = stats.deaths;
            info.assists = stats.assists;

            info.firstHalfKills = stats.firstHalfKills;
            info.firstHalfDeaths = stats.firstHalfDeaths;
            info.firstHalfAssists = stats.firstHalfAssists;

            info.secondHalfKills = stats.secondHalfKills;
            info.secondHalfDeaths = stats.secondHalfDeaths;
            info.secondHalfAssists = stats.secondHalfAssists;

            info.overtimeKills = stats.overtimeKills;
            info.overtimeDeaths = stats.overtimeDeaths;
            info.overtimeAssists = stats.overtimeAssists;

            info.isMvp = member.playerId == mvpPlayerId ? 1 : 0;

            count++;
        }

        out.memberCount = count;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[MATCH RESULT DETAIL] roomId="
            << roomId
            << " winnerTeam=" << static_cast<int>(out.winnerTeam)
            << " scoreA=" << static_cast<int>(out.scoreA)
            << " scoreB=" << static_cast<int>(out.scoreB)
            << " memberCount=" << static_cast<int>(out.memberCount)
            << " mvpPlayerId=" << mvpPlayerId
            << std::endl;
    }

    bool IsATeamAttackRound(uint8_t currentRound)
    {
        // 1 ~ SIDE_SWAP_AFTER_ROUND 라운드: A팀 공격
        if (currentRound <= SIDE_SWAP_AFTER_ROUND)
            return true;

        // SIDE_SWAP_AFTER_ROUND + 1 ~ OVERTIME_START_ROUND - 1 라운드: A팀 수비
        if (currentRound < OVERTIME_START_ROUND)
            return false;

        // OVERTIME_START_ROUND 라운드부터 듀스, 매 라운드 공수 교대
        return ((currentRound - OVERTIME_START_ROUND) % 2) == 0;
    }

    RoundWinReason GetEliminationWinReason(RoomTeam winnerTeam, uint8_t currentRound)
    {
        bool aTeamAttack = IsATeamAttackRound(currentRound);

        RoomTeam attackTeam = aTeamAttack ? RoomTeam::A : RoomTeam::B;
        RoomTeam defenseTeam = aTeamAttack ? RoomTeam::B : RoomTeam::A;

        if (winnerTeam == attackTeam)
            return RoundWinReason::ATTACK_ELIMINATION;

        if (winnerTeam == defenseTeam)
            return RoundWinReason::DEFENSE_ELIMINATION;

        return RoundWinReason::NONE;
    }

    RoomTeam GetSpawnRoleTeam(RoomTeam originalTeam, uint8_t currentRound)
    {
        bool aTeamAttack = IsATeamAttackRound(currentRound);

        // 주의:
        // 여기서 반환하는 team은 실제 팀 변경이 아니다.
        // 클라이언트 MapInfo에서 어떤 스폰 포인트를 쓸지 알려주는 "스폰 역할 team"이다.
        // 0 = AttackSpawnPoints
        // 1 = DefendSpawnPoints

        if (originalTeam == RoomTeam::A)
        {
            return aTeamAttack ? RoomTeam::A : RoomTeam::B;
        }

        if (originalTeam == RoomTeam::B)
        {
            return aTeamAttack ? RoomTeam::B : RoomTeam::A;
        }

        return originalTeam;
    }

    void ResetRound(uint32_t roomId)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        ClearDronesInRoom(roomId, 2);
        ClearDroppedWeaponsInRoom(roomId);

        SC_RoundReset out{};

        bool isSideSwapStartRound = false;
        bool isOvertimeStartRound = false;
        bool isOvertimeRound = false;
        bool forceDefaultInventoryReset = false;

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROUND_RESET);

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            MatchRuntimeState& state = g_matchStates[roomId];

            state.currentRound++;
            state.roundResolving = false;

            if (state.currentRound == SIDE_SWAP_AFTER_ROUND + 1)
            {
                state.loseStreakA = 0;
                state.loseStreakB = 0;
                isSideSwapStartRound = true;
            }

            // 연장전 시작
            if (state.currentRound == OVERTIME_START_ROUND)
            {
                state.loseStreakA = 0;
                state.loseStreakB = 0;
                isOvertimeStartRound = true;
            }

            // 연장전은 매 라운드 공수 교대 + 6000 크레딧
            if (state.currentRound >= OVERTIME_START_ROUND)
            {
                isOvertimeRound = true;
            }

            // 공수 변경 시점, 연장전 라운드는 기본 상태로 초기화
            forceDefaultInventoryReset =
                isSideSwapStartRound ||
                isOvertimeRound;

            state.spikeCarrierPlayerId = 0;
            state.spikePlanted = false;

            state.spikeDefuserPlayerId = 0;
            state.spikeHalfDefused = false;

            // 이전 라운드에 남아 있는 스파이크 타이머를 무효화한다.
            state.spikePlantGeneration++;

            state.buyPhase = true;

            out.currentRound = state.currentRound;
            out.scoreA = state.scoreA;
            out.scoreB = state.scoreB;
        }

        uint8_t count = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.slotIndex >= 3)
                continue;

            if (count >= GAME_MEMBER_COUNT)
                break;

            Session* playerSession =
                SessionManager::Instance().Find(member.playerId);

            if (playerSession == nullptr)
                continue;

            bool wasDead = playerSession->IsDead();

            if (forceDefaultInventoryReset || wasDead)
            {
                // 죽은 플레이어, 공수 변경, 연장전은 기본 장비로 초기화
                playerSession->InitRoundState();
            }
            else
            {
                // 생존자는 들고 있던 무기 유지
                playerSession->ResetRoundStateKeepInventory();
            }

            playerSession->ClearLastDamager();
            playerSession->ClearAssistDamageRecords();

            RoundResetMemberInfo& info = out.members[count];

            info.playerId = playerSession->GetPlayerId();

            RoomTeam spawnRoleTeam = GetSpawnRoleTeam(
                member.team,
                out.currentRound
            );

            info.actualTeam = static_cast<uint8_t>(member.team);
            info.spawnTeam = static_cast<uint8_t>(spawnRoleTeam);

            info.slotIndex = member.slotIndex;

            info.hp = playerSession->GetHP();
            info.barrier = playerSession->GetBarrier();
            info.maxHp = playerSession->GetMaxHP();
            info.maxBarrier = playerSession->GetMaxBarrier();

            info.currentWeaponId = playerSession->GetCurrentWeaponId();

            PlayerWeaponState* weaponState =
                playerSession->GetWeaponState(info.currentWeaponId);

            if (weaponState != nullptr)
            {
                info.magAmmo = weaponState->magAmmo;
                info.reserveAmmo = weaponState->reserveAmmo;
            }
            else
            {
                info.magAmmo = 0;
                info.reserveAmmo = 0;
            }
            FillRoundResetWeaponSlots(info, playerSession);
            count++;
        }

        out.memberCount = count;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[ROUND RESET] roomId="
            << roomId
            << " round=" << static_cast<int>(out.currentRound)
            << " scoreA=" << static_cast<int>(out.scoreA)
            << " scoreB=" << static_cast<int>(out.scoreB)
            << " memberCount=" << static_cast<int>(out.memberCount)
            << std::endl;

        if (isSideSwapStartRound && !isOvertimeRound)
        {
            int halfStartCredit = GetCreditReward("ROUND_START_HALF");

            SetAllPlayerCreditsInRoom(roomId, halfStartCredit);
            BroadcastScoreboardUpdate(roomId);

            std::cout << "[CREDIT HALF START] roomId="
                << roomId
                << " credit=" << halfStartCredit
                << std::endl;
        }

        if (isOvertimeRound)
        {
            SetAllPlayerCreditsInRoom(roomId, OVERTIME_ROUND_CREDIT);
            BroadcastScoreboardUpdate(roomId);

            std::cout << "[CREDIT OVERTIME ROUND] roomId="
                << roomId
                << " credit=" << OVERTIME_ROUND_CREDIT
                << std::endl;
        }

        StartGamePrepareTimerThread(roomId);
    }

    bool FindOwnedWeaponByPredicate(
        Session* session,
        bool (*predicate)(int),
        PlayerWeaponState& outWeapon)
    {
        if (session == nullptr)
            return false;

        std::vector<PlayerWeaponState> ownedWeapons =
            session->GetOwnedWeapons();

        for (const PlayerWeaponState& weapon : ownedWeapons)
        {
            if (!weapon.owned)
                continue;

            if (predicate == nullptr)
                continue;

            if (!predicate(weapon.weaponId))
                continue;

            outWeapon = weapon;
            return true;
        }

        return false;
    }

    void FillRoundResetWeaponSlots(
        RoundResetMemberInfo& info,
        Session* session)
    {
        info.primaryWeaponId = -1;
        info.primaryMagAmmo = 0;
        info.primaryReserveAmmo = 0;

        info.pistolWeaponId = -1;
        info.pistolMagAmmo = 0;
        info.pistolReserveAmmo = 0;

        info.knifeWeaponId = -1;
        info.knifeMagAmmo = 0;
        info.knifeReserveAmmo = 0;

        if (session == nullptr)
            return;

        PlayerWeaponState weapon{};

        if (FindOwnedWeaponByPredicate(session, IsPrimaryWeapon, weapon))
        {
            info.primaryWeaponId = weapon.weaponId;
            info.primaryMagAmmo = weapon.magAmmo;
            info.primaryReserveAmmo = weapon.reserveAmmo;
        }

        if (FindOwnedWeaponByPredicate(session, IsPistolWeapon, weapon))
        {
            info.pistolWeaponId = weapon.weaponId;
            info.pistolMagAmmo = weapon.magAmmo;
            info.pistolReserveAmmo = weapon.reserveAmmo;
        }

        if (FindOwnedWeaponByPredicate(session, IsKnifeWeapon, weapon))
        {
            info.knifeWeaponId = weapon.weaponId;
            info.knifeMagAmmo = weapon.magAmmo;
            info.knifeReserveAmmo = weapon.reserveAmmo;
        }
    }

    void StartRoundEndThread(uint32_t roomId, RoomTeam winnerTeam, bool matchEnd)
    {
        std::thread([roomId, winnerTeam, matchEnd]()
            {
                std::this_thread::sleep_for(
                    std::chrono::seconds(ROUND_END_DELAY_SECONDS)
                );

                uint8_t scoreA = 0;
                uint8_t scoreB = 0;

                {
                    std::lock_guard<std::mutex> lock(g_matchStateMutex);

                    auto it = g_matchStates.find(roomId);
                    if (it == g_matchStates.end())
                        return;

                    scoreA = it->second.scoreA;
                    scoreB = it->second.scoreB;
                }

                if (matchEnd)
                {
                    BroadcastMatchResultDetail(
                        roomId,
                        winnerTeam,
                        scoreA,
                        scoreB
                    );

                    return;
                }

                ResetRound(roomId);
            }
        ).detach();
    }

    bool IsTeamEliminated(const Room& room, RoomTeam team)
    {
        int teamPlayerCount = 0;
        int aliveCount = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team != team)
                continue;

            if (member.slotIndex >= 3)
                continue;

            teamPlayerCount++;

            Session* playerSession =
                SessionManager::Instance().Find(member.playerId);

            if (playerSession == nullptr)
                continue;

            if (!playerSession->IsDead())
                aliveCount++;
        }

        return teamPlayerCount > 0 && aliveCount == 0;
    }

    bool ShouldEndMatch(uint8_t scoreA, uint8_t scoreB, bool isOvertime)
    {
        uint8_t highScore = scoreA > scoreB ? scoreA : scoreB;
        uint8_t lowScore = scoreA > scoreB ? scoreB : scoreA;

        if (!isOvertime)
        {
            // 정규전에서는 3:0 또는 3:1이면 종료
            return highScore >= MATCH_WIN_ROUND_COUNT;
        }

        // 듀스에서는 2점 차가 나야 종료
        return highScore >= MATCH_WIN_ROUND_COUNT &&
            (highScore - lowScore) >= 2;
    }

    void TryResolveRoundByElimination(uint32_t roomId)
    {
        bool spikePlanted = false;
        uint8_t ruleRound = 1;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                return;

            if (it->second.roundResolving || it->second.matchEnded)
                return;

            spikePlanted = it->second.spikePlanted;
            ruleRound = it->second.currentRound;
        }

        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        bool teamADead = IsTeamEliminated(room, RoomTeam::A);
        bool teamBDead = IsTeamEliminated(room, RoomTeam::B);

        if (!teamADead && !teamBDead)
            return;

        RoomTeam attackTeam = GetAttackTeam(ruleRound);
        RoomTeam defenseTeam = GetDefenseTeam(ruleRound);

        RoomTeam winnerTeam = RoomTeam::A;

        if (spikePlanted)
        {
            bool attackDead =
                attackTeam == RoomTeam::A ? teamADead : teamBDead;

            bool defenseDead =
                defenseTeam == RoomTeam::A ? teamADead : teamBDead;

            if (defenseDead)
            {
                // 스파이크 설치 후 수비팀 전멸 = 공격팀 승리
                winnerTeam = attackTeam;
            }
            else if (attackDead)
            {
                // 스파이크 설치 후 공격팀 전멸 = 아직 라운드 종료 아님
                // 수비팀은 반드시 해체해야 승리
                std::cout << "[ELIMINATION BLOCK] attack team dead but spike planted. need defuse. roomId="
                    << roomId
                    << std::endl;
                return;
            }
            else
            {
                return;
            }
        }
        else
        {
            if (teamADead && teamBDead)
            {
                std::cout << "[ROUND RESULT BLOCK] both teams dead roomId="
                    << roomId
                    << std::endl;
                return;
            }

            winnerTeam = teamADead ? RoomTeam::B : RoomTeam::A;
        }

        uint8_t scoreA = 0;
        uint8_t scoreB = 0;
        uint8_t currentRound = 1;
        bool matchEnd = false;
        bool isOvertime = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            MatchRuntimeState& state = g_matchStates[roomId];

            if (state.roundResolving || state.matchEnded)
                return;

            state.roundResolving = true;

            if (winnerTeam == RoomTeam::A)
                state.scoreA++;
            else
                state.scoreB++;

            if (IsOvertimeScore(state.scoreA, state.scoreB))
            {
                state.isOvertime = true;
                std::cout << "[OVERTIME START] roomId=" << roomId << std::endl;
            }

            currentRound = state.currentRound;
            scoreA = state.scoreA;
            scoreB = state.scoreB;
            isOvertime = state.isOvertime;

            matchEnd = ShouldEndMatch(scoreA, scoreB, isOvertime);

            if (matchEnd)
                state.matchEnded = true;
        }

        RoundWinReason winReason =
            GetEliminationWinReason(winnerTeam, currentRound);

        BroadcastRoundResult(
            roomId,
            winnerTeam,
            scoreA,
            scoreB,
            currentRound,
            matchEnd,
            winReason
        );

        ApplyRoundEndCredits(roomId, winnerTeam, winReason);

        BroadcastScoreboardUpdate(roomId);

        StartRoundEndThread(
            roomId,
            winnerTeam,
            matchEnd
        );
    }

    bool AreAllPlayersReturnedFromResult(uint32_t roomId, const Room& room)
    {
        std::lock_guard<std::mutex> lock(g_resultLeaveMutex);

        auto it = g_resultLeavePlayers.find(roomId);

        if (it == g_resultLeavePlayers.end())
            return true;

        int playMemberCount = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.slotIndex >= 3)
                continue;

            playMemberCount++;

            if (it->second.count(member.playerId) == 0)
            {
                std::cout << "[RESULT RETURN CHECK] not returned. "
                    << "roomId=" << roomId
                    << " playerId=" << member.playerId
                    << std::endl;

                return false;
            }
        }

        return playMemberCount == 6;
    }

    RoomTeam GetDefenseTeam(uint8_t currentRound)
    {
        bool aTeamAttack = IsATeamAttackRound(currentRound);

        return aTeamAttack ? RoomTeam::B : RoomTeam::A;
    }

    RoomTeam GetAttackTeam(uint8_t currentRound)
    {
        bool aTeamAttack = IsATeamAttackRound(currentRound);
        return aTeamAttack ? RoomTeam::A : RoomTeam::B;
    }

    void BroadcastSpikePickupNotify(uint32_t roomId, uint32_t carrierPlayerId)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        RoomMember carrierMember{};
        if (!RoomManager::Instance().GetMember(roomId, carrierPlayerId, carrierMember))
            return;

        for (const RoomMember& receiver : room.members)
        {
            if (receiver.team == RoomTeam::OBSERVER)
                continue;

            Session* target = SessionManager::Instance().Find(receiver.playerId);
            if (target == nullptr)
                continue;

            SC_SpikePickupNotify out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_PICKUP_NOTIFY);

            out.carrierPlayerId = carrierPlayerId;

            // 같은 공격팀에게만 누가 먹었는지 공개
            out.showCarrierInfo = receiver.team == carrierMember.team ? 1 : 0;

            memset(out.carrierNickname, 0, sizeof(out.carrierNickname));

            if (out.showCarrierInfo != 0)
            {
                strncpy_s(
                    out.carrierNickname,
                    sizeof(out.carrierNickname),
                    carrierMember.nickname.c_str(),
                    _TRUNCATE
                );
            }

            target->Send(&out);
        }

        std::cout << "[SPIKE PICKUP NOTIFY] roomId="
            << roomId
            << " carrier=" << carrierPlayerId
            << std::endl;
    }

    void BroadcastSpikePlanted(uint32_t roomId, float x, float y, float z)
    {
        SC_SpikePlanted out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_PLANTED);

        out.x = x;
        out.y = y;
        out.z = z;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SPIKE PLANTED] roomId="
            << roomId
            << " pos=(" << x << ", " << y << ", " << z << ")"
            << std::endl;
    }

    void ResolveRoundBySpikeExplode(uint32_t roomId, uint64_t spikeGeneration)
    {
        RoomTeam winnerTeam = RoomTeam::A;

        uint8_t scoreA = 0;
        uint8_t scoreB = 0;
        uint8_t currentRound = 1;
        bool matchEnd = false;
        bool isOvertime = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                return;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                return;

            if (!state.spikePlanted)
                return;

            // 이 스레드가 시작될 때의 설치와 현재 설치가 다르면
            // 이전 라운드에서 남아 있던 타이머이므로 무시한다.
            if (state.spikePlantGeneration != spikeGeneration)
            {
                std::cout << "[SPIKE TIMER STALE] roomId="
                    << roomId
                    << " timerGeneration=" << spikeGeneration
                    << " currentGeneration=" << state.spikePlantGeneration
                    << std::endl;
                return;
            }

            state.roundResolving = true;
            state.spikePlanted = false;

            // 같은 설치에 대해 남아 있을 수 있는 다른 타이머도 무효화한다.
            state.spikePlantGeneration++;

            winnerTeam = GetAttackTeam(state.currentRound);

            if (winnerTeam == RoomTeam::A)
                state.scoreA++;
            else
                state.scoreB++;

            if (IsOvertimeScore(state.scoreA, state.scoreB))
                state.isOvertime = true;

            currentRound = state.currentRound;
            scoreA = state.scoreA;
            scoreB = state.scoreB;
            isOvertime = state.isOvertime;

            matchEnd = ShouldEndMatch(scoreA, scoreB, isOvertime);

            if (matchEnd)
                state.matchEnded = true;
        }

        BroadcastRoundResult(
            roomId,
            winnerTeam,
            scoreA,
            scoreB,
            currentRound,
            matchEnd,
            RoundWinReason::ATTACK_SPIKE_EXPLODE
        );

        ApplyRoundEndCredits(
            roomId,
            winnerTeam,
            RoundWinReason::ATTACK_SPIKE_EXPLODE
        );

        BroadcastScoreboardUpdate(roomId);

        StartRoundEndThread(
            roomId,
            winnerTeam,
            matchEnd
        );

        std::cout << "[SPIKE EXPLODE ROUND WIN] roomId="
            << roomId
            << " winnerTeam=" << static_cast<int>(winnerTeam)
            << std::endl;
    }

    void StartSpikeTimerThread(uint32_t roomId, uint64_t spikeGeneration)
    {
        std::thread([roomId, spikeGeneration]()
            {
                std::this_thread::sleep_for(
                    std::chrono::seconds(SPIKE_EXPLODE_SECONDS)
                );

                ResolveRoundBySpikeExplode(
                    roomId,
                    spikeGeneration
                );
            }
        ).detach();

        std::cout << "[SPIKE TIMER START] roomId="
            << roomId
            << " generation=" << spikeGeneration
            << " seconds=" << SPIKE_EXPLODE_SECONDS
            << std::endl;
    }

    void ResolveRoundByTimeOut(uint32_t roomId)
    {
        RoomTeam winnerTeam = RoomTeam::B;

        uint8_t scoreA = 0;
        uint8_t scoreB = 0;
        uint8_t currentRound = 1;
        bool matchEnd = false;
        bool isOvertime = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                return;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                return;

            state.roundResolving = true;

            winnerTeam = GetDefenseTeam(state.currentRound);

            if (winnerTeam == RoomTeam::A)
                state.scoreA++;
            else
                state.scoreB++;

            if (IsOvertimeScore(state.scoreA, state.scoreB))
                state.isOvertime = true;

            currentRound = state.currentRound;
            scoreA = state.scoreA;
            scoreB = state.scoreB;
            isOvertime = state.isOvertime;

            matchEnd = ShouldEndMatch(scoreA, scoreB, isOvertime);

            if (matchEnd)
                state.matchEnded = true;
        }

        BroadcastRoundResult(
            roomId,
            winnerTeam,
            scoreA,
            scoreB,
            currentRound,
            matchEnd,
            RoundWinReason::DEFENSE_TIME_OUT
        );

        ApplyRoundEndCredits(
            roomId,
            winnerTeam,
            RoundWinReason::DEFENSE_TIME_OUT
        );

        BroadcastScoreboardUpdate(roomId);

        StartRoundEndThread(
            roomId,
            winnerTeam,
            matchEnd
        );

        std::cout << "[ROUND TIME OUT] roomId="
            << roomId
            << " winnerTeam=" << static_cast<int>(winnerTeam)
            << " scoreA=" << static_cast<int>(scoreA)
            << " scoreB=" << static_cast<int>(scoreB)
            << " currentRound=" << static_cast<int>(currentRound)
            << std::endl;
    }

    void StartRoundTimerThread(uint32_t roomId)
    {
        std::thread([roomId]()
            {
                for (int remain = ROUND_PLAY_SECONDS; remain >= 0; --remain)
                {
                    {
                        std::lock_guard<std::mutex> lock(g_matchStateMutex);

                        auto it = g_matchStates.find(roomId);
                        if (it == g_matchStates.end())
                            return;

                        if (it->second.roundResolving || it->second.matchEnded || it->second.spikePlanted)
                            return;
                    }

                    BroadcastRoundTimer(
                        roomId,
                        static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                ResolveRoundByTimeOut(roomId);
            }
        ).detach();

        std::cout << "[ROUND TIMER START] roomId="
            << roomId
            << " seconds=" << ROUND_PLAY_SECONDS
            << std::endl;
    }

    void BroadcastSpikeDropped(uint32_t roomId, float x, float y, float z, float dirX, float dirY, float dirZ, uint32_t dropperPlayerId)
    {
        SC_SpikeDropped out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_DROPPED);

        out.x = x;
        out.y = y;
        out.z = z;

        out.dirX = dirX;
        out.dirY = dirY;
        out.dirZ = dirZ;

        out.dropperPlayerId = dropperPlayerId;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void BroadcastSpikeDefused(uint32_t roomId, uint32_t defuserPlayerId)
    {
        SC_SpikeDefused out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_DEFUSED);
        out.defuserPlayerId = defuserPlayerId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SPIKE DEFUSED] roomId="
            << roomId
            << " defuser=" << defuserPlayerId
            << std::endl;
    }

    void ResolveRoundBySpikeDefuse(uint32_t roomId, uint32_t defuserPlayerId)
    {
        RoomTeam winnerTeam = RoomTeam::B;

        uint8_t scoreA = 0;
        uint8_t scoreB = 0;
        uint8_t currentRound = 1;
        bool matchEnd = false;
        bool isOvertime = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                return;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                return;

            if (!state.spikePlanted)
                return;

            state.roundResolving = true;
            state.spikePlanted = false;
            state.spikeCarrierPlayerId = 0;

            // 설치 당시 시작된 폭발 타이머를 무효화한다.
            state.spikePlantGeneration++;

            state.spikeDefuserPlayerId = 0;
            state.spikeHalfDefused = false;

            winnerTeam = GetDefenseTeam(state.currentRound);

            if (winnerTeam == RoomTeam::A)
                state.scoreA++;
            else
                state.scoreB++;

            if (IsOvertimeScore(state.scoreA, state.scoreB))
                state.isOvertime = true;

            currentRound = state.currentRound;
            scoreA = state.scoreA;
            scoreB = state.scoreB;
            isOvertime = state.isOvertime;

            matchEnd = ShouldEndMatch(scoreA, scoreB, isOvertime);

            if (matchEnd)
                state.matchEnded = true;
        }

        BroadcastSpikeDefused(roomId, defuserPlayerId);

        BroadcastRoundResult(
            roomId,
            winnerTeam,
            scoreA,
            scoreB,
            currentRound,
            matchEnd,
            RoundWinReason::DEFENSE_SPIKE_DEFUSE
        );

        StartRoundEndThread(roomId, winnerTeam, matchEnd);
    }

    void BroadcastSpikeDefuseState(uint32_t roomId, uint8_t state, bool halfDefused, uint32_t defuserPlayerId)
    {
        SC_SpikeDefuseState out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_DEFUSE_STATE);

        out.state = state;
        out.halfDefused = halfDefused ? 1 : 0;
        out.defuserPlayerId = defuserPlayerId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SPIKE DEFUSE STATE] roomId="
            << roomId
            << " state=" << static_cast<int>(state)
            << " half=" << static_cast<int>(out.halfDefused)
            << " defuser=" << defuserPlayerId
            << std::endl;
    }

    void SetAllPlayerCreditsInRoom(uint32_t roomId, int credit)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            Session* s = SessionManager::Instance().Find(member.playerId);

            if (s == nullptr)
                continue;

            s->SetCredits(credit);
        }
        std::cout << "[CREDITS SET] roomId="
            << roomId
            << " credits=" << credit
            << std::endl;
    }

    std::string GetLoseStreakRewardKey(uint8_t loseStreak)
    {
        if (loseStreak <= 1)
            return "LOSE_STREAK_1";

        if (loseStreak == 2)
            return "LOSE_STREAK_2";

        return "LOSE_STREAK_3_PLUS";
    }

    void ApplyRoundEndCredits(uint32_t roomId, RoomTeam winnerTeam, RoundWinReason winReason)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        RoomTeam loserTeam = winnerTeam == RoomTeam::A ? RoomTeam::B : RoomTeam::A;

        uint8_t loserStreak = 1;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            MatchRuntimeState& state = g_matchStates[roomId];

            if (winnerTeam == RoomTeam::A)
            {
                state.loseStreakA = 0;

                if (state.loseStreakB < 3)
                    state.loseStreakB++;

                loserStreak = state.loseStreakB;
            }
            else
            {
                state.loseStreakB = 0;

                if (state.loseStreakA < 3)
                    state.loseStreakA++;

                loserStreak = state.loseStreakA;
            }
        }

        int winCredit = GetCreditReward("ROUND_WIN");
        int loseCredit = GetCreditReward(GetLoseStreakRewardKey(loserStreak));
        int surviveDefeatCredit = GetCreditReward("SURVIVE_DEFEAT");

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            Session* s = SessionManager::Instance().Find(member.playerId);

            if (s == nullptr)
                continue;

            if (member.team == winnerTeam)
            {
                s->AddCredits(winCredit);

                std::cout << "[CREDIT ROUND WIN] playerId="
                    << s->GetPlayerId()
                    << " add=" << winCredit
                    << " total=" << s->GetCredits()
                    << std::endl;

                continue;
            }

            bool isSurviveDefeat =
                member.team == loserTeam &&
                winReason == RoundWinReason::DEFENSE_TIME_OUT &&
                !s->IsDead();

            if (isSurviveDefeat)
            {
                s->AddCredits(surviveDefeatCredit);

                std::cout << "[CREDIT SURVIVE DEFEAT] playerId="
                    << s->GetPlayerId()
                    << " add=" << surviveDefeatCredit
                    << " total=" << s->GetCredits()
                    << std::endl;
            }
            else
            {
                s->AddCredits(loseCredit);

                std::cout << "[CREDIT ROUND LOSE] playerId="
                    << s->GetPlayerId()
                    << " streak=" << static_cast<int>(loserStreak)
                    << " add=" << loseCredit
                    << " total=" << s->GetCredits()
                    << std::endl;
            }
        }
    }

    struct DroppedWeaponState
    {
        uint32_t dropId = 0;
        uint32_t roomId = 0;

        int weaponId = 0;
        int magAmmo = 0;
        int reserveAmmo = 0;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        float dirX = 0.f;
        float dirY = 0.f;
        float dirZ = 0.f;
    };

    std::mutex g_droppedWeaponMutex;
    std::unordered_map<uint32_t, DroppedWeaponState> g_droppedWeapons;
    uint32_t g_nextDroppedWeaponId = 1;

    void ClearDroppedWeaponsInRoom(uint32_t roomId)
    {
        std::lock_guard<std::mutex> lock(g_droppedWeaponMutex);

        int removedCount = 0;

        for (auto it = g_droppedWeapons.begin(); it != g_droppedWeapons.end();)
        {
            if (it->second.roomId == roomId)
            {
                it = g_droppedWeapons.erase(it);
                removedCount++;
            }
            else
            {
                ++it;
            }
        }

        std::cout << "[DROPPED WEAPONS CLEAR] roomId="
            << roomId
            << " removedCount=" << removedCount
            << std::endl;
    }

    bool IsKnifeWeapon(int weaponId)
    {
        const ServerWeaponData* data = ServerWeaponTable::Find(weaponId);

        if (data == nullptr)
            return false;

        return data->weaponType == ServerWeaponType::Knife;
    }

    bool IsPistolWeapon(int weaponId)
    {
        const ServerWeaponData* data = ServerWeaponTable::Find(weaponId);

        if (data == nullptr)
            return false;

        return data->weaponType == ServerWeaponType::Pistol;
    }

    bool IsPrimaryWeapon(int weaponId)
    {
        const ServerWeaponData* data =
            ServerWeaponTable::Find(weaponId);

        if (data == nullptr)
            return false;

        return
            data->weaponType == ServerWeaponType::AR ||
            data->weaponType == ServerWeaponType::SMG ||
            data->weaponType == ServerWeaponType::LMG ||
            data->weaponType == ServerWeaponType::SR ||
            data->weaponType == ServerWeaponType::Shotgun ||
            data->weaponType == ServerWeaponType::DMR;
    }

    int FindReplaceWeaponIdForPickup(Session* session, int pickedWeaponId)
    {
        if (session == nullptr)
            return -1;

        std::vector<PlayerWeaponState> ownedWeapons =
            session->GetOwnedWeapons();

        if (IsKnifeWeapon(pickedWeaponId))
        {
            for (const PlayerWeaponState& weapon : ownedWeapons)
            {
                if (IsKnifeWeapon(weapon.weaponId))
                    return weapon.weaponId;
            }

            return -1;
        }

        if (IsPistolWeapon(pickedWeaponId))
        {
            for (const PlayerWeaponState& weapon : ownedWeapons)
            {
                if (IsPistolWeapon(weapon.weaponId))
                    return weapon.weaponId;
            }

            return -1;
        }

        if (IsPrimaryWeapon(pickedWeaponId))
        {
            for (const PlayerWeaponState& weapon : ownedWeapons)
            {
                if (IsPrimaryWeapon(weapon.weaponId))
                    return weapon.weaponId;
            }

            return -1;
        }

        return -1;
    }

    uint32_t CreateDroppedWeapon(uint32_t roomId, uint32_t dropperPlayerId, int weaponId, int magAmmo, int reserveAmmo, float x, float y, float z, float dirX, float dirY, float dirZ)
    {
        uint32_t dropId = 0;

        {
            std::lock_guard<std::mutex> lock(g_droppedWeaponMutex);

            dropId = g_nextDroppedWeaponId++;

            DroppedWeaponState state{};
            state.dropId = dropId;
            state.roomId = roomId;
            state.weaponId = weaponId;
            state.magAmmo = magAmmo;
            state.reserveAmmo = reserveAmmo;
            state.x = x;
            state.y = y;
            state.z = z;
            state.dirX = dirX;
            state.dirY = dirY;
            state.dirZ = dirZ;

            g_droppedWeapons[dropId] = state;
        }

        SC_WeaponDropped out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_WEAPON_DROPPED);

        out.dropId = dropId;
        out.dropperPlayerId = dropperPlayerId;
        out.weaponId = weaponId;
        out.magAmmo = magAmmo;
        out.reserveAmmo = reserveAmmo;

        out.x = x;
        out.y = y;
        out.z = z;

        out.dirX = dirX;
        out.dirY = dirY;
        out.dirZ = dirZ;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[WEAPON DROPPED] roomId="
            << roomId
            << " dropId=" << dropId
            << " weaponId=" << weaponId
            << " ammo=" << magAmmo << "/" << reserveAmmo
            << std::endl;

        return dropId;
    }

    void BroadcastWeaponPickupNotify(uint32_t roomId, uint32_t dropId, uint32_t pickerPlayerId, int weaponId, int magAmmo, int reserveAmmo)
    {
        SC_WeaponPickupNotify out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_WEAPON_PICKUP_NOTIFY
            );

        out.dropId = dropId;
        out.pickerPlayerId = pickerPlayerId;
        out.weaponId = weaponId;
        out.magAmmo = magAmmo;
        out.reserveAmmo = reserveAmmo;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void BroadcastWeaponSwitchNotify(uint32_t roomId, uint32_t playerId, int weaponId)
    {
        SC_WeaponSwitchNotify out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_WEAPON_SWITCH_NOTIFY
            );

        out.playerId = playerId;
        out.weaponId = weaponId;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void BroadcastReloadVisualInternal(uint32_t roomId, uint32_t playerId, int weaponId, bool isReloading)
    {
        SC_ReloadVisual out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_RELOAD_VISUAL
            );

        out.playerId = playerId;
        out.weaponId = weaponId;
        out.isReloading = isReloading ? 1 : 0;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void SendStoreBuyResult(
        Session* session,
        StoreBuyResult result,
        StoreItemType itemType,
        int itemId,
        int price,
        int weaponId,
        int magAmmo,
        int reserveAmmo)
    {
        if (session == nullptr)
            return;

        SC_StoreBuyResult out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_STORE_BUY_RESULT
            );

        out.result = static_cast<uint8_t>(result);
        out.itemType = static_cast<uint8_t>(itemType);

        out.itemId = itemId;
        out.price = price;
        out.remainCredits = session->GetCredits();

        out.weaponId = weaponId;
        out.magAmmo = magAmmo;
        out.reserveAmmo = reserveAmmo;

        out.barrier = session->GetBarrier();
        out.maxBarrier = session->GetMaxBarrier();

        session->Send(&out);
    }

    constexpr int DRONE_MAX_HP = 100;

    struct ServerDroneState
    {
        uint32_t droneId = 0;
        uint32_t ownerPlayerId = 0;
        uint32_t roomId = 0;

        int hp = DRONE_MAX_HP;

        bool taserUsed = false;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        float yaw = 0.f;
        float pitch = 0.f;
    };

    std::mutex g_droneMutex;
    std::unordered_map<uint32_t, ServerDroneState> g_drones;
    std::unordered_map<uint32_t, uint32_t> g_ownerDroneIds;
    uint32_t g_nextDroneId = 1;

    bool IsDroneActiveForPlayer(uint32_t playerId)
    {
        std::lock_guard<std::mutex> lock(g_droneMutex);
        return g_ownerDroneIds.find(playerId) != g_ownerDroneIds.end();
    }

    void BroadcastDroneDestroy(uint32_t roomId, uint32_t droneId, uint32_t ownerPlayerId, uint8_t reason)
    {
        if (roomId == 0 || droneId == 0 || ownerPlayerId == 0)
            return;

        SC_DroneDestroy out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_DRONE_DESTROY);

        out.droneId = droneId;
        out.ownerPlayerId = ownerPlayerId;
        out.reason = reason;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void BroadcastDroneTaserFire(
        uint32_t roomId,
        uint32_t droneId,
        uint32_t ownerPlayerId,
        uint32_t targetPlayerId,
        bool hit,
        uint8_t stack,
        float duration,
        int32_t damage)
    {
        SC_DroneTaserFire out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_DRONE_TASER_FIRE
            );

        out.droneId = droneId;
        out.ownerPlayerId = ownerPlayerId;
        out.targetPlayerId = targetPlayerId;

        out.hit = hit ? 1 : 0;
        out.stack = stack;

        out.duration = duration;
        out.damage = damage;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void DestroyDroneForPlayer(uint32_t ownerPlayerId, uint8_t reason)
    {
        uint32_t roomId = 0;
        uint32_t droneId = 0;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto ownerIt = g_ownerDroneIds.find(ownerPlayerId);

            if (ownerIt == g_ownerDroneIds.end())
                return;

            droneId = ownerIt->second;

            auto droneIt = g_drones.find(droneId);

            if (droneIt == g_drones.end())
            {
                g_ownerDroneIds.erase(ownerIt);
                return;
            }

            roomId = droneIt->second.roomId;

            g_drones.erase(droneIt);
            g_ownerDroneIds.erase(ownerIt);
        }

        BroadcastDroneDestroy(roomId, droneId, ownerPlayerId, reason);

        std::cout << "[DRONE DESTROY] owner="
            << ownerPlayerId
            << " droneId=" << droneId
            << " roomId=" << roomId
            << " reason=" << static_cast<int>(reason)
            << std::endl;
    }

    void DestroyDroneById(uint32_t droneId, uint8_t reason)
    {
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto droneIt = g_drones.find(droneId);
            if (droneIt == g_drones.end())
                return;

            roomId = droneIt->second.roomId;
            ownerPlayerId = droneIt->second.ownerPlayerId;

            g_drones.erase(droneIt);
            g_ownerDroneIds.erase(ownerPlayerId);
        }

        BroadcastDroneDestroy(
            roomId,
            droneId,
            ownerPlayerId,
            reason
        );

        std::cout
            << "[DRONE DESTROY BY ID] droneId="
            << droneId
            << " owner="
            << ownerPlayerId
            << " reason="
            << static_cast<int>(reason)
            << std::endl;
    }

    void ClearDronesInRoom(uint32_t roomId, uint8_t reason)
    {
        std::vector<ServerDroneState> removedDrones;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            for (auto it = g_drones.begin(); it != g_drones.end();)
            {
                if (it->second.roomId != roomId)
                {
                    ++it;
                    continue;
                }

                ServerDroneState removed = it->second;
                removedDrones.push_back(removed);

                g_ownerDroneIds.erase(removed.ownerPlayerId);
                it = g_drones.erase(it);
            }
        }

        for (const ServerDroneState& drone : removedDrones)
        {
            BroadcastDroneDestroy(
                roomId,
                drone.droneId,
                drone.ownerPlayerId,
                reason
            );
        }

        if (!removedDrones.empty())
        {
            std::cout << "[DRONE CLEAR ROOM] roomId="
                << roomId
                << " count=" << removedDrones.size()
                << " reason=" << static_cast<int>(reason)
                << std::endl;
        }
    }

    bool IsDroneBlockedAction(Session* session, const char* actionName)
    {
        if (session == nullptr)
            return true;

        if (!IsDroneActiveForPlayer(session->GetPlayerId()))
            return false;

        std::cout << "[DRONE ACTION BLOCK] action="
            << actionName
            << " playerId="
            << session->GetPlayerId()
            << std::endl;

        return true;
    }

}

void PacketDispatcher::Dispatch(Session* session, PacketHeader* header)
{
    PacketType type = (PacketType)header->type;

    std::cout << "[DISPATCH BEGIN] session=" << session
        << " playerId=" << session->GetPlayerId()
        << " userId=" << session->GetUserId()
        << " type=" << (uint16_t)type
        << " size=" << header->size
        << std::endl;

    switch (type)
    {
    case PacketType::PKT_CS_REGISTER:
    case PacketType::PKT_CS_LOGIN:
        break;

    default:
    {
        if (!session->IsAuthenticated())
        {
            std::cout << "[AUTH BLOCK] session=" << session
                << " playerId=" << session->GetPlayerId()
                << " type=" << (uint16_t)type
                << " userId=" << session->GetUserId()
                << std::endl;
            return;
        }
        break;
    }
    }

    switch (type)
    {
    case PacketType::PKT_CS_REGISTER:
    {
        auto* pkt = reinterpret_cast<CS_Register*>(header);

        std::cout << "[REGISTER PACKET] id=" << pkt->id
            << " nick=" << pkt->nickname
            << std::endl;

        uint8_t result = (uint8_t)UserManager::Instance().Register(
            pkt->id,
            pkt->nickname,
            pkt->password,
            pkt->confirmPassword
        );

        SC_RegisterResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_REGISTER_RESULT;
        out.result = result;

        session->Send(&out);

        std::cout << "[REGISTER RESULT SENT] result=" << (int)result << std::endl;
        break;
    }

    case PacketType::PKT_CS_LOGIN:
    {
        auto* pkt = reinterpret_cast<CS_Login*>(header);

        std::cout << "[LOGIN PACKET] id=" << pkt->id
            << " session=" << session
            << " alreadyAuth=" << session->IsAuthenticated()
            << std::endl;

        if (session->IsAuthenticated())
        {
            SC_LoginResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_LOGIN_RESULT;
            out.result = (uint8_t)LoginResult::SUCCESS;
            session->Send(&out);
            break;
        }

        uint8_t result = (uint8_t)UserManager::Instance().Login(
            pkt->id,
            pkt->password
        );

        if (result == (uint8_t)LoginResult::SUCCESS)
        {
            bool loginOk = UserManager::Instance().OnLogin(pkt->id, session);

            if (!loginOk)
            {
                result = (uint8_t)LoginResult::ALREADY_LOGIN;

                std::cout << "[LOGIN FAIL] already logged in id=" << pkt->id
                    << " session=" << session
                    << std::endl;
            }
            else
            {
                session->SetAuthenticated(true);
                session->SetState(SessionState::AUTHENTICATED);

                uint32_t newPlayerId = SessionManager::Instance().AllocatePlayerId();
                session->SetPlayerId(newPlayerId);
                SessionManager::Instance().Add(session);

                std::cout << "[LOGIN SUCCESS READY] id=" << pkt->id
                    << " playerId=" << newPlayerId
                    << " session=" << session
                    << std::endl;
            }
        }

        SC_LoginResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOGIN_RESULT;
        out.result = result;

        session->Send(&out);

        std::cout << "[LOGIN RESULT SENT] id=" << pkt->id
            << " result=" << (int)result
            << " playerId=" << session->GetPlayerId()
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_LOBBY_ENTER:
    {
        PlayerData data{};
        bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);
        if (!ok)
            break;

        session->SetState(SessionState::IN_LOBBY);

        SC_LobbyEnter out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_ENTER;

        strncpy_s(out.nickname, data.nickname.c_str(), _TRUNCATE);

        out.level = data.level;
        out.currentExp = data.currentExp;
        out.requiredExp = UserManager::Instance().GetRequiredExpForLevel(data.level);

        out.playerIconId = data.playerIconId;
        out.lobbyCharacterModelId = data.lobbyCharacterModelId;
        out.lobbyBackgroundId = data.lobbyBackgroundId;

        session->Send(&out);

        std::cout << "[LOBBY ENTER] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " nickname=" << data.nickname
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_LOBBY_CHARACTER_SELECT:
    {
        auto* pkt = reinterpret_cast<CS_LobbyCharacterSelect*>(header);

        bool owned = UserManager::Instance().IsCharacterOwned(
            session->GetUserId(),
            pkt->lobbyCharacterModelId
        );

        bool ok = false;

        if (owned)
        {
            ok = UserManager::Instance().SetLobbyCharacterModel(
                session->GetUserId(),
                pkt->lobbyCharacterModelId
            );
        }

        SC_LobbyCharacterSelectResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_CHARACTER_SELECT_RESULT;
        out.result = ok ? 1 : 0;
        out.lobbyCharacterModelId = pkt->lobbyCharacterModelId;

        session->Send(&out);

        break;
    }

    case PacketType::PKT_CS_LOBBY_BACKGROUND_SELECT:
    {
        auto* pkt = reinterpret_cast<CS_LobbyBackgroundSelect*>(header);

        bool ok = UserManager::Instance().SetLobbyBackground(
            session->GetUserId(),
            pkt->lobbyBackgroundId
        );

        SC_LobbyBackgroundSelectResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_BACKGROUND_SELECT_RESULT;
        out.result = ok ? 1 : 0;
        out.lobbyBackgroundId = pkt->lobbyBackgroundId;

        session->Send(&out);

        break;
    }

    case PacketType::PKT_CS_LOBBY_CHAT:
    {
        if (session->GetState() != SessionState::IN_LOBBY)
        {
            std::cout << "[LOBBY CHAT BLOCK] not in lobby playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        auto* pkt = reinterpret_cast<CS_LobbyChat*>(header);

        PlayerData data{};
        bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);
        if (!ok)
            break;

        SC_LobbyChat out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_CHAT;
        out.playerId = session->GetPlayerId();

        strncpy_s(out.nickname, data.nickname.c_str(), _TRUNCATE);
        strncpy_s(out.message, pkt->message, _TRUNCATE);

        SessionManager::Instance().BroadcastLobby(&out);

        break;
    }

    case PacketType::PKT_CS_LOBBY_CHARACTER_LIST:
    {
        CharacterOwnedData ownedData{};
        PlayerData playerData{};

        bool okOwned = UserManager::Instance().GetCharacterOwnedData(session->GetUserId(), ownedData);
        bool okPlayer = UserManager::Instance().GetPlayerData(session->GetUserId(), playerData);
        if (!okOwned || !okPlayer)
            break;

        SC_LobbyCharacterList out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_CHARACTER_LIST;
        out.count = MAX_LOBBY_CHARACTER_COUNT;
        out.currentCharacterModelId = playerData.lobbyCharacterModelId;

        out.characters[0].characterId = 0;
        out.characters[0].owned = (uint8_t)(ownedData.c0 ? 1 : 0);

        out.characters[1].characterId = 1;
        out.characters[1].owned = (uint8_t)(ownedData.c1 ? 1 : 0);

        out.characters[2].characterId = 2;
        out.characters[2].owned = (uint8_t)(ownedData.c2 ? 1 : 0);

        out.characters[3].characterId = 3;
        out.characters[3].owned = (uint8_t)(ownedData.c3 ? 1 : 0);

        session->Send(&out);

        break;
    }

    case PacketType::PKT_CS_TEMP_GAME_ROOM_ENTER:
    {
        PlayerData data{};
        bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);

        SC_TempGameRoomEnter out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_TEMP_GAME_ROOM_ENTER;

        if (!ok)
        {
            out.result = 1;
            out.playerCount = 0;
            session->Send(&out);
            break;
        }

        out.result = 0;
        out.playerCount = 1;

        out.players[0].slotIndex = 0;
        strncpy_s(out.players[0].nickname, data.nickname.c_str(), _TRUNCATE);
        out.players[0].playerIconId = data.playerIconId;
        out.players[0].logoBorderId = data.logoBorderId;
        out.players[0].rankImageId = data.rankImageId;

        out.players[1].slotIndex = 1;
        out.players[2].slotIndex = 2;

        session->Send(&out);

        break;
    }

    case PacketType::PKT_CS_TEMP_GAME_START:
    {
        SC_TempGameStartResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_TEMP_GAME_START_RESULT;
        out.result = 0;

        session->Send(&out);

        break;
    }

    case PacketType::PKT_CS_ROOM_LIST:
    {
        std::cout << "[ROOM LIST REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << std::endl;

        SC_RoomList out{};
        out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_LIST;

        std::vector<Room> roomList = RoomManager::Instance().GetRoomList();

        uint16_t count = static_cast<uint16_t>(roomList.size());
        if (count > MAX_ROOM_LIST_COUNT)
            count = MAX_ROOM_LIST_COUNT;

        out.roomCount = count;

        for (uint16_t i = 0; i < count; ++i)
        {
            const Room& room = roomList[i];
            RoomInfo& info = out.rooms[i];

            info.roomId = room.roomId;

            memset(info.roomName, 0, ROOM_NAME_LEN);
            memset(info.mapName, 0, MAP_NAME_LEN);
            memset(info.hostName, 0, HOST_NAME_LEN);

            strncpy_s(info.roomName, ROOM_NAME_LEN, room.roomName.c_str(), _TRUNCATE);
            strncpy_s(info.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);
            strncpy_s(info.hostName, HOST_NAME_LEN, room.hostName.c_str(), _TRUNCATE);

            info.currentPlayerCount = room.currentPlayerCount;
            info.maxPlayerCount = room.maxPlayerCount;
            info.state = (uint8_t)room.state;

            std::cout << "[ROOM INFO] index=" << i
                << " roomId=" << info.roomId
                << " roomName=" << info.roomName
                << " mapName=" << info.mapName
                << " hostName=" << info.hostName
                << " players=" << info.currentPlayerCount << "/" << info.maxPlayerCount
                << " state=" << (int)info.state
                << std::endl;
        }

        out.header.size =
            sizeof(PacketHeader)
            + sizeof(uint16_t)
            + sizeof(RoomInfo) * out.roomCount;

        session->Send(&out);

        std::cout << "[ROOM LIST SENT] playerId=" << session->GetPlayerId()
            << " roomCount=" << out.roomCount
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_CREATE:
    {
        std::cout << "[ROOM CREATE REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " state=" << (int)session->GetState()
            << std::endl;

        if (session->GetState() == SessionState::IN_ROOM ||
            session->GetState() == SessionState::IN_GAME)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::ALREADY_IN_ROOM;

            session->Send(&out);
            break;
        }

        if (session->GetState() != SessionState::IN_LOBBY)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::NOT_IN_LOBBY;

            session->Send(&out);
            break;
        }

        PlayerData data{};
        bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);

        if (!ok)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::PLAYER_DATA_NOT_FOUND;

            session->Send(&out);
            break;
        }

        std::string roomName = data.nickname + WideToUTF8(L"의 방");
        std::string mapName = WideToUTF8(L"기본 맵");

        RoomMember leader{};
        leader.playerId = session->GetPlayerId();
        leader.userId = session->GetUserId();
        leader.nickname = data.nickname;
        leader.level = data.level;
        leader.playerIconId = data.playerIconId;
        leader.rankImageId = data.rankImageId;
        leader.team = RoomTeam::A;
        leader.slotIndex = 0;
        leader.isLeader = true;

        uint32_t roomId = RoomManager::Instance().CreateRoom(
            roomName,
            mapName,
            data.nickname,
            leader,
            ROOM_MAX_MEMBER_COUNT
        );

        if (roomId == 0)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::CREATE_FAILED;

            session->Send(&out);
            break;
        }

        session->SetState(SessionState::IN_ROOM);
        session->SetCurrentRoomId(roomId);

        Room room{};
        RoomManager::Instance().GetRoom(roomId, room);

        SC_RoomCreateResult out{};
        FillRoomCreateResultPacket(
            out,
            room,
            (uint8_t)RoomCreateResult::SUCCESS,
            session->GetPlayerId()
        );

        session->Send(&out);

        std::cout << "[ROOM CREATE SUCCESS] roomId=" << roomId
            << " roomName=" << room.roomName
            << " leaderPlayerId=" << session->GetPlayerId()
            << " leaderNickname=" << data.nickname
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_ENTER:
    {
        auto* pkt = reinterpret_cast<CS_RoomEnter*>(header);

        std::cout << "[ROOM ENTER REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " roomId=" << pkt->roomId
            << " state=" << (int)session->GetState()
            << std::endl;

        SC_RoomEnterResult resultPkt{};
        resultPkt.header.size = sizeof(resultPkt);
        resultPkt.header.type = (uint16_t)PacketType::PKT_SC_ROOM_ENTER_RESULT;
        resultPkt.roomId = pkt->roomId;

        if (session->GetState() == SessionState::IN_ROOM ||
            session->GetState() == SessionState::IN_GAME)
        {
            resultPkt.result = (uint8_t)RoomEnterResult::ALREADY_IN_ROOM;
            session->Send(&resultPkt);
            break;
        }

        if (session->GetState() != SessionState::IN_LOBBY)
        {
            resultPkt.result = (uint8_t)RoomEnterResult::NOT_IN_LOBBY;
            session->Send(&resultPkt);
            break;
        }

        Room checkRoom{};
        if (!RoomManager::Instance().GetRoom(pkt->roomId, checkRoom))
        {
            resultPkt.result = (uint8_t)RoomEnterResult::ROOM_NOT_FOUND;
            session->Send(&resultPkt);
            break;
        }

        PlayerData data{};
        if (!UserManager::Instance().GetPlayerData(session->GetUserId(), data))
        {
            resultPkt.result = (uint8_t)RoomEnterResult::PLAYER_DATA_NOT_FOUND;
            session->Send(&resultPkt);
            break;
        }

        RoomMember member{};
        member.playerId = session->GetPlayerId();
        member.userId = session->GetUserId();
        member.nickname = data.nickname;
        member.level = data.level;
        member.playerIconId = data.playerIconId;
        member.rankImageId = data.rankImageId;
        member.isLeader = false;

        Room updatedRoom{};
        bool ok = RoomManager::Instance().EnterRoom(pkt->roomId, member, updatedRoom);

        if (!ok)
        {
            resultPkt.result = (uint8_t)RoomEnterResult::ROOM_FULL;
            session->Send(&resultPkt);
            break;
        }

        session->SetState(SessionState::IN_ROOM);
        session->SetCurrentRoomId(pkt->roomId);

        resultPkt.result = (uint8_t)RoomEnterResult::SUCCESS;
        session->Send(&resultPkt);

        BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM ENTER SUCCESS] roomId=" << pkt->roomId
            << " playerId=" << session->GetPlayerId()
            << " nickname=" << data.nickname
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_LEAVE:
    {
        std::cout << "[ROOM LEAVE REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " roomId=" << session->GetCurrentRoomId()
            << " state=" << (int)session->GetState()
            << std::endl;

        SC_RoomLeaveResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_LEAVE_RESULT;
        out.roomId = session->GetCurrentRoomId();
        out.roomDeleted = 0;

        if (session->GetState() != SessionState::IN_ROOM)
        {
            out.result = (uint8_t)RoomLeaveResult::NOT_IN_ROOM;
            session->Send(&out);
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        bool roomDeleted = false;
        Room updatedRoom{};

        bool ok = RoomManager::Instance().LeaveRoom(
            roomId,
            session->GetPlayerId(),
            roomDeleted,
            updatedRoom
        );

        if (!ok)
        {
            out.result = (uint8_t)RoomLeaveResult::ROOM_NOT_FOUND;
            session->Send(&out);
            break;
        }

        session->SetState(SessionState::IN_LOBBY);
        session->SetCurrentRoomId(0);

        out.result = (uint8_t)RoomLeaveResult::SUCCESS;
        out.roomDeleted = roomDeleted ? 1 : 0;

        session->Send(&out);

        if (!roomDeleted)
        {
            BroadcastRoomState(updatedRoom);
        }

        std::cout << "[ROOM LEAVE SUCCESS] playerId=" << session->GetPlayerId()
            << " roomId=" << roomId
            << " roomDeleted=" << roomDeleted
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_GAME_START:
    {
        std::cout << "[ROOM GAME START REQUEST] playerId="
            << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " roomId=" << session->GetCurrentRoomId()
            << " state=" << static_cast<int>(session->GetState())
            << std::endl;

        if (session->GetState() != SessionState::IN_ROOM ||
            session->GetCurrentRoomId() == 0)
        {
            SendRoomGameStartResult(session, RoomGameStartResult::NOT_IN_ROOM);
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomGameStartResult(session, RoomGameStartResult::ROOM_NOT_FOUND);
            break;
        }

        if (room.state == RoomState::PLAYING)
        {
            SendRoomGameStartResult(session, RoomGameStartResult::ALREADY_PLAYING);
            break;
        }

        if (room.leaderPlayerId != session->GetPlayerId())
        {
            SendRoomGameStartResult(session, RoomGameStartResult::NOT_LEADER);
            break;
        }

        if (!AreAllPlayersReturnedFromResult(roomId, room))
        {
            std::cout << "[ROOM GAME START BLOCK] not all players returned from result. "
                << "roomId=" << roomId
                << " leaderPlayerId=" << session->GetPlayerId()
                << std::endl;

            SendRoomGameStartResult(
                session,
                RoomGameStartResult::NOT_ALL_RETURNED_FROM_RESULT
            );

            break;
        }

        if (!RoomManager::Instance().CanStartGame(roomId))
        {
            SendRoomGameStartResult(session, RoomGameStartResult::NOT_ENOUGH_PLAYERS);
            break;
        }

        Room updatedRoom{};
        bool ok = RoomManager::Instance().StartGame(
            roomId,
            session->GetPlayerId(),
            updatedRoom
        );

        if (!ok)
        {
            SendRoomGameStartResult(session, RoomGameStartResult::FAILED);
            break;
        }

        ResetGameLoadRuntimeState(roomId);
        {
            std::lock_guard<std::mutex> lock(g_resultLeaveMutex);
            g_resultLeavePlayers.erase(roomId);
        }

        BroadcastRoomGameStartResult(updatedRoom, RoomGameStartResult::SUCCESS);

        std::cout << "[ROOM GAME START SUCCESS] roomId="
            << roomId
            << " leaderPlayerId=" << session->GetPlayerId()
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_SLOT_CHANGE:
    {
        auto* pkt = reinterpret_cast<CS_RoomSlotChange*>(header);

        RoomTeam targetTeam = static_cast<RoomTeam>(pkt->targetTeam);
        uint8_t targetSlotIndex = pkt->targetSlotIndex;

        std::cout << "[ROOM SLOT CHANGE REQUEST] playerId=" << session->GetPlayerId()
            << " roomId=" << session->GetCurrentRoomId()
            << " targetTeam=" << static_cast<int>(pkt->targetTeam)
            << " targetSlot=" << static_cast<int>(targetSlotIndex)
            << std::endl;

        if (session->GetState() != SessionState::IN_ROOM || session->GetCurrentRoomId() == 0)
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::NOT_IN_ROOM,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::ROOM_NOT_FOUND,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        RoomMember requesterMember{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), requesterMember))
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::NOT_IN_ROOM,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        if (requesterMember.team == targetTeam && requesterMember.slotIndex == targetSlotIndex)
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::SAME_SLOT,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        RoomMember targetMember{};
        bool occupied = RoomManager::Instance().GetMemberBySlot(
            roomId,
            targetTeam,
            targetSlotIndex,
            targetMember
        );

        if (!occupied)
        {
            Room updatedRoom{};
            bool moved = RoomManager::Instance().MoveMemberToSlot(
                roomId,
                session->GetPlayerId(),
                targetTeam,
                targetSlotIndex,
                updatedRoom
            );

            if (!moved)
            {
                SendRoomSlotChangeResult(
                    session,
                    RoomSlotChangeResult::INVALID_SLOT,
                    targetTeam,
                    targetSlotIndex
                );
                break;
            }

            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::SUCCESS,
                targetTeam,
                targetSlotIndex
            );

            BroadcastRoomState(updatedRoom);

            std::cout << "[ROOM SLOT MOVE SUCCESS] playerId=" << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;

            break;
        }

        if (targetMember.playerId == session->GetPlayerId())
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::SAME_SLOT,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        Session* targetSession = SessionManager::Instance().Find(targetMember.playerId);
        if (targetSession == nullptr)
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::TARGET_NOT_FOUND,
                targetTeam,
                targetSlotIndex,
                targetMember.nickname
            );
            break;
        }

        PendingSlotSwap pending{};
        pending.roomId = roomId;
        pending.requesterPlayerId = session->GetPlayerId();
        pending.targetPlayerId = targetMember.playerId;
        pending.requesterTeam = requesterMember.team;
        pending.requesterSlotIndex = requesterMember.slotIndex;
        pending.targetTeam = targetMember.team;
        pending.targetSlotIndex = targetMember.slotIndex;

        if (!AddPendingSlotSwap(pending))
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::REQUEST_ALREADY_EXISTS,
                targetTeam,
                targetSlotIndex,
                targetMember.nickname
            );
            break;
        }

        SC_RoomSlotSwapRequest requestOut{};
        requestOut.header.size = sizeof(requestOut);
        requestOut.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_SLOT_SWAP_REQUEST);

        requestOut.requesterPlayerId = session->GetPlayerId();

        memset(requestOut.requesterNickname, 0, sizeof(requestOut.requesterNickname));
        strncpy_s(
            requestOut.requesterNickname,
            sizeof(requestOut.requesterNickname),
            requesterMember.nickname.c_str(),
            _TRUNCATE
        );

        requestOut.requesterTeam = static_cast<uint8_t>(requesterMember.team);
        requestOut.requesterSlotIndex = requesterMember.slotIndex;

        requestOut.targetTeam = static_cast<uint8_t>(targetMember.team);
        requestOut.targetSlotIndex = targetMember.slotIndex;

        targetSession->Send(&requestOut);

        SendRoomSlotChangeResult(
            session,
            RoomSlotChangeResult::REQUEST_SENT,
            targetTeam,
            targetSlotIndex,
            targetMember.nickname
        );

        std::cout << "[ROOM SLOT SWAP REQUEST SENT] requester=" << session->GetPlayerId()
            << " target=" << targetMember.playerId
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_SLOT_SWAP_RESPONSE:
    {
        auto* pkt = reinterpret_cast<CS_RoomSlotSwapResponse*>(header);

        uint32_t targetPlayerId = session->GetPlayerId();
        uint32_t requesterPlayerId = pkt->requesterPlayerId;
        bool accept = pkt->accept != 0;

        std::cout << "[ROOM SLOT SWAP RESPONSE] target=" << targetPlayerId
            << " requester=" << requesterPlayerId
            << " accept=" << accept
            << std::endl;

        PendingSlotSwap pending{};
        bool found = TakePendingSlotSwap(
            targetPlayerId,
            requesterPlayerId,
            pending
        );

        if (!found)
        {
            std::cout << "[ROOM SLOT SWAP RESPONSE FAIL] pending not found"
                << std::endl;
            break;
        }

        Session* requesterSession = SessionManager::Instance().Find(requesterPlayerId);

        RoomMember targetMember{};
        RoomManager::Instance().GetMember(
            pending.roomId,
            targetPlayerId,
            targetMember
        );

        if (!accept)
        {
            if (requesterSession != nullptr)
            {
                SendRoomSlotChangeResult(
                    requesterSession,
                    RoomSlotChangeResult::REJECTED,
                    pending.targetTeam,
                    pending.targetSlotIndex,
                    targetMember.nickname
                );
            }

            std::cout << "[ROOM SLOT SWAP REJECTED] requester=" << requesterPlayerId
                << " target=" << targetPlayerId
                << std::endl;

            break;
        }

        Room updatedRoom{};
        bool swapped = RoomManager::Instance().SwapMembers(
            pending.roomId,
            requesterPlayerId,
            targetPlayerId,
            updatedRoom
        );

        if (!swapped)
        {
            if (requesterSession != nullptr)
            {
                SendRoomSlotChangeResult(
                    requesterSession,
                    RoomSlotChangeResult::FAILED,
                    pending.targetTeam,
                    pending.targetSlotIndex,
                    targetMember.nickname
                );
            }

            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::FAILED,
                pending.requesterTeam,
                pending.requesterSlotIndex
            );

            std::cout << "[ROOM SLOT SWAP FAIL] roomId=" << pending.roomId
                << std::endl;

            break;
        }

        if (requesterSession != nullptr)
        {
            SendRoomSlotChangeResult(
                requesterSession,
                RoomSlotChangeResult::SUCCESS,
                pending.targetTeam,
                pending.targetSlotIndex,
                targetMember.nickname
            );
        }

        SendRoomSlotChangeResult(
            session,
            RoomSlotChangeResult::SUCCESS,
            pending.requesterTeam,
            pending.requesterSlotIndex
        );

        BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM SLOT SWAP SUCCESS] roomId=" << pending.roomId
            << " requester=" << requesterPlayerId
            << " target=" << targetPlayerId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_TRANSFER_LEADER:
    {
        auto* pkt = reinterpret_cast<CS_RoomTransferLeader*>(header);

        uint32_t roomId = session->GetCurrentRoomId();
        uint32_t targetPlayerId = pkt->targetPlayerId;

        std::cout << "[ROOM TRANSFER LEADER REQUEST] leader="
            << session->GetPlayerId()
            << " target=" << targetPlayerId
            << " roomId=" << roomId
            << std::endl;

        if (session->GetState() != SessionState::IN_ROOM || roomId == 0)
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::NOT_IN_ROOM,
                targetPlayerId
            );
            break;
        }

        if (session->GetPlayerId() == targetPlayerId)
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::CANNOT_TARGET_SELF,
                targetPlayerId
            );
            break;
        }

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::ROOM_NOT_FOUND,
                targetPlayerId
            );
            break;
        }

        if (room.leaderPlayerId != session->GetPlayerId())
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::NOT_LEADER,
                targetPlayerId
            );
            break;
        }

        RoomMember targetMember{};
        if (!RoomManager::Instance().GetMember(roomId, targetPlayerId, targetMember))
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::TARGET_NOT_FOUND,
                targetPlayerId
            );
            break;
        }

        Room updatedRoom{};
        bool ok = RoomManager::Instance().TransferLeader(
            roomId,
            session->GetPlayerId(),
            targetPlayerId,
            updatedRoom
        );

        if (!ok)
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::FAILED,
                targetPlayerId,
                targetMember.nickname
            );
            break;
        }

        SendRoomTransferLeaderResult(
            session,
            RoomAdminResult::SUCCESS,
            targetPlayerId,
            targetMember.nickname
        );

        BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM TRANSFER LEADER SUCCESS] oldLeader="
            << session->GetPlayerId()
            << " newLeader=" << targetPlayerId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_KICK:
    {
        auto* pkt = reinterpret_cast<CS_RoomKick*>(header);

        uint32_t roomId = session->GetCurrentRoomId();
        uint32_t targetPlayerId = pkt->targetPlayerId;

        std::cout << "[ROOM KICK REQUEST] leader="
            << session->GetPlayerId()
            << " target=" << targetPlayerId
            << " roomId=" << roomId
            << std::endl;

        if (session->GetState() != SessionState::IN_ROOM || roomId == 0)
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::NOT_IN_ROOM,
                targetPlayerId
            );
            break;
        }

        if (session->GetPlayerId() == targetPlayerId)
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::CANNOT_TARGET_SELF,
                targetPlayerId
            );
            break;
        }

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::ROOM_NOT_FOUND,
                targetPlayerId
            );
            break;
        }

        if (room.leaderPlayerId != session->GetPlayerId())
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::NOT_LEADER,
                targetPlayerId
            );
            break;
        }

        RoomMember targetMember{};
        if (!RoomManager::Instance().GetMember(roomId, targetPlayerId, targetMember))
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::TARGET_NOT_FOUND,
                targetPlayerId
            );
            break;
        }

        PlayerData leaderData{};
        std::string leaderNickname = "방장";

        if (UserManager::Instance().GetPlayerData(session->GetUserId(), leaderData))
            leaderNickname = leaderData.nickname;

        bool roomDeleted = false;
        Room updatedRoom{};

        bool ok = RoomManager::Instance().KickMember(
            roomId,
            session->GetPlayerId(),
            targetPlayerId,
            roomDeleted,
            updatedRoom
        );

        if (!ok)
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::FAILED,
                targetPlayerId,
                targetMember.nickname
            );
            break;
        }

        Session* targetSession = SessionManager::Instance().Find(targetPlayerId);
        if (targetSession != nullptr)
        {
            targetSession->SetState(SessionState::IN_LOBBY);
            targetSession->SetCurrentRoomId(0);

            SendRoomKicked(
                targetSession,
                roomId,
                leaderNickname
            );
        }

        SendRoomKickResult(
            session,
            RoomAdminResult::SUCCESS,
            targetPlayerId,
            targetMember.nickname
        );

        if (!roomDeleted)
            BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM KICK SUCCESS] leader="
            << session->GetPlayerId()
            << " target=" << targetPlayerId
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_PICK_ENTER:
    {
        std::cout << "[PICK ENTER REQUEST] playerId="
            << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " roomId=" << session->GetCurrentRoomId()
            << " state=" << static_cast<int>(session->GetState())
            << std::endl;

        if (session->GetCurrentRoomId() == 0)
        {
            std::cout << "[PICK ENTER BLOCK] currentRoomId is 0" << std::endl;
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        Room readyRoom{};
        bool readyOk = RoomManager::Instance().SetPickReady(
            roomId,
            session->GetPlayerId(),
            readyRoom
        );

        if (!readyOk)
        {
            std::cout << "[PICK ENTER READY FAIL] playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        SendPickState(session);

        Room timerRoom{};
        bool timerStartOk = RoomManager::Instance().TryStartPickTimer(
            roomId,
            timerRoom
        );

        if (timerStartOk)
        {
            std::cout << "[PICK TIMER START] roomId="
                << roomId
                << std::endl;

            BroadcastPickStateToRoom(timerRoom);
            StartPickTimerThread(roomId);
        }

        break;
    }

    case PacketType::PKT_CS_PICK_HOVER:
    {
        auto* pkt = reinterpret_cast<CS_PickHover*>(header);

        std::cout << "[PICK HOVER REQUEST] playerId="
            << session->GetPlayerId()
            << " roomId=" << session->GetCurrentRoomId()
            << " characterId=" << pkt->characterId
            << std::endl;

        if (session->GetCurrentRoomId() == 0)
        {
            std::cout << "[PICK HOVER BLOCK] currentRoomId is 0" << std::endl;
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        RoomMember requester{};
        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            requester))
        {
            std::cout << "[PICK HOVER BLOCK] requester not found" << std::endl;
            break;
        }

        Room updatedRoom{};
        bool ok = RoomManager::Instance().SetPickHover(
            roomId,
            session->GetPlayerId(),
            pkt->characterId,
            updatedRoom
        );

        if (!ok)
        {
            std::cout << "[PICK HOVER FAIL] playerId="
                << session->GetPlayerId()
                << " characterId=" << pkt->characterId
                << std::endl;

            SendPickState(session);
            break;
        }

        BroadcastPickStateToTeam(updatedRoom, requester.team);

        std::cout << "[PICK HOVER SUCCESS] playerId="
            << session->GetPlayerId()
            << " characterId=" << pkt->characterId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_PICK_LOCK:
    {
        auto* pkt = reinterpret_cast<CS_PickLock*>(header);

        std::cout << "[PICK LOCK REQUEST] playerId="
            << session->GetPlayerId()
            << " roomId=" << session->GetCurrentRoomId()
            << " characterId=" << pkt->characterId
            << std::endl;

        if (session->GetCurrentRoomId() == 0)
        {
            SendPickLockResult(
                session,
                PickLockResult::NOT_IN_GAME,
                pkt->characterId
            );
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        RoomMember requester{};
        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            requester))
        {
            SendPickLockResult(
                session,
                PickLockResult::NOT_IN_GAME,
                pkt->characterId
            );
            break;
        }

        Room updatedRoom{};
        uint8_t result = static_cast<uint8_t>(PickLockResult::FAILED);

        bool ok = RoomManager::Instance().LockPick(
            roomId,
            session->GetPlayerId(),
            pkt->characterId,
            updatedRoom,
            result
        );

        SendPickLockResult(
            session,
            static_cast<PickLockResult>(result),
            pkt->characterId
        );

        if (!ok)
        {
            std::cout << "[PICK LOCK FAIL] playerId="
                << session->GetPlayerId()
                << " characterId=" << pkt->characterId
                << " result=" << static_cast<int>(result)
                << std::endl;

            SendPickState(session);
            break;
        }

        BroadcastPickStateToTeam(updatedRoom, requester.team);

        std::cout << "[PICK LOCK SUCCESS] playerId="
            << session->GetPlayerId()
            << " characterId=" << pkt->characterId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_GAME_LOAD_READY:
    {
        std::cout << "[GAME LOAD READY REQUEST] playerId="
            << session->GetPlayerId()
            << " roomId=" << session->GetCurrentRoomId()
            << " state=" << static_cast<int>(session->GetState())
            << std::endl;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[GAME LOAD READY BLOCK] roomId is 0" << std::endl;
            break;
        }

        Room readyRoom{};
        bool allReady = MarkGameLoadReadyAndCheckAll(
            roomId,
            session->GetPlayerId(),
            readyRoom
        );

        if (!allReady)
        {
            std::cout << "[GAME LOAD READY WAIT] playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        std::cout << "[GAME ALL CLIENT READY] roomId="
            << roomId
            << std::endl;

        BroadcastGameAllReady(readyRoom);
        StartGamePrepareTimerThread(roomId);

        break;
    }

    case PacketType::PKT_CS_MOVE_STATE:
    {
        auto* pkt = reinterpret_cast<CS_MoveState*>(header);

        if (header->size != sizeof(CS_MoveState))
        {
            std::cout << "[MOVE BLOCK] invalid packet size playerId="
                << session->GetPlayerId()
                << " receivedSize=" << header->size
                << " expectedSize=" << sizeof(CS_MoveState)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME &&
            session->GetState() != SessionState::IN_ROOM)
        {
            std::cout << "[MOVE BLOCK] invalid state playerId="
                << session->GetPlayerId()
                << " state=" << static_cast<int>(session->GetState())
                << std::endl;

            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[MOVE BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneActiveForPlayer(session->GetPlayerId()))
        {
            std::cout << "[MOVE BLOCK] player is controlling drone. playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        session->SetPosition(
            pkt->x,
            pkt->y,
            pkt->z
        );

        session->SetYaw(pkt->yaw);
        session->SetPitch(pkt->pitch);

        uint8_t animIndex = pkt->animIndex;

        // 클라이언트 애니메이션 인덱스
        //
        // 0 ~ 5   : 권총
        // 6 ~ 11  : 긴 총
        // 12 ~ 17 : 칼
        constexpr uint8_t MAX_ANIM_INDEX = 17;

        if (animIndex > MAX_ANIM_INDEX)
        {
            std::cout << "[MOVE ANIM INVALID] playerId="
                << session->GetPlayerId()
                << " animIndex="
                << static_cast<int>(animIndex)
                << std::endl;

            animIndex = 0;
        }

        session->SetAnimIndex(animIndex);

        SC_MoveState out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_MOVE_STATE
            );

        out.playerId = session->GetPlayerId();

        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;

        out.yaw = pkt->yaw;
        out.pitch = pkt->pitch;

        out.animIndex = animIndex;
        out.isCrouching = pkt->isCrouching != 0 ? 1 : 0;
        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        break;
    }

    case PacketType::PKT_CS_HIT:
    {
        auto* pkt = reinterpret_cast<CS_Hit*>(header);

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[HIT BLOCK] attacker not in game. playerId="
                << session->GetPlayerId()
                << " state=" << static_cast<int>(session->GetState())
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "HIT"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[HIT BLOCK] roomId is 0. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        Session* target = SessionManager::Instance().Find(pkt->targetId);
        if (target == nullptr)
            break;

        if (target->GetCurrentRoomId() != roomId)
        {
            std::cout << "[HIT BLOCK] different room. attacker="
                << session->GetPlayerId()
                << " target=" << target->GetPlayerId()
                << std::endl;
            break;
        }

        RoomMember attackerMember{};
        RoomMember targetMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            attackerMember))
        {
            std::cout << "[HIT BLOCK] attacker member not found. attacker="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        if (!RoomManager::Instance().GetMember(
            roomId,
            target->GetPlayerId(),
            targetMember))
        {
            std::cout << "[HIT BLOCK] target member not found. target="
                << target->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        if (attackerMember.team == targetMember.team)
        {
            std::cout << "[HIT BLOCK] friendly fire blocked. attacker="
                << session->GetPlayerId()
                << " target=" << target->GetPlayerId()
                << " team=" << static_cast<int>(attackerMember.team)
                << std::endl;
            break;
        }

        if (target->IsDead())
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
        {
            std::cout << "[HIT BLOCK] weapon not owned. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        PlayerWeaponState* weaponState = session->GetWeaponState(weaponId);

        if (weaponState == nullptr)
            break;

        const ServerWeaponData* weaponData = ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
        {
            std::cout << "[HIT BLOCK] weapon data not found. weaponId="
                << weaponId
                << std::endl;
            break;
        }

        if (weaponData->weaponType != ServerWeaponType::Knife)
        {
            if (weaponState->magAmmo < 0)
            {
                SendAmmoUpdate(session, *weaponState);
                break;
            }
        }
        target->SetLastDamager(session->GetPlayerId());

        target->AddAssistDamageRecord(
            session->GetPlayerId(),
            weaponData->damage
        );

        target->Damage(weaponData->damage);

        SC_HPUpdate hpPkt{};
        FillHPUpdatePacket(hpPkt, target);

        BroadcastPacketToRoomInternal(roomId, &hpPkt);

        std::cout << "[HIT APPLY] attacker="
            << session->GetPlayerId()
            << " target=" << target->GetPlayerId()
            << " weaponId=" << weaponId
            << " damage=" << weaponData->damage
            << " ammo=" << weaponState->magAmmo
            << "/" << weaponState->reserveAmmo
            << " targetHp=" << target->GetHP()
            << " targetBarrier=" << target->GetBarrier()
            << std::endl;

        if (target->IsDead())
        {
            HandleDeath(target);
        }

        break;
    }

    case PacketType::PKT_CS_WEAPON_SWITCH:
    {
        auto* pkt = reinterpret_cast<CS_WeaponSwitch*>(header);

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[WEAPON SWITCH BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "WEAPON_SWITCH"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[WEAPON SWITCH BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (!session->HasWeapon(pkt->weaponId))
        {
            std::cout << "[WEAPON SWITCH BLOCK] not owned playerId="
                << session->GetPlayerId()
                << " weaponId=" << pkt->weaponId
                << std::endl;
            break;
        }

        int previousWeaponId = session->GetCurrentWeaponId();

        // 장전 중 다른 무기로 바꾸면 이전 무기의 장전을 서버에서도 취소한다.
        if (previousWeaponId != pkt->weaponId)
        {
            PlayerWeaponState* previousState =
                session->GetWeaponState(previousWeaponId);

            if (previousState != nullptr && previousState->isReloading)
            {
                previousState->isReloading = false;

                BroadcastReloadVisualInternal(
                    roomId,
                    session->GetPlayerId(),
                    previousWeaponId,
                    false
                );

                std::cout << "[RELOAD CANCEL BY SWITCH] playerId="
                    << session->GetPlayerId()
                    << " weaponId=" << previousWeaponId
                    << " nextWeaponId=" << pkt->weaponId
                    << std::endl;
            }
        }

        session->SetCurrentWeaponId(pkt->weaponId);

        PlayerWeaponState* weaponState = session->GetWeaponState(pkt->weaponId);
        if (weaponState != nullptr)
            SendAmmoUpdate(session, *weaponState);

        BroadcastWeaponSwitchNotify(
            roomId,
            session->GetPlayerId(),
            pkt->weaponId
        );

        std::cout << "[WEAPON SWITCH BROADCAST] playerId="
            << session->GetPlayerId()
            << " weaponId=" << pkt->weaponId
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ADS_STATE:
    {
        auto* pkt = reinterpret_cast<CS_ADSState*>(header);

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[ADS BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "ADS"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[ADS BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        SC_ADSState out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ADS_STATE);

        out.playerId = session->GetPlayerId();
        out.isADS = pkt->isADS != 0 ? 1 : 0;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[ADS STATE BROADCAST] playerId="
            << session->GetPlayerId()
            << " isADS=" << static_cast<int>(out.isADS)
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_RELOAD:
    {
        if (header->size != sizeof(CS_Reload))
        {
            std::cout << "[RELOAD BLOCK] invalid packet size. playerId="
                << session->GetPlayerId()
                << " receivedSize=" << header->size
                << " expectedSize=" << sizeof(CS_Reload)
                << std::endl;
            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[RELOAD BLOCK] not in game. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "RELOAD"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();
        if (roomId == 0)
            break;

        int weaponId = session->GetCurrentWeaponId();

        PlayerWeaponState* weaponState =
            session->GetWeaponState(weaponId);

        if (weaponState == nullptr)
        {
            std::cout << "[RELOAD BLOCK] weapon state null. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        // 장전 시작 상태가 없거나 무기 변경으로 취소된 장전은 적용하지 않는다.
        if (!weaponState->isReloading)
        {
            std::cout << "[RELOAD BLOCK] reload is not active. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;

            SendAmmoUpdate(session, *weaponState);
            break;
        }

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
        {
            weaponState->isReloading = false;
            break;
        }

        // 이 시점부터 완료 또는 실패 모두 장전 상태를 종료한다.
        weaponState->isReloading = false;

        if (weaponData->weaponType == ServerWeaponType::Knife ||
            weaponState->magAmmo >= weaponData->magSize ||
            weaponState->reserveAmmo <= 0)
        {
            BroadcastReloadVisualInternal(
                roomId,
                session->GetPlayerId(),
                weaponId,
                false
            );

            SendAmmoUpdate(session, *weaponState);
            break;
        }

        int needAmmo = weaponData->magSize - weaponState->magAmmo;
        int reloadAmount = std::min(needAmmo, weaponState->reserveAmmo);

        weaponState->magAmmo += reloadAmount;
        weaponState->reserveAmmo -= reloadAmount;

        SendAmmoUpdate(session, *weaponState);

        BroadcastReloadVisualInternal(
            roomId,
            session->GetPlayerId(),
            weaponId,
            false
        );

        std::cout << "[RELOAD APPLY] playerId="
            << session->GetPlayerId()
            << " weaponId=" << weaponId
            << " ammo=" << weaponState->magAmmo
            << "/" << weaponState->reserveAmmo
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_FIRE:
    {
        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[FIRE BLOCK] not in game. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "FIRE"))
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
        {
            std::cout << "[FIRE BLOCK] weapon not owned. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        PlayerWeaponState* weaponState = session->GetWeaponState(weaponId);
        if (weaponState == nullptr)
        {
            std::cout << "[FIRE BLOCK] weapon state null. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        if (weaponState->isReloading)
        {
            std::cout << "[FIRE BLOCK] weapon is reloading. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        const ServerWeaponData* weaponData = ServerWeaponTable::Find(weaponId);
        if (weaponData == nullptr)
        {
            std::cout << "[FIRE BLOCK] weapon data null. weaponId="
                << weaponId
                << std::endl;
            break;
        }

        if (weaponData->weaponType == ServerWeaponType::Knife)
        {
            SendAmmoUpdate(session, *weaponState);
            break;
        }

        if (weaponState->magAmmo <= 0)
        {
            std::cout << "[FIRE BLOCK] no ammo. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;

            SendAmmoUpdate(session, *weaponState);
            break;
        }

        weaponState->magAmmo -= 1;

        SendAmmoUpdate(session, *weaponState);

        uint32_t roomId =
            session->GetCurrentRoomId();

        SC_FireNotify fireOut{};

        fireOut.header.size =
            sizeof(fireOut);

        fireOut.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_FIRE_NOTIFY
                );

        fireOut.playerId =
            session->GetPlayerId();

        fireOut.weaponId =
            weaponId;

        BroadcastPacketToRoomInternal(
            roomId,
            &fireOut
        );

        std::cout << "[FIRE APPLY] playerId="
            << session->GetPlayerId()
            << " weaponId=" << weaponId
            << " ammo=" << weaponState->magAmmo
            << "/" << weaponState->reserveAmmo
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_KNIFE_ATTACK:
    {
        if (header->size != sizeof(CS_KnifeAttack))
        {
            std::cout << "[KNIFE ATTACK BLOCK] invalid packet size playerId="
                << session->GetPlayerId()
                << " receivedSize=" << header->size
                << " expectedSize=" << sizeof(CS_KnifeAttack)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[KNIFE ATTACK BLOCK] not in game playerId="
                << session->GetPlayerId()
                << " state="
                << static_cast<int>(session->GetState())
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout << "[KNIFE ATTACK BLOCK] player dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneBlockedAction(session, "KNIFE_ATTACK"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[KNIFE ATTACK BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        int currentWeaponId =
            session->GetCurrentWeaponId();

        if (!session->HasWeapon(currentWeaponId))
        {
            std::cout << "[KNIFE ATTACK BLOCK] weapon not owned playerId="
                << session->GetPlayerId()
                << " weaponId=" << currentWeaponId
                << std::endl;

            break;
        }

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(currentWeaponId);

        if (weaponData == nullptr)
        {
            std::cout << "[KNIFE ATTACK BLOCK] weapon data not found playerId="
                << session->GetPlayerId()
                << " weaponId=" << currentWeaponId
                << std::endl;

            break;
        }

        if (weaponData->weaponType != ServerWeaponType::Knife)
        {
            std::cout << "[KNIFE ATTACK BLOCK] current weapon is not knife playerId="
                << session->GetPlayerId()
                << " weaponId=" << currentWeaponId
                << std::endl;

            break;
        }

        SC_KnifeAttackNotify out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_KNIFE_ATTACK_NOTIFY
            );

        out.playerId = session->GetPlayerId();

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout << "[KNIFE ATTACK BROADCAST] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << " weaponId=" << currentWeaponId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_RESULT_LEAVE:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[RESULT LEAVE BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        Room updatedRoom{};

        bool ok = RoomManager::Instance().FinishGameAndReturnRoom(
            roomId,
            updatedRoom
        );

        if (!ok)
        {
            std::cout << "[RESULT LEAVE BLOCK] room not found roomId="
                << roomId
                << std::endl;
            break;
        }

        session->SetState(SessionState::IN_ROOM);

        {
            std::lock_guard<std::mutex> lock(g_resultLeaveMutex);
            g_resultLeavePlayers[roomId].insert(session->GetPlayerId());
        }

        SC_ResultLeaveDone out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_RESULT_LEAVE_DONE);
        out.roomId = roomId;

        session->Send(&out);

        BroadcastRoomState(updatedRoom);

        std::cout << "[RESULT LEAVE] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << " returnRoomState=WAITING"
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_STATE_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_RoomStateRequest*>(header);

        uint32_t roomId = pkt->roomId;

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            std::cout << "[ROOM STATE REQUEST FAIL] room not found roomId="
                << roomId
                << std::endl;
            break;
        }

        bool foundMember = false;

        for (const RoomMember& member : room.members)
        {
            if (member.playerId == session->GetPlayerId())
            {
                foundMember = true;
                break;
            }
        }

        if (!foundMember)
        {
            std::cout << "[ROOM STATE REQUEST FAIL] player not in room playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        session->SetState(SessionState::IN_ROOM);
        session->SetCurrentRoomId(roomId);

        SC_RoomState out{};
        FillRoomStatePacket(out, room);

        session->Send(&out);

        std::cout << "[ROOM STATE REQUEST OK] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SPIKE_PICKUP_REQUEST:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        std::cout << "[SPIKE PICKUP REQUEST] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << " state=" << static_cast<int>(session->GetState())
            << std::endl;

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        // 추가
        if (session->IsDead())
        {
            std::cout << "[SPIKE PICKUP BLOCK] player is dead. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "SPIKE_PICKUP"))
            break;

        RoomMember member{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        bool pickupOk = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            RoomTeam attackTeam = GetAttackTeam(state.currentRound);

            std::cout << "[SPIKE PICKUP CHECK] "
                << "playerId=" << session->GetPlayerId()
                << " memberTeam=" << static_cast<int>(member.team)
                << " attackTeam=" << static_cast<int>(attackTeam)
                << " planted=" << state.spikePlanted
                << " carrier=" << state.spikeCarrierPlayerId
                << " resolving=" << state.roundResolving
                << " matchEnded=" << state.matchEnded
                << std::endl;

            if (state.roundResolving || state.matchEnded)
                break;

            if (state.spikePlanted)
                break;

            if (member.team != attackTeam)
                break;

            if (state.spikeCarrierPlayerId != 0)
                break;

            state.spikeCarrierPlayerId = session->GetPlayerId();
            pickupOk = true;
        }

        if (pickupOk)
        {
            std::cout << "[SPIKE PICKUP SUCCESS] carrier="
                << session->GetPlayerId()
                << std::endl;

            BroadcastSpikePickupNotify(roomId, session->GetPlayerId());
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DROP_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_SpikeDropRequest*>(header);

        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        if (session->IsDead())
            break;

        if (IsDroneBlockedAction(session, "SPIKE_DROP"))
            break;

        bool dropOk = false;

        Vector3 dropPos{};
        dropPos.x = pkt->x;
        dropPos.y = pkt->y;
        dropPos.z = pkt->z;

        Vector3 dropDir{};
        dropDir.x = pkt->dirX;
        dropDir.y = pkt->dirY;
        dropDir.z = pkt->dirZ;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                break;

            if (state.spikePlanted)
                break;

            if (state.spikeCarrierPlayerId != session->GetPlayerId())
                break;

            state.spikeCarrierPlayerId = 0;
            dropOk = true;
        }

        if (dropOk)
        {
            BroadcastSpikeDropped(
                roomId,
                dropPos.x,
                dropPos.y,
                dropPos.z,
                dropDir.x,
                dropDir.y,
                dropDir.z,
                session->GetPlayerId()
            );

            std::cout << "[SPIKE DROP REQUEST OK] playerId="
                << session->GetPlayerId()
                << " pos=(" << dropPos.x << ", " << dropPos.y << ", " << dropPos.z << ")"
                << " dir=(" << dropDir.x << ", " << dropDir.y << ", " << dropDir.z << ")"
                << std::endl;
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_PLANT_COMPLETE:
    {
        auto* pkt = reinterpret_cast<CS_SpikePlantComplete*>(header);

        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        if (IsDroneBlockedAction(session, "SPIKE_PLANT"))
            break;

        RoomMember member{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        bool plantOk = false;
        uint64_t spikeGeneration = 0;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                break;

            if (state.spikePlanted)
                break;

            RoomTeam attackTeam = GetAttackTeam(state.currentRound);

            if (member.team != attackTeam)
                break;

            if (state.spikeCarrierPlayerId != session->GetPlayerId())
                break;

            state.spikeCarrierPlayerId = 0;
            state.spikePlanted = true;

            state.spikeDefuserPlayerId = 0;
            state.spikeHalfDefused = false;

            // 이번 설치만의 고유 번호를 발급한다.
            state.spikePlantGeneration++;
            spikeGeneration = state.spikePlantGeneration;

            plantOk = true;
        }

        if (plantOk)
        {
            BroadcastSpikePlanted(
                roomId,
                pkt->x,
                pkt->y,
                pkt->z
            );

            int plantCredit = GetCreditReward("SPIKE_PLANT");

            session->AddCredits(plantCredit);

            std::cout << "[CREDIT SPIKE PLANT] playerId="
                << session->GetPlayerId()
                << " add=" << plantCredit
                << " total=" << session->GetCredits()
                << std::endl;

            BroadcastScoreboardUpdate(roomId);

            StartSpikeTimerThread(
                roomId,
                spikeGeneration
            );
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DEFUSE_COMPLETE:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        if (IsDroneBlockedAction(session, "SPIKE_DEFUSE_COMPLETE"))
            break;

        RoomMember member{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        bool defuseOk = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                break;

            if (!state.spikePlanted)
                break;

            RoomTeam defenseTeam = GetDefenseTeam(state.currentRound);

            if (member.team != defenseTeam)
                break;

            if (state.spikeDefuserPlayerId != session->GetPlayerId())
                break;

            defuseOk = true;
        }

        if (defuseOk)
        {
            ResolveRoundBySpikeDefuse(
                roomId,
                session->GetPlayerId()
            );
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DEFUSE_START:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        if (IsDroneBlockedAction(session, "SPIKE_DEFUSE_START"))
            break;

        RoomMember member{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        bool startOk = false;
        bool halfDefused = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (!state.spikePlanted || state.roundResolving || state.matchEnded)
                break;

            RoomTeam defenseTeam = GetDefenseTeam(state.currentRound);

            if (member.team != defenseTeam)
                break;

            if (state.spikeDefuserPlayerId != 0)
                break;

            state.spikeDefuserPlayerId = session->GetPlayerId();
            halfDefused = state.spikeHalfDefused;
            startOk = true;
        }

        if (startOk)
        {
            BroadcastSpikeDefuseState(
                roomId,
                1,
                halfDefused,
                session->GetPlayerId()
            );
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DEFUSE_CANCEL:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        bool cancelOk = false;
        bool halfDefused = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (state.spikeDefuserPlayerId != session->GetPlayerId())
                break;

            state.spikeDefuserPlayerId = 0;
            halfDefused = state.spikeHalfDefused;
            cancelOk = true;
        }

        if (cancelOk)
        {
            BroadcastSpikeDefuseState(
                roomId,
                0,
                halfDefused,
                0
            );
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DEFUSE_HALF:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        bool halfOk = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (!state.spikePlanted || state.roundResolving || state.matchEnded)
                break;

            if (state.spikeDefuserPlayerId != session->GetPlayerId())
                break;

            if (!state.spikeHalfDefused)
            {
                state.spikeHalfDefused = true;
                halfOk = true;
            }
        }

        if (halfOk)
        {
            BroadcastSpikeDefuseState(
                roomId,
                2,
                true,
                session->GetPlayerId()
            );
        }

        break;
    }

    case PacketType::PKT_CS_WEAPON_DROP_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_WeaponDropRequest*>(header);

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (IsDroneBlockedAction(session, "WEAPON_DROP"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        int weaponId = pkt->weaponId;

        if (!session->HasWeapon(weaponId))
        {
            std::cout << "[WEAPON DROP BLOCK] not owned. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        PlayerWeaponState* weaponState = session->GetWeaponState(weaponId);

        if (weaponState == nullptr)
            break;

        int magAmmo = weaponState->magAmmo;
        int reserveAmmo = weaponState->reserveAmmo;

        session->RemoveWeapon(weaponId);

        CreateDroppedWeapon(
            roomId,
            session->GetPlayerId(),
            weaponId,
            magAmmo,
            reserveAmmo,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->dirX,
            pkt->dirY,
            pkt->dirZ
        );

        int currentWeaponId = session->GetCurrentWeaponId();

        PlayerWeaponState* currentState = session->GetWeaponState(currentWeaponId);

        if (currentState != nullptr)
            SendAmmoUpdate(session, *currentState);

        if (currentWeaponId > 0)
        {
            BroadcastWeaponSwitchNotify(
                roomId,
                session->GetPlayerId(),
                currentWeaponId
            );
        }

        std::cout << "[WEAPON DROP APPLY] playerId="
            << session->GetPlayerId()
            << " droppedWeaponId=" << weaponId
            << " currentWeaponId=" << currentWeaponId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_WEAPON_PICKUP_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_WeaponPickupRequest*>(header);

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (IsDroneBlockedAction(session, "WEAPON_PICKUP"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        DroppedWeaponState dropped{};

        {
            std::lock_guard<std::mutex> lock(g_droppedWeaponMutex);

            auto it = g_droppedWeapons.find(pkt->dropId);

            if (it == g_droppedWeapons.end())
            {
                std::cout << "[WEAPON PICKUP BLOCK] drop not found. dropId="
                    << pkt->dropId
                    << std::endl;
                break;
            }

            if (it->second.roomId != roomId)
            {
                std::cout << "[WEAPON PICKUP BLOCK] different room. dropId="
                    << pkt->dropId
                    << std::endl;
                break;
            }

            dropped = it->second;
            g_droppedWeapons.erase(it);
        }

        int replaceWeaponId =
            FindReplaceWeaponIdForPickup(session, dropped.weaponId);

        if (replaceWeaponId >= 0)
        {
            PlayerWeaponState* replaceState =
                session->GetWeaponState(replaceWeaponId);

            if (replaceState != nullptr)
            {
                int oldMag = replaceState->magAmmo;
                int oldReserve = replaceState->reserveAmmo;

                session->RemoveWeapon(replaceWeaponId);

                Vector3 pos = session->GetPosition();

                float yawRad = session->GetYaw() * 3.14159265f / 180.0f;

                float dirX = sinf(yawRad);
                float dirZ = cosf(yawRad);

                float dropX = pos.x + dirX * 1.0f;
                float dropY = pos.y + 0.6f;
                float dropZ = pos.z + dirZ * 1.0f;

                CreateDroppedWeapon(
                    roomId,
                    session->GetPlayerId(),
                    replaceWeaponId,
                    oldMag,
                    oldReserve,
                    dropX,
                    dropY,
                    dropZ,
                    dirX,
                    0.f,
                    dirZ
                );
            }
        }

        session->GiveWeapon(
            dropped.weaponId,
            dropped.magAmmo,
            dropped.reserveAmmo
        );

        session->SetCurrentWeaponId(dropped.weaponId);

        PlayerWeaponState* newWeaponState =
            session->GetWeaponState(dropped.weaponId);

        if (newWeaponState != nullptr)
            SendAmmoUpdate(session, *newWeaponState);

        SC_WeaponPickupNotify out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_WEAPON_PICKUP_NOTIFY);

        out.dropId = dropped.dropId;
        out.pickerPlayerId = session->GetPlayerId();
        out.weaponId = dropped.weaponId;
        out.magAmmo = dropped.magAmmo;
        out.reserveAmmo = dropped.reserveAmmo;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[WEAPON PICKUP APPLY] playerId="
            << session->GetPlayerId()
            << " weaponId=" << dropped.weaponId
            << " ammo=" << dropped.magAmmo << "/" << dropped.reserveAmmo
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_STORE_BUY_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_StoreBuyRequest*>(header);

        StoreItemType itemType =
            static_cast<StoreItemType>(pkt->itemType);

        if (session->GetState() != SessionState::IN_GAME)
        {
            SendStoreBuyResult(
                session,
                StoreBuyResult::NOT_IN_GAME,
                itemType,
                pkt->itemId,
                0,
                0,
                0,
                0
            );
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            SendStoreBuyResult(
                session,
                StoreBuyResult::NOT_IN_GAME,
                itemType,
                pkt->itemId,
                0,
                0,
                0,
                0
            );
            break;
        }

        if (!IsBuyPhase(roomId))
        {
            SendStoreBuyResult(
                session,
                StoreBuyResult::NOT_BUY_PHASE,
                itemType,
                pkt->itemId,
                0,
                0,
                0,
                0
            );
            break;
        }

        if (itemType == StoreItemType::WEAPON)
        {
            auto it = g_storeWeapons.find(pkt->itemId);

            if (it == g_storeWeapons.end() || !it->second.purchasable)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::INVALID_ITEM,
                    itemType,
                    pkt->itemId,
                    0,
                    0,
                    0,
                    0
                );
                break;
            }

            StoreWeaponData storeData = it->second;

            const ServerWeaponData* weaponData =
                ServerWeaponTable::Find(storeData.weaponId);

            if (weaponData == nullptr)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::INVALID_ITEM,
                    itemType,
                    pkt->itemId,
                    storeData.price,
                    storeData.weaponId,
                    0,
                    0
                );
                break;
            }

            if (session->GetCredits() < storeData.price)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::NOT_ENOUGH_CREDIT,
                    itemType,
                    pkt->itemId,
                    storeData.price,
                    storeData.weaponId,
                    0,
                    0
                );
                break;
            }

            if (session->HasWeapon(storeData.weaponId))
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::ALREADY_OWNED,
                    itemType,
                    pkt->itemId,
                    storeData.price,
                    storeData.weaponId,
                    0,
                    0
                );
                break;
            }

            int replaceWeaponId =
                FindReplaceWeaponIdForPickup(session, storeData.weaponId);

            if (replaceWeaponId >= 0)
            {
                PlayerWeaponState* oldWeaponState =
                    session->GetWeaponState(replaceWeaponId);

                int oldMagAmmo = 0;
                int oldReserveAmmo = 0;

                if (oldWeaponState != nullptr)
                {
                    oldMagAmmo = oldWeaponState->magAmmo;
                    oldReserveAmmo = oldWeaponState->reserveAmmo;
                }

                Vector3 pos = session->GetPosition();

                float yawRad = session->GetYaw() * 3.14159265f / 180.0f;

                float dirX = sinf(yawRad);
                float dirZ = cosf(yawRad);

                float dropX = pos.x + dirX * 1.0f;
                float dropY = pos.y + 0.6f;
                float dropZ = pos.z + dirZ * 1.0f;

                CreateDroppedWeapon(
                    roomId,
                    session->GetPlayerId(),
                    replaceWeaponId,
                    oldMagAmmo,
                    oldReserveAmmo,
                    dropX,
                    dropY,
                    dropZ,
                    dirX,
                    0.0f,
                    dirZ
                );

                session->RemoveWeapon(replaceWeaponId);
            }

            session->AddCredits(-storeData.price);

            session->GiveWeapon(
                storeData.weaponId,
                weaponData->magSize,
                weaponData->maxReserveAmmo
            );

            session->SetCurrentWeaponId(storeData.weaponId);

            PlayerWeaponState* newWeaponState =
                session->GetWeaponState(storeData.weaponId);

            if (newWeaponState != nullptr)
                SendAmmoUpdate(session, *newWeaponState);

            SendStoreBuyResult(
                session,
                StoreBuyResult::SUCCESS,
                itemType,
                pkt->itemId,
                storeData.price,
                storeData.weaponId,
                weaponData->magSize,
                weaponData->maxReserveAmmo
            );

            // dropId = 0 이면 상점 구매다.
            // 클라는 바닥 아이템 삭제하지 말고 무기만 적용하면 된다.
            BroadcastWeaponPickupNotify(
                roomId,
                0,
                session->GetPlayerId(),
                storeData.weaponId,
                weaponData->magSize,
                weaponData->maxReserveAmmo
            );

            BroadcastWeaponSwitchNotify(
                roomId,
                session->GetPlayerId(),
                storeData.weaponId
            );

            BroadcastScoreboardUpdate(roomId);

            std::cout << "[STORE BUY WEAPON SUCCESS] playerId="
                << session->GetPlayerId()
                << " weaponId=" << storeData.weaponId
                << " price=" << storeData.price
                << " credits=" << session->GetCredits()
                << std::endl;

            break;
        }

        if (itemType == StoreItemType::ARMOR)
        {
            auto it = g_storeArmors.find(pkt->itemId);

            if (it == g_storeArmors.end())
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::INVALID_ITEM,
                    itemType,
                    pkt->itemId,
                    0,
                    0,
                    0,
                    0
                );
                break;
            }

            StoreArmorData armorData = it->second;

            // 남아 있는 보호막이 있고, 현재 등급이 구매하려는 등급 이상일 때만 재구매 차단.
            // 예: 50 보호막 보유 중 -> 50 재구매 불가, 100 업그레이드 가능.
            // 보호막이 0이면 파괴된 상태이므로 같은 등급도 다시 구매 가능.
            bool hasActiveArmor = session->GetBarrier() > 0;
            bool alreadyOwnsSameOrBetterArmor =
                hasActiveArmor &&
                session->GetMaxBarrier() >= armorData.barrier;

            if (alreadyOwnsSameOrBetterArmor)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::ALREADY_OWNED,
                    itemType,
                    pkt->itemId,
                    armorData.price,
                    0,
                    0,
                    0
                );
                break;
            }

            if (session->GetCredits() < armorData.price)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::NOT_ENOUGH_CREDIT,
                    itemType,
                    pkt->itemId,
                    armorData.price,
                    0,
                    0,
                    0
                );
                break;
            }

            session->AddCredits(-armorData.price);
            session->SetArmor(armorData.barrier);

            SC_HPUpdate hpOut{};
            FillHPUpdatePacket(hpOut, session);
            BroadcastPacketToRoomInternal(roomId, &hpOut);

            SendStoreBuyResult(
                session,
                StoreBuyResult::SUCCESS,
                itemType,
                pkt->itemId,
                armorData.price,
                0,
                0,
                0
            );

            BroadcastScoreboardUpdate(roomId);

            std::cout << "[STORE BUY ARMOR SUCCESS] playerId="
                << session->GetPlayerId()
                << " armor=" << armorData.barrier
                << " price=" << armorData.price
                << " credits=" << session->GetCredits()
                << std::endl;

            break;
        }

        SendStoreBuyResult(
            session,
            StoreBuyResult::INVALID_ITEM,
            itemType,
            pkt->itemId,
            0,
            0,
            0,
            0
        );

        break;
    }

    case PacketType::PKT_CS_RELOAD_VISUAL:
    {
        auto* pkt =
            reinterpret_cast<CS_ReloadVisual*>(
                header
                );

        if (header->size != sizeof(CS_ReloadVisual))
            break;

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (IsDroneBlockedAction(session, "RELOAD_VISUAL"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();
        if (roomId == 0)
            break;

        PlayerWeaponState* weaponState =
            session->GetWeaponState(pkt->weaponId);

        if (weaponState == nullptr || !weaponState->owned)
            break;

        bool wantsReload = pkt->isReloading != 0;

        if (wantsReload)
        {
            // 장전 시작은 현재 들고 있는 무기에 대해서만 허용한다.
            if (pkt->weaponId != session->GetCurrentWeaponId())
            {
                std::cout << "[RELOAD START BLOCK] not current weapon. playerId="
                    << session->GetPlayerId()
                    << " weaponId=" << pkt->weaponId
                    << " currentWeaponId=" << session->GetCurrentWeaponId()
                    << std::endl;
                break;
            }

            const ServerWeaponData* weaponData =
                ServerWeaponTable::Find(pkt->weaponId);

            if (weaponData == nullptr ||
                weaponData->weaponType == ServerWeaponType::Knife ||
                weaponState->magAmmo >= weaponData->magSize ||
                weaponState->reserveAmmo <= 0)
            {
                weaponState->isReloading = false;

                SendAmmoUpdate(session, *weaponState);
                break;
            }

            weaponState->isReloading = true;

            BroadcastReloadVisualInternal(
                roomId,
                session->GetPlayerId(),
                pkt->weaponId,
                true
            );

            std::cout << "[RELOAD START ACCEPT] playerId="
                << session->GetPlayerId()
                << " weaponId=" << pkt->weaponId
                << std::endl;
        }
        else
        {
            // 취소는 이미 다른 무기로 바뀐 뒤 도착해도 허용한다.
            // 해당 무기의 남아 있는 장전 상태만 제거한다.
            bool wasReloading = weaponState->isReloading;

            weaponState->isReloading = false;

            BroadcastReloadVisualInternal(
                roomId,
                session->GetPlayerId(),
                pkt->weaponId,
                false
            );

            if (wasReloading)
            {
                std::cout << "[RELOAD CANCEL ACCEPT] playerId="
                    << session->GetPlayerId()
                    << " weaponId=" << pkt->weaponId
                    << std::endl;
            }
        }

        break;
    }

    case PacketType::PKT_CS_IN_GAME_CHAT:
    {
        if (header->size !=
            sizeof(CS_InGameChat))
        {
            std::cout
                << "[IN GAME CHAT BLOCK] invalid size="
                << header->size
                << std::endl;

            break;
        }

        if (session->GetState() !=
            SessionState::IN_GAME)
        {
            std::cout
                << "[IN GAME CHAT BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        auto* pkt =
            reinterpret_cast<CS_InGameChat*>(
                header
                );

        bool isTeamChat =
            pkt->channel ==
            static_cast<uint8_t>(
                InGameChatChannel::TEAM
                );

        bool isMatchChat =
            pkt->channel ==
            static_cast<uint8_t>(
                InGameChatChannel::MATCH
                );

        if (!isTeamChat &&
            !isMatchChat)
        {
            std::cout
                << "[IN GAME CHAT BLOCK] invalid channel="
                << static_cast<int>(pkt->channel)
                << std::endl;

            break;
        }

        size_t messageLength =
            strnlen_s(
                pkt->message,
                IN_GAME_CHAT_MESSAGE_LEN
            );

        if (messageLength == 0 ||
            messageLength >=
            IN_GAME_CHAT_MESSAGE_LEN)
        {
            std::cout
                << "[IN GAME CHAT BLOCK] invalid message length="
                << messageLength
                << std::endl;

            break;
        }

        Room room{};

        if (!RoomManager::Instance().GetRoom(
            roomId,
            room))
        {
            break;
        }

        RoomMember senderMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            senderMember))
        {
            break;
        }

        SC_InGameChat out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_IN_GAME_CHAT
                );

        out.senderPlayerId =
            session->GetPlayerId();

        out.channel =
            pkt->channel;

        out.senderTeam =
            static_cast<uint8_t>(
                senderMember.team
                );

        memset(
            out.nickname,
            0,
            sizeof(out.nickname)
        );

        strncpy_s(
            out.nickname,
            sizeof(out.nickname),
            senderMember.nickname.c_str(),
            _TRUNCATE
        );

        memset(
            out.message,
            0,
            sizeof(out.message)
        );

        strncpy_s(
            out.message,
            sizeof(out.message),
            pkt->message,
            _TRUNCATE
        );

        int sentCount = 0;

        for (const RoomMember& receiverMember :
            room.members)
        {
            if (isTeamChat &&
                receiverMember.team !=
                senderMember.team)
            {
                continue;
            }

            Session* receiver =
                SessionManager::Instance().Find(
                    receiverMember.playerId
                );

            if (receiver == nullptr)
                continue;

            if (receiver->GetCurrentRoomId() !=
                roomId)
            {
                continue;
            }

            if (receiver->GetState() !=
                SessionState::IN_GAME)
            {
                continue;
            }

            receiver->Send(
                &out
            );

            sentCount++;
        }

        std::cout
            << "[IN GAME CHAT] roomId="
            << roomId
            << " sender="
            << session->GetPlayerId()
            << " team="
            << static_cast<int>(
                senderMember.team
                )
            << " channel="
            << static_cast<int>(
                pkt->channel
                )
            << " targetCount="
            << sentCount
            << " message="
            << pkt->message
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_DRONE_DEPLOY:
    {
        auto* pkt = reinterpret_cast<CS_DroneDeploy*>(header);

        if (header->size != sizeof(CS_DroneDeploy))
        {
            std::cout << "[DRONE DEPLOY BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[DRONE DEPLOY BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (session->IsDead())
        {
            std::cout << "[DRONE DEPLOY BLOCK] dead playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        RoomMember member{};

        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        // 실제 이니시에이터 characterId로 바꿔야 함
        // 예: constexpr int INITIATOR_CHARACTER_ID = 0;
        // if (member.lockedCharacterId != INITIATOR_CHARACTER_ID) break;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);

            if (it == g_matchStates.end())
            {
                std::cout << "[DRONE DEPLOY BLOCK] match state not found. roomId="
                    << roomId
                    << std::endl;

                break;
            }

            if (it->second.buyPhase ||
                it->second.roundResolving ||
                it->second.matchEnded)
            {
                std::cout << "[DRONE DEPLOY BLOCK] invalid round state. playerId="
                    << session->GetPlayerId()
                    << " buyPhase=" << it->second.buyPhase
                    << " resolving=" << it->second.roundResolving
                    << " matchEnded=" << it->second.matchEnded
                    << std::endl;

                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            if (g_ownerDroneIds.find(session->GetPlayerId()) != g_ownerDroneIds.end())
            {
                std::cout << "[DRONE DEPLOY BLOCK] already has drone playerId="
                    << session->GetPlayerId()
                    << std::endl;
                break;
            }

            uint32_t droneId = g_nextDroneId++;

            ServerDroneState state{};
            state.droneId = droneId;
            state.ownerPlayerId = session->GetPlayerId();
            state.roomId = roomId;
            state.x = pkt->x;
            state.y = pkt->y;
            state.z = pkt->z;
            state.yaw = pkt->yaw;
            state.pitch = pkt->pitch;

            g_drones[droneId] = state;
            g_ownerDroneIds[session->GetPlayerId()] = droneId;

            SC_DroneSpawn out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_DRONE_SPAWN);
            out.droneId = droneId;
            out.ownerPlayerId = session->GetPlayerId();
            out.x = state.x;
            out.y = state.y;
            out.z = state.z;
            out.yaw = state.yaw;
            out.pitch = state.pitch;

            BroadcastPacketToRoomInternal(roomId, &out);

            std::cout << "[DRONE SPAWN] owner="
                << session->GetPlayerId()
                << " droneId=" << droneId
                << " roomId=" << roomId
                << std::endl;
        }

        break;
    }

    case PacketType::PKT_CS_DRONE_STATE:
    {
        auto* pkt = reinterpret_cast<CS_DroneState*>(header);

        if (header->size != sizeof(CS_DroneState))
            break;

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (session->IsDead())
        {
            DestroyDroneForPlayer(session->GetPlayerId(), 1);
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto droneIt = g_drones.find(pkt->droneId);
            if (droneIt == g_drones.end())
                break;

            ServerDroneState& drone = droneIt->second;

            if (drone.ownerPlayerId != session->GetPlayerId())
                break;

            if (drone.roomId != roomId)
                break;

            drone.x = pkt->x;
            drone.y = pkt->y;
            drone.z = pkt->z;
            drone.yaw = pkt->yaw;
            drone.pitch = pkt->pitch;

            SC_DroneState out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_DRONE_STATE);
            out.droneId = drone.droneId;
            out.ownerPlayerId = drone.ownerPlayerId;
            out.x = drone.x;
            out.y = drone.y;
            out.z = drone.z;
            out.yaw = drone.yaw;
            out.pitch = drone.pitch;

            BroadcastPacketToRoomInternal(roomId, &out);
        }

        break;
    }

    case PacketType::PKT_CS_DRONE_EXIT:
    {
        auto* pkt = reinterpret_cast<CS_DroneExit*>(header);

        if (header->size != sizeof(CS_DroneExit))
            break;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto ownerIt = g_ownerDroneIds.find(session->GetPlayerId());

            if (ownerIt == g_ownerDroneIds.end())
                break;

            if (ownerIt->second != pkt->droneId)
            {
                std::cout << "[DRONE EXIT BLOCK] wrong droneId. playerId="
                    << session->GetPlayerId()
                    << " requestDroneId=" << pkt->droneId
                    << " ownerDroneId=" << ownerIt->second
                    << std::endl;

                break;
            }
        }

        DestroyDroneForPlayer(session->GetPlayerId(), 0);

        break;
    }

    case PacketType::PKT_CS_DRONE_HIT:
    {
        auto* pkt = reinterpret_cast<CS_DroneHit*>(header);

        if (header->size != sizeof(CS_DroneHit))
        {
            std::cout
                << "[DRONE HIT BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[DRONE HIT BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[DRONE HIT BLOCK] attacker dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        uint32_t ownerPlayerId = 0;
        uint32_t droneRoomId = 0;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto droneIt = g_drones.find(pkt->droneId);
            if (droneIt == g_drones.end())
            {
                std::cout
                    << "[DRONE HIT BLOCK] drone not found droneId="
                    << pkt->droneId
                    << std::endl;

                break;
            }

            ownerPlayerId = droneIt->second.ownerPlayerId;
            droneRoomId = droneIt->second.roomId;
        }

        if (droneRoomId != roomId)
        {
            std::cout
                << "[DRONE HIT BLOCK] different room attacker="
                << session->GetPlayerId()
                << " droneId="
                << pkt->droneId
                << std::endl;

            break;
        }

        if (ownerPlayerId == session->GetPlayerId())
        {
            std::cout
                << "[DRONE HIT BLOCK] own drone attacker="
                << session->GetPlayerId()
                << " droneId="
                << pkt->droneId
                << std::endl;

            break;
        }

        RoomMember attackerMember{};
        RoomMember ownerMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            attackerMember))
        {
            break;
        }

        if (!RoomManager::Instance().GetMember(
            roomId,
            ownerPlayerId,
            ownerMember))
        {
            break;
        }

        if (attackerMember.team == ownerMember.team)
        {
            std::cout
                << "[DRONE HIT BLOCK] friendly drone fire blocked. attacker="
                << session->GetPlayerId()
                << " owner="
                << ownerPlayerId
                << std::endl;

            break;
        }

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
        {
            std::cout
                << "[DRONE HIT BLOCK] weapon not owned playerId="
                << session->GetPlayerId()
                << " weaponId="
                << weaponId
                << std::endl;

            break;
        }

        PlayerWeaponState* weaponState =
            session->GetWeaponState(weaponId);

        if (weaponState == nullptr)
            break;

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
            break;

        int damage = weaponData->damage;
        int remainHp = 0;
        bool destroyed = false;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto droneIt = g_drones.find(pkt->droneId);
            if (droneIt == g_drones.end())
                break;

            ServerDroneState& drone = droneIt->second;

            if (drone.roomId != roomId)
                break;

            if (drone.ownerPlayerId != ownerPlayerId)
                break;

            drone.hp -= damage;
            remainHp = drone.hp;

            if (drone.hp <= 0)
            {
                g_drones.erase(droneIt);
                g_ownerDroneIds.erase(ownerPlayerId);
                destroyed = true;
            }
        }

        std::cout
            << "[DRONE HIT] attacker="
            << session->GetPlayerId()
            << " droneId="
            << pkt->droneId
            << " owner="
            << ownerPlayerId
            << " damage="
            << damage
            << " remainHp="
            << remainHp
            << std::endl;

        if (destroyed)
        {
            BroadcastDroneDestroy(
                roomId,
                pkt->droneId,
                ownerPlayerId,
                1
            );

            std::cout
                << "[DRONE DESTROY BY HIT] droneId="
                << pkt->droneId
                << " owner="
                << ownerPlayerId
                << std::endl;
        }

        break;
    }

    case PacketType::PKT_CS_DRONE_TASER_FIRE:
    {
        auto* pkt = reinterpret_cast<CS_DroneTaserFire*>(header);

        if (header->size != sizeof(CS_DroneTaserFire))
        {
            std::cout
                << "[DRONE TASER BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[DRONE TASER BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[DRONE TASER BLOCK] owner dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        constexpr float DRONE_TASER_RANGE = 100.f;
        constexpr float DRONE_TASER_RANGE_SQ =
            DRONE_TASER_RANGE * DRONE_TASER_RANGE;

        constexpr float DRONE_TASER_DURATION = 3.f;
        constexpr int32_t DRONE_TASER_DAMAGE = 10;
        constexpr uint8_t DRONE_TASER_STACK = 1;

        ServerDroneState droneSnapshot{};

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto droneIt = g_drones.find(pkt->droneId);

            if (droneIt == g_drones.end())
            {
                std::cout
                    << "[DRONE TASER BLOCK] drone not found droneId="
                    << pkt->droneId
                    << std::endl;

                break;
            }

            ServerDroneState& drone = droneIt->second;

            if (drone.ownerPlayerId != session->GetPlayerId())
            {
                std::cout
                    << "[DRONE TASER BLOCK] not owner playerId="
                    << session->GetPlayerId()
                    << " droneOwner="
                    << drone.ownerPlayerId
                    << std::endl;

                break;
            }

            if (drone.roomId != roomId)
            {
                std::cout
                    << "[DRONE TASER BLOCK] room mismatch playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (drone.taserUsed)
            {
                std::cout
                    << "[DRONE TASER BLOCK] already used droneId="
                    << pkt->droneId
                    << std::endl;

                break;
            }

            // 핵심:
            // 맞췄든 못 맞췄든 여기서 1회 사용 확정.
            drone.taserUsed = true;
            droneSnapshot = drone;
        }

        bool validHit = false;
        uint32_t finalTargetPlayerId = 0;

        if (pkt->targetPlayerId != 0)
        {
            Session* target =
                SessionManager::Instance().Find(pkt->targetPlayerId);

            if (target != nullptr &&
                target->GetCurrentRoomId() == roomId &&
                !target->IsDead())
            {
                RoomMember ownerMember{};
                RoomMember targetMember{};

                bool ownerOk =
                    RoomManager::Instance().GetMember(
                        roomId,
                        session->GetPlayerId(),
                        ownerMember
                    );

                bool targetOk =
                    RoomManager::Instance().GetMember(
                        roomId,
                        target->GetPlayerId(),
                        targetMember
                    );

                if (ownerOk &&
                    targetOk &&
                    ownerMember.team != targetMember.team)
                {
                    Vector3 targetPos = target->GetPosition();

                    float dx = targetPos.x - droneSnapshot.x;
                    float dy = targetPos.y - droneSnapshot.y;
                    float dz = targetPos.z - droneSnapshot.z;

                    float distSq =
                        dx * dx +
                        dy * dy +
                        dz * dz;

                    if (distSq <= DRONE_TASER_RANGE_SQ)
                    {
                        validHit = true;
                        finalTargetPlayerId = target->GetPlayerId();

                        target->SetLastDamager(
                            session->GetPlayerId()
                        );

                        target->AddAssistDamageRecord(
                            session->GetPlayerId(),
                            DRONE_TASER_DAMAGE
                        );

                        target->Damage(
                            DRONE_TASER_DAMAGE
                        );

                        SC_HPUpdate hpPkt{};
                        FillHPUpdatePacket(
                            hpPkt,
                            target
                        );

                        BroadcastPacketToRoomInternal(
                            roomId,
                            &hpPkt
                        );

                        std::cout
                            << "[DRONE TASER HIT] owner="
                            << session->GetPlayerId()
                            << " target="
                            << target->GetPlayerId()
                            << " damage="
                            << DRONE_TASER_DAMAGE
                            << " targetHp="
                            << target->GetHP()
                            << std::endl;

                        if (target->IsDead())
                        {
                            HandleDeath(target);
                        }
                    }
                }
            }
        }

        BroadcastDroneTaserFire(
            roomId,
            pkt->droneId,
            session->GetPlayerId(),
            finalTargetPlayerId,
            validHit,
            validHit ? DRONE_TASER_STACK : 0,
            DRONE_TASER_DURATION,
            validHit ? DRONE_TASER_DAMAGE : 0
        );

        if (!validHit)
        {
            std::cout
                << "[DRONE TASER MISS/BLOCKED] owner="
                << session->GetPlayerId()
                << " droneId="
                << pkt->droneId
                << " requestedTarget="
                << pkt->targetPlayerId
                << std::endl;
        }

        break;
    }

    default:
    {
        std::cout << "[UNHANDLED PACKET] type=" << (uint16_t)type
            << " session=" << session
            << std::endl;
        break;
    }
    }
}

void AddKillStatByRound(Session* player, uint32_t roomId)
{
    if (player == nullptr)
        return;

    PlayerMatchStats& stats = player->GetMatchStats();

    stats.kills++;

    uint8_t currentRound = 1;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);
        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
            currentRound = it->second.currentRound;
    }

    if (IsFirstHalfRound(currentRound))
        stats.firstHalfKills++;
    else if (IsSecondHalfRound(currentRound))
        stats.secondHalfKills++;
    else
        stats.overtimeKills++;
}

void AddDeathStatByRound(Session* player, uint32_t roomId)
{
    if (player == nullptr)
        return;

    PlayerMatchStats& stats = player->GetMatchStats();

    stats.deaths++;

    uint8_t currentRound = 1;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);
        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
            currentRound = it->second.currentRound;
    }

    if (IsFirstHalfRound(currentRound))
        stats.firstHalfDeaths++;
    else if (IsSecondHalfRound(currentRound))
        stats.secondHalfDeaths++;
    else
        stats.overtimeDeaths++;
}

void AddAssistStatByRound(Session* player, uint32_t roomId)
{
    if (player == nullptr)
        return;

    PlayerMatchStats& stats = player->GetMatchStats();

    stats.assists++;

    uint8_t currentRound = 1;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);
        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
            currentRound = it->second.currentRound;
    }

    if (IsFirstHalfRound(currentRound))
        stats.firstHalfAssists++;
    else if (IsSecondHalfRound(currentRound))
        stats.secondHalfAssists++;
    else
        stats.overtimeAssists++;
}

void PacketDispatcher::HandleDeath(Session* target)
{
    if (target == nullptr)
        return;

    uint32_t roomId = target->GetCurrentRoomId();

    DestroyDroneForPlayer(target->GetPlayerId(), 1);

    constexpr int ASSIST_VALID_SECONDS = 10;

    uint32_t killerId = target->GetLastDamager();
    Session* killer = SessionManager::Instance().Find(killerId);

    SC_Die diePkt{};
    diePkt.header.size = sizeof(diePkt);
    diePkt.header.type = static_cast<uint16_t>(PacketType::PKT_SC_DIE);

    diePkt.killerPlayerId = 0;
    diePkt.deadPlayerId = target->GetPlayerId();

    diePkt.killerTeam = 255;
    diePkt.deadTeam = 255;

    diePkt.killerCharacterId = -1;
    diePkt.deadCharacterId = -1;

    memset(diePkt.killerNickname, 0, sizeof(diePkt.killerNickname));
    memset(diePkt.deadNickname, 0, sizeof(diePkt.deadNickname));

    RoomMember deadMember{};

    if (RoomManager::Instance().GetMember(roomId, target->GetPlayerId(), deadMember))
    {
        diePkt.deadTeam =
            static_cast<uint8_t>(deadMember.team);

        diePkt.deadCharacterId =
            deadMember.lockedCharacterId;

        strncpy_s(
            diePkt.deadNickname,
            sizeof(diePkt.deadNickname),
            deadMember.nickname.c_str(),
            _TRUNCATE
        );
    }

    // 킬러가 정상적으로 존재할 때만 킬러 정보 입력
    if (killer != nullptr && killer != target)
    {
        diePkt.killerPlayerId =
            killer->GetPlayerId();

        RoomMember killerMember{};

        if (RoomManager::Instance().GetMember(
            roomId,
            killer->GetPlayerId(),
            killerMember))
        {
            diePkt.killerTeam =
                static_cast<uint8_t>(killerMember.team);

            diePkt.killerCharacterId =
                killerMember.lockedCharacterId;

            strncpy_s(
                diePkt.killerNickname,
                sizeof(diePkt.killerNickname),
                killerMember.nickname.c_str(),
                _TRUNCATE
            );
        }
    }

    if (killer != nullptr && killer != target)
    {
        AddKillStatByRound(killer, roomId);
        killer->AddCredits(GetCreditReward("KILL"));

        AddDeathStatByRound(target, roomId);

        std::cout << "[CREDIT KILL] killer="
            << killer->GetPlayerId()
            << " add=" << GetCreditReward("KILL")
            << " total=" << killer->GetCredits()
            << std::endl;

        std::vector<uint32_t> assistPlayerIds =
            target->GetAssistPlayerIds(killerId, ASSIST_VALID_SECONDS);

        for (uint32_t assistPlayerId : assistPlayerIds)
        {
            Session* assister = SessionManager::Instance().Find(assistPlayerId);

            if (assister == nullptr)
                continue;

            if (assister == killer)
                continue;

            if (assister == target)
                continue;

            AddAssistStatByRound(assister, roomId);

            std::cout << "[ASSIST UPDATE] assister="
                << assister->GetPlayerId()
                << " target=" << target->GetPlayerId()
                << std::endl;
        }

        std::cout << "[KDA UPDATE] killer="
            << killer->GetPlayerId()
            << " target=" << target->GetPlayerId()
            << std::endl;
    }
    else
    {
        AddDeathStatByRound(target, roomId);
    }

    target->ClearAssistDamageRecords();

    std::vector<PlayerWeaponState> ownedWeapons =
        target->GetOwnedWeapons();

    Vector3 deathPos = target->GetPosition();

    int dropOffset = 0;

    for (const PlayerWeaponState& weapon : ownedWeapons)
    {
        CreateDroppedWeapon(
            roomId,
            target->GetPlayerId(),
            weapon.weaponId,
            weapon.magAmmo,
            weapon.reserveAmmo,
            deathPos.x + 0.3f * dropOffset,
            deathPos.y + 0.5f,
            deathPos.z,
            0.f,
            0.f,
            1.f
        );

        dropOffset++;
    }

    for (const PlayerWeaponState& weapon : ownedWeapons)
    {
        target->RemoveWeapon(weapon.weaponId);
    }

    bool shouldDropSpike = false;
    Vector3 dropPos{};

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
        {
            MatchRuntimeState& state = it->second;

            if (!state.spikePlanted &&
                state.spikeCarrierPlayerId == target->GetPlayerId())
            {
                dropPos = target->GetPosition();
                state.spikeCarrierPlayerId = 0;
                shouldDropSpike = true;
            }
        }
    }

    if (shouldDropSpike)
    {
        BroadcastSpikeDropped(
            roomId,
            dropPos.x,
            dropPos.y,
            dropPos.z,
            0.0f,
            0.2f,
            0.0f,
            target->GetPlayerId()
        );

        std::cout << "[SPIKE DROP BY DEATH] playerId="
            << target->GetPlayerId()
            << " roomId=" << roomId
            << std::endl;
    }

    BroadcastPacketToRoomInternal(roomId, &diePkt);

    SC_HPUpdate hpPkt{};
    FillHPUpdatePacket(hpPkt, target);
    BroadcastPacketToRoomInternal(roomId, &hpPkt);

    std::cout << "[PLAYER DEAD] playerId="
        << target->GetPlayerId()
        << " roomId=" << roomId
        << " hp=" << target->GetHP()
        << std::endl;

    bool shouldCancelDefuse = false;
    bool halfDefused = false;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
        {
            MatchRuntimeState& state = it->second;

            if (state.spikeDefuserPlayerId == target->GetPlayerId())
            {
                state.spikeDefuserPlayerId = 0;
                halfDefused = state.spikeHalfDefused;
                shouldCancelDefuse = true;
            }
        }
    }

    if (shouldCancelDefuse)
    {
        BroadcastSpikeDefuseState(
            roomId,
            0,
            halfDefused,
            0
        );
    }

    BroadcastScoreboardUpdate(roomId);

    bool spikePlanted = false;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
        {
            MatchRuntimeState& state = it->second;

            spikePlanted = state.spikePlanted;

            std::cout << "[SPIKE DEATH CHECK] deadPlayerId="
                << target->GetPlayerId()
                << " carrier=" << state.spikeCarrierPlayerId
                << " planted=" << state.spikePlanted
                << std::endl;
        }
    }

    TryResolveRoundByElimination(roomId);
}