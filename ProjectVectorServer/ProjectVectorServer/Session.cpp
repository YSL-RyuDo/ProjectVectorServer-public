#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "RoomManager.h"

namespace
{
    void FillRoomMemberInfoForDisconnect(RoomMemberInfo& out, const RoomMember& member)
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

    void SendRoomStateToMembersAfterDisconnect(const Room& room)
    {
        SC_RoomState out{};
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
            FillRoomMemberInfoForDisconnect(out.members[i], room.members[i]);
        }

        for (const RoomMember& member : room.members)
        {
            Session* target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[ROOM STATE BROADCAST ON DISCONNECT] roomId="
            << room.roomId
            << " memberCount=" << static_cast<int>(out.memberCount)
            << std::endl;
    }
}

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
    std::cout << "[DISCONNECT] playerId=" << GetPlayerId()
        << " userId=" << GetUserId()
        << " state=" << static_cast<int>(GetState())
        << " roomId=" << GetCurrentRoomId()
        << std::endl;

    if (GetState() == SessionState::IN_ROOM && GetCurrentRoomId() != 0)
    {
        uint32_t roomId = GetCurrentRoomId();

        bool roomDeleted = false;
        Room updatedRoom{};

        bool ok = RoomManager::Instance().LeaveRoom(
            roomId,
            GetPlayerId(),
            roomDeleted,
            updatedRoom
        );

        if (ok)
        {
            std::cout << "[ROOM LEAVE BY DISCONNECT] playerId="
                << GetPlayerId()
                << " roomId=" << roomId
                << " roomDeleted=" << roomDeleted
                << std::endl;

            if (!roomDeleted)
            {
                SendRoomStateToMembersAfterDisconnect(updatedRoom);
            }
        }
        else
        {
            std::cout << "[ROOM LEAVE BY DISCONNECT FAIL] playerId="
                << GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
        }

        SetCurrentRoomId(0);
        SetState(SessionState::IN_LOBBY);
    }

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
        return;

    hp_ -= amount;

    if (hp_ < 0)
        hp_ = 0;
}

void Session::Respawn()
{
    hp_ = 100;
    isDead_ = false;

    position_.x = 0.f;
    position_.y = 1.f;
    position_.z = 0.f;
    rotationYaw_ = 0.f;
}