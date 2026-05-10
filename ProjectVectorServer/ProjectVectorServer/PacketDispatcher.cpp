#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "RoomManager.h"

#include <cstring>
#include <windows.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <algorithm>
#include <vector>

namespace
{
    constexpr uint16_t PICK_TIME_SECONDS = 20;
    constexpr uint16_t PREPARE_TIME_SECONDS = 10;

    void BroadcastMatchLoadingInfo(const Room& room);
    void BroadcastMatchStart(const Room& room);

    void BroadcastPickTimer(const Room& room,PickPhase phase,uint16_t remainingSeconds)
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

    void FillRoomCreateResultPacket(
        SC_RoomCreateResult& out,
        const Room& room,
        uint8_t result,
        uint32_t receiverPlayerId)
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

    bool TakePendingSlotSwap(
        uint32_t targetPlayerId,
        uint32_t requesterPlayerId,
        PendingSlotSwap& outRequest)
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

    void SendRoomSlotChangeResult(
        Session* session,
        RoomSlotChangeResult result,
        RoomTeam targetTeam,
        uint8_t targetSlotIndex,
        const std::string& targetNickname = "")
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

    void SendRoomTransferLeaderResult(
        Session* session,
        RoomAdminResult result,
        uint32_t targetPlayerId,
        const std::string& targetNickname = "")
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

    void SendRoomKickResult(
        Session* session,
        RoomAdminResult result,
        uint32_t targetPlayerId,
        const std::string& targetNickname = "")
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

    void FillPickTeamMemberInfo(
        PickTeamMemberInfo& out,
        const RoomMember& member,
        uint32_t receiverPlayerId)
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

    void FillPickStatePacket(
        SC_PickState& out,
        const Room& room,
        const RoomMember& receiverMember,
        uint32_t receiverPlayerId)
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

    void SendPickLockResult(
        Session* session,
        PickLockResult result,
        int characterId)
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

                    BroadcastPickTimer(
                        updatedRoom,
                        PickPhase::PICKING,
                        static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                // 2) 자동 픽 확정
                Room finalPickRoom{};
                bool forceOk = RoomManager::Instance().ForceLockMissingPicks(
                    roomId,
                    finalPickRoom
                );

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

                // 5) 약간의 연출 시간 주고 게임 시작 패킷
                std::this_thread::sleep_for(std::chrono::seconds(2));

                Room startRoom{};
                if (!RoomManager::Instance().GetRoom(roomId, startRoom))
                    return;

                BroadcastMatchStart(startRoom);

                std::cout << "[MATCH FLOW COMPLETE] roomId="
                    << roomId
                    << std::endl;
            }).detach();
    }

    void FillMatchLoadingMemberInfo(
        MatchLoadingMemberInfo& out,
        const RoomMember& member)
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

        if (session->GetState() != SessionState::IN_ROOM || session->GetCurrentRoomId() == 0)
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

    case PacketType::PKT_CS_MOVE_STATE:
    {
        auto* pkt = reinterpret_cast<CS_MoveState*>(header);

        session->SetPosition(pkt->x, pkt->y, pkt->z);
        session->SetYaw(pkt->yaw);

        SC_MoveState out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_MOVE_STATE;

        out.playerId = session->GetPlayerId();
        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;
        out.yaw = pkt->yaw;

        SessionManager::Instance().Broadcast(&out);

        break;
    }

    case PacketType::PKT_CS_HIT:
    {
        auto* pkt = reinterpret_cast<CS_Hit*>(header);

        Session* target = SessionManager::Instance().Find(pkt->targetId);
        if (!target)
            break;

        target->Damage(pkt->damage);

        SC_HPUpdate hpPkt{};
        hpPkt.header.size = sizeof(hpPkt);
        hpPkt.header.type = (uint16_t)PacketType::PKT_SC_HP_UPDATE;
        hpPkt.playerId = target->GetPlayerId();
        hpPkt.hp = target->GetHP();

        SessionManager::Instance().Broadcast(&hpPkt);

        if (target->IsDead())
        {
            HandleDeath(target);
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

void PacketDispatcher::HandleDeath(Session* target)
{
    SC_Die diePkt{};
    diePkt.header.size = sizeof(diePkt);
    diePkt.header.type = (uint16_t)PacketType::PKT_SC_DIE;
    diePkt.playerId = target->GetPlayerId();

    SessionManager::Instance().Broadcast(&diePkt);

    target->Respawn();

    SC_SpawnPlayer spawnPkt{};
    spawnPkt.header.size = sizeof(spawnPkt);
    spawnPkt.header.type = (uint16_t)PacketType::PKT_SC_SPAWN_PLAYER;
    spawnPkt.playerId = target->GetPlayerId();
    spawnPkt.x = target->GetPosition().x;
    spawnPkt.y = target->GetPosition().y;
    spawnPkt.z = target->GetPosition().z;
    spawnPkt.hp = target->GetHP();

    SessionManager::Instance().Broadcast(&spawnPkt);
}