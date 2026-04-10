#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "RoomManager.h"
#include <cstring>

void PacketDispatcher::Dispatch(Session* session, PacketHeader* header)
{
    PacketType type = (PacketType)header->type;

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
                std::cout << "[AUTH BLOCK] playerId="
                    << session->GetPlayerId() << std::endl;
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
                << " nick=" << pkt->nickname << std::endl;

            uint8_t result = (uint8_t)UserManager::Instance().Register(
                pkt->id,
                pkt->nickname,
                pkt->password,
                pkt->confirmPassword
            );
            std::cout << "[REGISTER RESULT] " << (int)result << std::endl;

            SC_RegisterResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_REGISTER_RESULT;
            out.result = result;

            session->Send(&out);
            break;
        }
        case PacketType::PKT_CS_LOGIN:
        {
            auto* pkt = reinterpret_cast<CS_Login*>(header);

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

			SC_LoginResult out{};
			out.header.size = sizeof(out);
			out.header.type = (uint16_t)PacketType::PKT_SC_LOGIN_RESULT;
			out.result = result;
            
			session->Send(&out);

            if (result == (uint8_t)LoginResult::SUCCESS)
            {
                session->SetAuthenticated(true);
                UserManager::Instance().OnLogin(pkt->id, session);

                std::cout << "[LOGIN SUCCESS] id=" << pkt->id << std::endl;
            }

            break;
        }
        case PacketType::PKT_CS_LOBBY_ENTER:
        {
            PlayerData data{};
            bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);
            if (!ok)
                break;

            SC_LobbyEnter out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_ENTER;

            strncpy_s(out.nickname, data.nickname.c_str(), _TRUNCATE);
            out.level = data.level;
            out.lobbyCharacterModelId = data.lobbyCharacterModelId;
			out.playerIconId = data.playerIconId;

            session->Send(&out);
            break;
        }
        case PacketType::PKT_CS_LOBBY_CHARACTER_SELECT:
        {
            auto* pkt = reinterpret_cast<CS_LobbyCharacterSelect*>(header);

            bool ok = UserManager::Instance().SetLobbyCharacterModel(
                session->GetUserId(),
                pkt->lobbyCharacterModelId
            );

            SC_LobbyCharacterSelectResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_CHARACTER_SELECT_RESULT;
            out.result = ok ? 1 : 0;
            out.lobbyCharacterModelId = pkt->lobbyCharacterModelId;

            session->Send(&out);
            break;
        }
        case PacketType::PKT_CS_LOBBY_CHAT:
        {
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

            SessionManager::Instance().Broadcast(&out);
            break;
        }
        case PacketType::PKT_CS_ROOM_LIST:
        {
            SC_RoomList out{};
            out.header.size = sizeof(out);
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

                strncpy_s(info.roomName, ROOM_NAME_LEN, room.roomName.c_str(), _TRUNCATE);
                strncpy_s(info.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);
                strncpy_s(info.hostName, HOST_NAME_LEN, room.hostName.c_str(), _TRUNCATE);

                info.currentPlayerCount = room.currentPlayerCount;
                info.maxPlayerCount = room.maxPlayerCount;
                info.state = (uint8_t)room.state;
            }

            session->Send(&out);
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

            Session* target =
                SessionManager::Instance().Find(pkt->targetId);

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
                HandleDeath(target);

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