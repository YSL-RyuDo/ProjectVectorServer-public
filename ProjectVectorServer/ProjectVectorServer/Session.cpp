#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "RoomManager.h"
#include "ServerWeaponData.h"

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

void Session::SetHP(int hp)
{
    hp_ = hp;

    if (hp_ < 0)
        hp_ = 0;

    if (hp_ > maxHp_)
        hp_ = maxHp_;
}

void Session::SetBarrier(int barrier)
{
    barrier_ = barrier;

    if (barrier_ < 0)
        barrier_ = 0;

    if (barrier_ > maxBarrier_)
        barrier_ = maxBarrier_;
}

void Session::InitRoundState()
{
    maxHp_ = 100;
    hp_ = maxHp_;

    maxBarrier_ = 0;
    barrier_ = 0;

    isDead_ = false;

    weapons_.clear();

    // 기본 권총 P92
    GiveWeapon(
        static_cast<int>(ServerWeaponId::P92),
        12,
        36
    );

    // 기본 나이프
    GiveWeapon(
        static_cast<int>(ServerWeaponId::Knife),
        0,
        0
    );

    // 시작 무기는 P92
    currentWeaponId_ =
        static_cast<int>(ServerWeaponId::P92);
}

void Session::ResetRoundStateKeepInventory()
{
    maxHp_ = 100;
    hp_ = maxHp_;

    // 베리어는 라운드마다 초기화
    maxBarrier_ = 0;
    barrier_ = 0;

    isDead_ = false;

    int primaryWeaponId = -1;
    int pistolWeaponId = -1;
    int knifeWeaponId = -1;

    // 새 라운드 시작이므로 재장전 상태 해제 + 탄약 회복
    for (auto& pair : weapons_)
    {
        PlayerWeaponState& weapon = pair.second;

        if (!weapon.owned)
            continue;

        weapon.isReloading = false;

        const ServerWeaponData* data =
            ServerWeaponTable::Find(weapon.weaponId);

        if (data == nullptr)
            continue;

        weapon.magAmmo = data->magSize;
        weapon.reserveAmmo = data->maxReserveAmmo;

        if (data->weaponType == ServerWeaponType::Pistol)
        {
            pistolWeaponId = weapon.weaponId;
        }
        else if (data->weaponType == ServerWeaponType::Knife)
        {
            knifeWeaponId = weapon.weaponId;
        }
        else
        {
            // AR, SMG, Shotgun, DMR, LMG, SR 전부 주무기
            primaryWeaponId = weapon.weaponId;
        }
    }

    // 혹시 무기가 하나도 없으면 기본 지급
    if (weapons_.empty())
    {
        GiveWeapon(0, 12, 24);
        GiveWeapon(13, 0, 0);
        currentWeaponId_ = 0;
        return;
    }

    // 다음 라운드 시작 장비 우선순위:
    // 주무기 보유 중이면 주무기부터 들고 시작
    if (primaryWeaponId >= 0)
    {
        currentWeaponId_ = primaryWeaponId;
        return;
    }

    // 주무기가 없으면 권총
    if (pistolWeaponId >= 0)
    {
        currentWeaponId_ = pistolWeaponId;
        return;
    }

    // 권총도 없으면 칼
    if (knifeWeaponId >= 0)
    {
        currentWeaponId_ = knifeWeaponId;
        return;
    }

    // 마지막 방어
    GiveWeapon(0, 12, 24);
    GiveWeapon(13, 0, 0);
    currentWeaponId_ = 0;
}

void Session::GiveWeapon(int weaponId, int magAmmo, int reserveAmmo)
{
    PlayerWeaponState state{};
    state.weaponId = weaponId;
    state.magAmmo = magAmmo;
    state.reserveAmmo = reserveAmmo;
    state.owned = true;
    state.isReloading = false;

    weapons_[weaponId] = state;
}

bool Session::HasWeapon(int weaponId) const
{
    auto it = weapons_.find(weaponId);

    if (it == weapons_.end())
        return false;

    return it->second.owned;
}

PlayerWeaponState* Session::GetWeaponState(int weaponId)
{
    auto it = weapons_.find(weaponId);

    if (it == weapons_.end())
        return nullptr;

    return &it->second;
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

    if (amount <= 0)
        return;

    if (barrier_ > 0)
    {
        int absorbed = std::min(barrier_, amount);
        barrier_ -= absorbed;
        amount -= absorbed;
    }

    if (amount > 0)
    {
        hp_ -= amount;

        if (hp_ < 0)
            hp_ = 0;
    }

    if (hp_ <= 0)
    {
        hp_ = 0;
        isDead_ = true;
    }
}

void Session::Respawn()
{
    InitRoundState();

    position_.x = 0.f;
    position_.y = 1.f;
    position_.z = 0.f;
    rotationYaw_ = 0.f;
}

void Session::AddAssistDamageRecord(uint32_t attackerPlayerId, int damage)
{
    if (attackerPlayerId == 0)
        return;

    if (attackerPlayerId == playerId_)
        return;

    AssistDamageRecord& record = assistDamageRecords_[attackerPlayerId];

    record.attackerPlayerId = attackerPlayerId;
    record.damage += damage;
    record.lastHitTime = std::chrono::steady_clock::now();
}

std::vector<uint32_t> Session::GetAssistPlayerIds(
    uint32_t killerPlayerId,
    int validSeconds) const
{
    std::vector<uint32_t> result;

    auto now = std::chrono::steady_clock::now();

    for (const auto& pair : assistDamageRecords_)
    {
        const AssistDamageRecord& record = pair.second;

        if (record.attackerPlayerId == 0)
            continue;

        if (record.attackerPlayerId == killerPlayerId)
            continue;

        auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(
                now - record.lastHitTime
            ).count();

        if (elapsed > validSeconds)
            continue;

        if (record.damage <= 0)
            continue;

        result.push_back(record.attackerPlayerId);
    }

    return result;
}

void Session::ClearAssistDamageRecords()
{
    assistDamageRecords_.clear();
}

bool Session::RemoveWeapon(int weaponId)
{
    auto it = weapons_.find(weaponId);

    if (it == weapons_.end())
        return false;

    weapons_.erase(it);

    if (currentWeaponId_ == weaponId)
    {
        int p92Id =
            static_cast<int>(ServerWeaponId::P92);

        int knifeId =
            static_cast<int>(ServerWeaponId::Knife);

        if (HasWeapon(p92Id))
        {
            currentWeaponId_ = p92Id;
        }
        else if (HasWeapon(knifeId))
        {
            currentWeaponId_ = knifeId;
        }
        else
        {
            currentWeaponId_ = -1;
        }
    }

    return true;
}

std::vector<PlayerWeaponState> Session::GetOwnedWeapons() const
{
    std::vector<PlayerWeaponState> result;

    for (const auto& pair : weapons_)
    {
        if (pair.second.owned)
            result.push_back(pair.second);
    }

    return result;
}