#include "Session.h"
#include "PacketDispatcher.h"
#include "PacketType.h"
#include "MapCollision.h"
#include "GroundHeightMap.h"
#include "HeightVolumeManager.h"

Session::Session(SOCKET socket)
    : socket_(socket), recvSize_(0)
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

    int size = header->size;

    send(socket_, reinterpret_cast<char*>(packet), size, 0);
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

void Session::Spawn(float spawnX, float spawnZ, float yaw)
{
    constexpr float HALF_HEIGHT = 1.5f; // Unity 기준

    float groundY = GroundHeightMap::Sample(spawnX, spawnZ);

    position_.x = spawnX;
    position_.z = spawnZ;
    position_.y = groundY + HALF_HEIGHT;
    rotationYaw_ = yaw;
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

    send(socket_, (char*)&pkt, sizeof(pkt), 0);
}

static float GetGroundY(float x, float z, float footY)
{
    float y;

    // 1. 다리 / 플랫폼 (HeightVolume)
    if (HeightVolumeManager::TryGetGroundY(x, z, footY, y))
        return y;

    // 2. 기본 지형
    return GroundHeightMap::Sample(x, z);
}

void Session::OnMove(float inputX, float inputZ, float yaw)
{
    constexpr float SPEED = 5.0f;
    constexpr float DT = 0.016f;

    constexpr float HALF_HEIGHT = 1.0f;
    constexpr float STEP_HEIGHT = 0.8f;

    constexpr float Y_LERP_SPEED = 12.0f;
    constexpr float Y_DEADZONE = 0.02f;

    float rad = yaw * 3.141592f / 180.f;

    float forwardX = sinf(rad);
    float forwardZ = cosf(rad);
    float rightX = cosf(rad);
    float rightZ = -sinf(rad);

    float dx = (rightX * inputX + forwardX * inputZ) * SPEED * DT;
    float dz = (rightZ * inputX + forwardZ * inputZ) * SPEED * DT;

    float curX = position_.x;
    float curZ = position_.z;
    float curY = position_.y;

    float curFootY = curY - HALF_HEIGHT;

    float nextX = curX;
    float nextZ = curZ;
    float targetFootY = curFootY;

    {
        float testX = curX + dx;
        float groundY = GetGroundY(testX, curZ, curFootY);

        float predictedFootY = groundY;
        float predictedHeadY = predictedFootY + HALF_HEIGHT * 2.0f;

        if (!HeightVolumeManager::IsCeilingBlocked(testX, curZ, predictedFootY, predictedHeadY))
        {
            if (groundY - curFootY <= STEP_HEIGHT)
            {
                nextX = testX;
                targetFootY = groundY;
            }
        }
    }

    {
        float testZ = curZ + dz;
        float groundY = GetGroundY(nextX, testZ, targetFootY);

        float predictedFootY = groundY;
        float predictedHeadY = predictedFootY + HALF_HEIGHT * 2.0f;

        if (!HeightVolumeManager::IsCeilingBlocked(nextX, testZ, predictedFootY, predictedHeadY))
        {
            if (groundY - targetFootY <= STEP_HEIGHT)
            {
                nextZ = testZ;
                targetFootY = groundY;
            }
        }
    }

    float targetY = targetFootY + HALF_HEIGHT;
    float diffY = targetY - curY;

    if (fabs(diffY) > Y_DEADZONE)
    {
        float t = Y_LERP_SPEED * DT;
        if (t > 1.0f) t = 1.0f;

        position_.y = curY + diffY * t;
    }

    position_.x = nextX;
    position_.z = nextZ;
    rotationYaw_ = yaw;
}
