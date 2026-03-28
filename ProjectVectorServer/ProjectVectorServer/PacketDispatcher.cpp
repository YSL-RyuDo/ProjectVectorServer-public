#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"

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