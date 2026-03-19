#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"

Session::Session(SOCKET socket)
    : socket_(socket)
{
}

Session::~Session()
{
    closesocket(socket_);
}

void Session::PostRecv()
{
    auto* ov = new OverlappedEx(IOType::Recv);
    DWORD flags = 0;
    WSARecv(socket_, &ov->wsaBuf, 1, nullptr, &flags, &ov->overlapped, nullptr);
}

void Session::OnRecv(int bytes, OverlappedEx* ov)
{
    memcpy(recvBuffer_ + recvSize_, ov->buffer, bytes);
    recvSize_ += bytes;
    ProcessPackets();
}

void Session::Send(void* packet)
{
    PacketHeader* header =
        reinterpret_cast<PacketHeader*>(packet);

    send(socket_, reinterpret_cast<char*>(packet), header->size, 0);
}

void Session::Disconnect()
{
    std::cout << "[DISCONNECT] playerId=" << GetPlayerId() << std::endl;

    UserManager::Instance().OnLogout(this);
    SessionManager::Instance().Remove(GetPlayerId());
    closesocket(socket_);
}

void Session::ProcessPackets()
{
    int offset = 0;

    while (true)
    {
        if (recvSize_ - offset < sizeof(PacketHeader))
            break;

        auto* header =
            reinterpret_cast<PacketHeader*>(recvBuffer_ + offset);

        if (recvSize_ - offset < header->size)
            break;

        PacketDispatcher::Dispatch(this, header);

        offset += header->size;
    }

    if (offset > 0)
    {
        memmove(recvBuffer_,
            recvBuffer_ + offset,
            recvSize_ - offset);
        recvSize_ -= offset;
    }
}

void Session::Spawn(float x, float y, float z, float yaw)
{
    position_.x = x;
    position_.y = y;
    position_.z = z;
    rotationYaw_ = yaw;
}

void Session::SendHello(uint32_t myId)
{
    SC_Hello pkt{};
    pkt.header.size = sizeof(SC_Hello);
    pkt.header.type = (uint16_t)PacketType::PKT_SC_HELLO;
    pkt.myPlayerId = myId;

    send(socket_, (char*)&pkt, sizeof(pkt), 0);
}

void Session::SendSpawn(uint32_t playerId, float x, float y, float z)
{
    SC_SpawnPlayer pkt{};
    pkt.header.size = sizeof(SC_SpawnPlayer);
    pkt.header.type = (uint16_t)PacketType::PKT_SC_SPAWN_PLAYER;
    pkt.playerId = playerId;
    pkt.x = x;
    pkt.y = y;
    pkt.z = z;
    pkt.hp = hp_;

    send(socket_, (char*)&pkt, sizeof(pkt), 0);
}

void Session::Damage(int amount)
{
    if (hp_ <= 0)
        return; // 이미 죽었으면 무시

    hp_ -= amount;

    if (hp_ < 0)
        hp_ = 0;
}


void Session::Respawn()
{
    hp_ = 100;
    isDead_ = false;

    // 임시 스폰 위치 (간소화)
    position_.x = 0.f;
    position_.y = 1.f;
    position_.z = 0.f;
    rotationYaw_ = 0.f;
}