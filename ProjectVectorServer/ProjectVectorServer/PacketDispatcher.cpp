#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "RoomManager.h"
#include <cstring>
#include <windows.h>

namespace
{
    void FillRoomMemberInfo(RoomMemberInfo& out, const RoomMember& member)
    {
        out.playerId = member.playerId;

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
}

void WriteUTF8(char* dest, int size, const wchar_t* src)
{
    memset(dest, 0, size);

    int len = WideCharToMultiByte(
        CP_UTF8,
        0,
        src,
        -1,
        dest,
        size - 1,
        NULL,
        NULL
    );

    if (len <= 0)
        return;

    dest[len - 1] = '\0'; 
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

void PacketDispatcher::Dispatch(Session* session, PacketHeader* header)
{
    PacketType type = (PacketType)header->type;

    std::cout << "[DISPATCH BEGIN] session=" << session
        << " playerId=" << session->GetPlayerId()
        << " userId=" << session->GetUserId()
        << " type=" << (uint16_t)type
        << " size=" << header->size
        << std::endl;

    // 인증 필터
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

        std::cout << "[REGISTER RESULT] id=" << pkt->id
            << " result=" << (int)result
            << std::endl;

        SC_RegisterResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_REGISTER_RESULT;
        out.result = result;

        session->Send(&out);

        std::cout << "[REGISTER RESULT SENT] session=" << session
            << " result=" << (int)result
            << std::endl;
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

            std::cout << "[LOGIN SKIP ALREADY AUTH] id=" << pkt->id
                << " playerId=" << session->GetPlayerId()
                << std::endl;
            break;
        }

        uint8_t result = (uint8_t)UserManager::Instance().Login(
            pkt->id,
            pkt->password
        );

        std::cout << "[LOGIN RESULT CHECK] id=" << pkt->id
            << " result=" << (int)result
            << std::endl;

        if (result == (uint8_t)LoginResult::SUCCESS)
        {
            session->SetAuthenticated(true);
            session->SetState(SessionState::AUTHENTICATED);
            UserManager::Instance().OnLogin(pkt->id, session);

            uint32_t newPlayerId = SessionManager::Instance().AllocatePlayerId();
            session->SetPlayerId(newPlayerId);
            SessionManager::Instance().Add(session);

            std::cout << "[LOGIN SUCCESS READY] id=" << pkt->id
                << " playerId=" << newPlayerId
                << " session=" << session
                << std::endl;
        }
        else
        {
            std::cout << "[LOGIN FAIL] id=" << pkt->id
                << " result=" << (int)result
                << std::endl;
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

        if (result == (uint8_t)LoginResult::SUCCESS)
        {
            std::cout << "[LOGIN SUCCESS SENT] id=" << pkt->id
                << " playerId=" << session->GetPlayerId()
                << std::endl;
        }

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
            << " level=" << data.level
            << " currentExp=" << data.currentExp
            << " requiredExp=" << out.requiredExp
            << " icon=" << data.playerIconId
            << " char=" << data.lobbyCharacterModelId
            << " bg=" << data.lobbyBackgroundId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_LOBBY_CHARACTER_SELECT:
    {
        auto* pkt = reinterpret_cast<CS_LobbyCharacterSelect*>(header);

        std::cout << "[LOBBY CHARACTER SELECT REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " modelId=" << pkt->lobbyCharacterModelId
            << std::endl;

        bool owned = UserManager::Instance().IsCharacterOwned(
            session->GetUserId(),
            pkt->lobbyCharacterModelId
        );

        std::cout << "[LOBBY CHARACTER OWN CHECK] userId=" << session->GetUserId()
            << " modelId=" << pkt->lobbyCharacterModelId
            << " owned=" << owned
            << std::endl;

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

        std::cout << "[LOBBY CHARACTER SELECT RESULT] playerId=" << session->GetPlayerId()
            << " modelId=" << pkt->lobbyCharacterModelId
            << " result=" << (int)out.result
            << std::endl;
        break;
    }

    case PacketType::PKT_CS_LOBBY_BACKGROUND_SELECT:
    {
        auto* pkt = reinterpret_cast<CS_LobbyBackgroundSelect*>(header);

        std::cout << "[LOBBY BACKGROUND SELECT REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " backgroundId=" << pkt->lobbyBackgroundId
            << std::endl;

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

        std::cout << "[LOBBY BACKGROUND SELECT RESULT] playerId=" << session->GetPlayerId()
            << " backgroundId=" << pkt->lobbyBackgroundId
            << " result=" << (int)out.result
            << std::endl;
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

        std::cout << "[LOBBY CHAT REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " message=" << pkt->message
            << std::endl;

        PlayerData data{};
        bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);
        if (!ok)
        {
            std::cout << "[LOBBY CHAT FAIL] playerData not found userId="
                << session->GetUserId()
                << std::endl;
            break;
        }

        SC_LobbyChat out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_CHAT;
        out.playerId = session->GetPlayerId();

        strncpy_s(out.nickname, data.nickname.c_str(), _TRUNCATE);
        strncpy_s(out.message, pkt->message, _TRUNCATE);

        SessionManager::Instance().BroadcastLobby(&out);

        std::cout << "[LOBBY CHAT BROADCAST] playerId=" << session->GetPlayerId()
            << " nickname=" << data.nickname
            << " message=" << out.message
            << std::endl;
        break;
    }

    case PacketType::PKT_CS_LOBBY_CHARACTER_LIST:
    {
        std::cout << "[LOBBY CHARACTER LIST REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << std::endl;

        CharacterOwnedData ownedData{};
        PlayerData playerData{};

        bool okOwned = UserManager::Instance().GetCharacterOwnedData(session->GetUserId(), ownedData);
        bool okPlayer = UserManager::Instance().GetPlayerData(session->GetUserId(), playerData);
        if (!okOwned || !okPlayer)
        {
            std::cout << "[LOBBY CHARACTER LIST FAIL] userId=" << session->GetUserId()
                << " okOwned=" << okOwned
                << " okPlayer=" << okPlayer
                << std::endl;
            break;
        }

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

        std::cout << "[LOBBY CHARACTER LIST SENT] playerId=" << session->GetPlayerId()
            << " currentChar=" << out.currentCharacterModelId
            << " count=" << out.count
            << " owned=("
            << (int)out.characters[0].owned << ","
            << (int)out.characters[1].owned << ","
            << (int)out.characters[2].owned << ","
            << (int)out.characters[3].owned << ")"
            << std::endl;
        break;
    }

    //추후에 유니티랑 비교할것
    case PacketType::PKT_CS_TEMP_GAME_ROOM_ENTER:
    {
        std::cout << "[TEMP GAME ROOM ENTER REQUEST] playerId="
            << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << std::endl;

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

            std::cout << "[TEMP GAME ROOM ENTER FAIL] userId="
                << session->GetUserId()
                << std::endl;
            break;
        }

        out.result = 0;
        out.playerCount = 1;

        out.players[0].slotIndex = 0;
        strncpy_s(out.players[0].nickname, data.nickname.c_str(), _TRUNCATE);
        out.players[0].playerIconId = data.playerIconId;
        out.players[0].logoBorderId = data.logoBorderId;
        out.players[0].rankImageId = data.rankImageId;

        // 1,2는 비워둠
        out.players[1].slotIndex = 1;
        out.players[2].slotIndex = 2;

        session->Send(&out);

        std::cout << "[TEMP GAME ROOM ENTER SUCCESS] playerId="
            << session->GetPlayerId()
            << " nickname=" << out.players[0].nickname
            << " iconId=" << out.players[0].playerIconId
            << " borderId=" << out.players[0].logoBorderId
            << " rankId=" << out.players[0].rankImageId
            << std::endl;
        break;
    }

