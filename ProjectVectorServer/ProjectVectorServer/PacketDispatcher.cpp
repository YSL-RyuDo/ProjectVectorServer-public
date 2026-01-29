#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "Session.h"
#include "SessionManager.h"

void PacketDispatcher::Dispatch(Session* session, PacketHeader* header)
{
    switch ((PacketType)header->type)
    {
        case PacketType::PKT_CS_MOVE_STATE:
        {
            auto* pkt = reinterpret_cast<CS_MoveState*>(header);

            session->SetPosition(pkt->x, pkt->y, pkt->z);
            session->SetYaw(pkt->yaw);

            SC_MoveState out{};
            out.header.size = sizeof(SC_MoveState);
            out.header.type =
                (uint16_t)PacketType::PKT_SC_MOVE_STATE;

            out.playerId = session->GetPlayerId();
            out.x = pkt->x;
            out.y = pkt->y;
            out.z = pkt->z;
            out.yaw = pkt->yaw;

            for (Session* other : SessionManager::Instance().GetAll())
            {
                if (other == session)
                    continue;

                other->Send(&out);
            }
            break;
        }
        case PacketType::PKT_CS_HIT:
        {
            auto* pkt = reinterpret_cast<CS_Hit*>(header);

            std::cout << "[HIT] 패킷 수신"
                << " attacker=" << session->GetPlayerId()
                << " target=" << pkt->targetId
                << " damage=" << pkt->damage
                << std::endl;

            Session* target =
                SessionManager::Instance().Find(pkt->targetId);

            if (!target)
            {
                std::cout << "[HIT][ERROR] 타겟 세션 못 찾음 targetId="
                    << pkt->targetId << std::endl;
                break;
            }

            std::cout << "[HIT] 타겟 찾음 playerId="
                << target->GetPlayerId()
                << " 기존 HP=" << target->GetHP()
                << std::endl;

            // 1. 데미지 적용
            target->Damage(pkt->damage);

            std::cout << "[HIT] 데미지 적용 후 HP="
                << target->GetHP()
                << std::endl;

            // 2. HP 업데이트 패킷
            SC_HPUpdate hpPkt{};
            hpPkt.header.size = sizeof(hpPkt);
            hpPkt.header.type = (uint16_t)PacketType::PKT_SC_HP_UPDATE;
            hpPkt.playerId = target->GetPlayerId();
            hpPkt.hp = target->GetHP();

            SessionManager::Instance().Broadcast(&hpPkt);

            std::cout << "[HIT] HP_UPDATE 브로드캐스트 완료"
                << std::endl;

            // 3. 사망 체크
            if (target->IsDead())
            {
                std::cout << "[HIT] 사망 조건 만족 → HandleDeath 호출"
                    << std::endl;

                HandleDeath(target);
            }
            else
            {
                std::cout << "[HIT] 아직 생존 HP="
                    << target->GetHP()
                    << std::endl;
            }

            break;
        }


    default:
        break;
    }


}

void PacketDispatcher::HandleDeath(Session* target)
{
    std::cout << "[DEATH] HandleDeath 진입 playerId="
        << target->GetPlayerId()
        << std::endl;

    // 1. DIE 패킷
    SC_Die diePkt{};
    diePkt.header.size = sizeof(diePkt);
    diePkt.header.type = (uint16_t)PacketType::PKT_SC_DIE;
    diePkt.playerId = target->GetPlayerId();

    SessionManager::Instance().Broadcast(&diePkt);

    std::cout << "[DEATH] DIE 패킷 브로드캐스트 완료 playerId="
        << target->GetPlayerId()
        << std::endl;

    // 2. 서버 리스폰 처리
    target->Respawn();

    std::cout << "[DEATH] Respawn 완료 playerId="
        << target->GetPlayerId()
        << " 새 HP=" << target->GetHP()
        << " 위치=("
        << target->GetPosition().x << ", "
        << target->GetPosition().y << ", "
        << target->GetPosition().z << ")"
        << std::endl;

    // 3. Spawn 패킷
    SC_SpawnPlayer spawnPkt{};
    spawnPkt.header.size = sizeof(spawnPkt);
    spawnPkt.header.type =
        (uint16_t)PacketType::PKT_SC_SPAWN_PLAYER;
    spawnPkt.playerId = target->GetPlayerId();
    spawnPkt.x = target->GetPosition().x;
    spawnPkt.y = target->GetPosition().y;
    spawnPkt.z = target->GetPosition().z;
    spawnPkt.hp = target->GetHP();

    SessionManager::Instance().Broadcast(&spawnPkt);

    std::cout << "[DEATH] SPAWN 패킷 브로드캐스트 완료 playerId="
        << target->GetPlayerId()
        << " HP=" << spawnPkt.hp
        << std::endl;
}
