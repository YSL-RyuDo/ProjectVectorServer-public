#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "Session.h"
#include "SessionManager.h"

void PacketDispatcher::Dispatch(Session* session, PacketHeader* header)
{
    switch ((PacketType)header->type)
    {
        case PacketType::PKT_CS_MOVE:
        {
            auto* pkt = reinterpret_cast<CS_Move*>(header);

            session->OnMove(pkt->inputX, pkt->inputZ, pkt->yaw);

            SC_Move movePkt{};
            movePkt.header.size = sizeof(SC_Move);
            movePkt.header.type = (uint16_t)PacketType::PKT_SC_MOVE;
            movePkt.playerId = session->GetPlayerId();
            movePkt.x = session->GetPosition().x;
            movePkt.y = session->GetPosition().y;
            movePkt.z = session->GetPosition().z;
            movePkt.yaw = session->GetYaw();

            SessionManager::Instance().Broadcast(&movePkt);
            break;
        }

        case PacketType::PKT_CS_ROTATE:
        {
            auto* pkt = reinterpret_cast<CS_Rotate*>(header);
            session->SetYaw(pkt->yaw);

            SC_Rotate out{};
            out.header.size = sizeof(SC_Rotate);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROTATE;
            out.playerId = session->GetPlayerId();
            out.yaw = pkt->yaw;

            SessionManager::Instance().Broadcast(&out);
            break;
        }
    }
}