    case PacketType::PKT_CS_TEMP_GAME_START:
    {
        std::cout << "[TEMP GAME START REQUEST] playerId="
            << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << std::endl;

        SC_TempGameStartResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_TEMP_GAME_START_RESULT;
        out.result = 0; // 임시로 무조건 성공

        session->Send(&out);

        std::cout << "[TEMP GAME START RESULT] playerId="
            << session->GetPlayerId()
            << " result=" << (int)out.result
            << std::endl;
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

            std::cout << "[ROOM CREATE FAIL] already in room playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (session->GetState() != SessionState::IN_LOBBY)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::NOT_IN_LOBBY;

            session->Send(&out);

            std::cout << "[ROOM CREATE FAIL] not in lobby playerId="
                << session->GetPlayerId()
                << std::endl;
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

            std::cout << "[ROOM CREATE FAIL] player data not found userId="
                << session->GetUserId()
                << std::endl;
            break;
        }

        char roomNameBuffer[ROOM_NAME_LEN]{};
        std::string roomName = data.nickname + WideToUTF8(L"의 방");
        std::string mapName = WideToUTF8(L"기본 맵");

        RoomMember leader{};
        leader.playerId = session->GetPlayerId();
        leader.userId = session->GetUserId();
        leader.nickname = data.nickname;
        leader.level = data.level;
        leader.playerIconId = data.playerIconId;
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

            std::cout << "[ROOM CREATE FAIL] create failed playerId="
                << session->GetPlayerId()
                << std::endl;
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

