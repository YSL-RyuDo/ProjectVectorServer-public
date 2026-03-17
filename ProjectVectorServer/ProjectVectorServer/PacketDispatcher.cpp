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
            break; // 허용

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
            break;
        }
        case PacketType::PKT_CS_LOGIN:
        {
            auto* pkt = reinterpret_cast<CS_Login*>(header);

            uint8_t result = (uint8_t)UserManager::Instance().Login(
                pkt->id,
                pkt->password
            );

            SC_LoginResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_LOGIN_RESULT;
            out.result = result;

            session->Send(&out);

            // 로그인 성공 처리 (핵심)
            if (result == (uint8_t)LoginResult::SUCCESS)
            {
                session->SetAuthenticated(true);

                std::cout << "[AUTH SUCCESS] playerId="
                    << session->GetPlayerId() << std::endl;

                // 게임 입장 처리
                uint32_t myId = session->GetPlayerId();

                // HELLO
                session->SendHello(myId);

                // 기존 플레이어들 정보 보내기
                for (Session* other : SessionManager::Instance().GetAll())
                {
                    if (other == session)
                        continue;

                    session->SendSpawn(
                        other->GetPlayerId(),
                        other->GetPosition().x,
                        other->GetPosition().y,
                        other->GetPosition().z
                    );
                }

                // 나를 다른 사람들에게 스폰
                SC_SpawnPlayer spawn{};
                spawn.header.size = sizeof(spawn);
                spawn.header.type = (uint16_t)PacketType::PKT_SC_SPAWN_PLAYER;
                spawn.playerId = myId;
                spawn.x = session->GetPosition().x;
                spawn.y = session->GetPosition().y;
                spawn.z = session->GetPosition().z;
                spawn.hp = session->GetHP();

                SessionManager::Instance().Broadcast(&spawn);
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