            std::cout << "[ROOM LEAVE FAIL] not in room playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        bool roomDeleted = false;
        bool ok = RoomManager::Instance().LeaveRoom(
            roomId,
            session->GetPlayerId(),
            roomDeleted
        );

        if (!ok)
        {
            out.result = (uint8_t)RoomLeaveResult::ROOM_NOT_FOUND;
            session->Send(&out);

            std::cout << "[ROOM LEAVE FAIL] room not found roomId="
                << roomId
                << " playerId=" << session->GetPlayerId()
                << std::endl;
            break;
        }

        session->SetState(SessionState::IN_LOBBY);
        session->SetCurrentRoomId(0);

        out.result = (uint8_t)RoomLeaveResult::SUCCESS;
        out.roomDeleted = roomDeleted ? 1 : 0;

        session->Send(&out);

        std::cout << "[ROOM LEAVE SUCCESS] playerId=" << session->GetPlayerId()
            << " roomId=" << roomId
            << " roomDeleted=" << roomDeleted
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_MOVE_STATE:
    {
        auto* pkt = reinterpret_cast<CS_MoveState*>(header);

        std::cout << "[MOVE REQUEST] playerId=" << session->GetPlayerId()
            << " pos=(" << pkt->x << "," << pkt->y << "," << pkt->z << ")"
            << " yaw=" << pkt->yaw
            << std::endl;

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

        std::cout << "[MOVE BROADCAST] playerId=" << out.playerId
            << " pos=(" << out.x << "," << out.y << "," << out.z << ")"
            << " yaw=" << out.yaw
            << std::endl;
        break;
    }

    case PacketType::PKT_CS_HIT:
    {
        auto* pkt = reinterpret_cast<CS_Hit*>(header);

        std::cout << "[HIT REQUEST] attacker=" << session->GetPlayerId()
            << " target=" << pkt->targetId
            << " damage=" << pkt->damage
            << std::endl;

        Session* target = SessionManager::Instance().Find(pkt->targetId);
        if (!target)
        {
            std::cout << "[HIT FAIL] target not found targetId="
                << pkt->targetId
                << std::endl;
            break;
        }

        target->Damage(pkt->damage);

        std::cout << "[HIT APPLIED] target=" << target->GetPlayerId()
            << " hp=" << target->GetHP()
            << std::endl;

        SC_HPUpdate hpPkt{};
        hpPkt.header.size = sizeof(hpPkt);
        hpPkt.header.type = (uint16_t)PacketType::PKT_SC_HP_UPDATE;
        hpPkt.playerId = target->GetPlayerId();
        hpPkt.hp = target->GetHP();

        SessionManager::Instance().Broadcast(&hpPkt);

        std::cout << "[HP UPDATE BROADCAST] target=" << hpPkt.playerId
            << " hp=" << hpPkt.hp
            << std::endl;

        if (target->IsDead())
        {
            std::cout << "[TARGET DEAD] playerId=" << target->GetPlayerId()
                << std::endl;
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
    std::cout << "[HANDLE DEATH BEGIN] playerId=" << target->GetPlayerId()
        << " hp=" << target->GetHP()
        << std::endl;

    SC_Die diePkt{};
    diePkt.header.size = sizeof(diePkt);
    diePkt.header.type = (uint16_t)PacketType::PKT_SC_DIE;
    diePkt.playerId = target->GetPlayerId();

    SessionManager::Instance().Broadcast(&diePkt);

    std::cout << "[DIE BROADCAST] playerId=" << diePkt.playerId
        << std::endl;

    target->Respawn();

    std::cout << "[RESPAWN DONE] playerId=" << target->GetPlayerId()
        << " pos=(" << target->GetPosition().x << ","
        << target->GetPosition().y << ","
        << target->GetPosition().z << ")"
        << " hp=" << target->GetHP()
        << std::endl;

    SC_SpawnPlayer spawnPkt{};
    spawnPkt.header.size = sizeof(spawnPkt);
    spawnPkt.header.type = (uint16_t)PacketType::PKT_SC_SPAWN_PLAYER;
    spawnPkt.playerId = target->GetPlayerId();
    spawnPkt.x = target->GetPosition().x;
    spawnPkt.y = target->GetPosition().y;
    spawnPkt.z = target->GetPosition().z;
    spawnPkt.hp = target->GetHP();

    SessionManager::Instance().Broadcast(&spawnPkt);

    std::cout << "[RESPAWN BROADCAST] playerId=" << spawnPkt.playerId
        << " pos=(" << spawnPkt.x << "," << spawnPkt.y << "," << spawnPkt.z << ")"
        << " hp=" << spawnPkt.hp
        << std::endl;
}