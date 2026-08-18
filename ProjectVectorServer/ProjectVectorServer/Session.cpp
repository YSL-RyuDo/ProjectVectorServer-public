#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "PacketDispatcher.h"
#include "PacketType.h"
#include "Packet.h"
#include "RoomManager.h"
#include "ServerWeaponData.h"
#include "WeaponSkinManager.h"

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
            std::shared_ptr<Session> target = SessionManager::Instance().Find(member.playerId);
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
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool Session::PostRecv()
{
    if (disconnected_.load())
        return false;

    auto* ov = new OverlappedEx(IOType::Recv);
    DWORD flags = 0;
    int result = WSARecv(
        socket_,
        &ov->wsaBuf,
        1,
        nullptr,
        &flags,
        &ov->overlapped,
        nullptr);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        delete ov;
        return false;
    }

    return true;
}

void Session::OnRecv(int bytes, OverlappedEx* ov)
{
    memcpy(recvBuffer_ + recvSize_, ov->buffer, bytes);
    recvSize_ += bytes;
    ProcessPackets();
}

void Session::Send(void* packet)
{
    if (packet == nullptr || disconnected_.load())
        return;

    PacketHeader* header =
        reinterpret_cast<PacketHeader*>(packet);

    std::lock_guard<std::mutex> lock(sendMutex_);
    if (disconnected_.load() || socket_ == INVALID_SOCKET)
        return;

    int sentBytes = 0;
    const int packetSize = static_cast<int>(header->size);
    const char* bytes = reinterpret_cast<const char*>(packet);

    while (sentBytes < packetSize)
    {
        int result = send(socket_, bytes + sentBytes, packetSize - sentBytes, 0);
        if (result == SOCKET_ERROR || result == 0)
            break;

        sentBytes += result;
    }
}

void Session::Disconnect()
{
    bool expected = false;
    if (!disconnected_.compare_exchange_strong(expected, true))
        return;

    std::cout << "[DISCONNECT] playerId=" << GetPlayerId()
        << " userId=" << GetUserId()
        << " state=" << static_cast<int>(GetState())
        << " roomId=" << GetCurrentRoomId()
        << std::endl;

    SessionState state = GetState();
    if ((state == SessionState::IN_ROOM || state == SessionState::IN_GAME) &&
        GetCurrentRoomId() != 0)
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

    std::lock_guard<std::mutex> lock(sendMutex_);
    if (socket_ != INVALID_SOCKET)
    {
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
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

int Session::SelectBestCurrentWeaponId() const
{
    int primaryWeaponId = -1;
    int pistolWeaponId = -1;
    int knifeWeaponId = -1;

    for (const auto& pair : weapons_)
    {
        const PlayerWeaponState& weapon = pair.second;

        if (!weapon.owned)
            continue;

        const ServerWeaponData* data = ServerWeaponTable::Find(weapon.weaponId);
        if (data == nullptr)
            continue;

        if (data->weaponType == ServerWeaponType::Knife)
        {
            knifeWeaponId = weapon.weaponId;
        }
        else if (data->weaponType == ServerWeaponType::Pistol)
        {
            pistolWeaponId = weapon.weaponId;
        }
        else
        {
            // AR, SMG, Shotgun, DMR, LMG, SR 전부 주무기 취급
            primaryWeaponId = weapon.weaponId;
        }
    }

    if (primaryWeaponId >= 0)
        return primaryWeaponId;

    if (pistolWeaponId >= 0)
        return pistolWeaponId;

    if (knifeWeaponId >= 0)
        return knifeWeaponId;

    return 0;
}

void Session::InitRoundState()
{
    maxHp_ = 100;
    hp_ = maxHp_;

    maxBarrier_ = 0;
    barrier_ = 0;

    isDead_ = false;

    weapons_.clear();

    std::array<int, WEAPON_SKIN_WEAPON_COUNT> equippedSkins{};
    WeaponSkinManager::Instance().GetEquippedSkins(userId_, activeCharacterId_, equippedSkins);

    // 기본 권총 P92
    GiveWeapon(
        static_cast<int>(ServerWeaponId::P92),
        12,
        36,
        equippedSkins[static_cast<int>(ServerWeaponId::P92)]
    );

    // 기본 나이프
    GiveWeapon(
        static_cast<int>(ServerWeaponId::Knife),
        0,
        0,
        equippedSkins[static_cast<int>(ServerWeaponId::Knife)]
    );

    currentWeaponId_ = SelectBestCurrentWeaponId();
}

void Session::ResetRoundStateKeepInventory()
{
    maxHp_ = 100;
    hp_ = maxHp_;

    // 생존자의 보호막은 현재 남은 수치 그대로 다음 라운드에 보존한다.
    // 예: 100 보호막이 40 남았다면 다음 라운드도 40/100이다.
    // 완전히 파괴된 경우에만 보유 보호막 정보를 제거한다.
    if (barrier_ <= 0)
    {
        barrier_ = 0;
        maxBarrier_ = 0;
    }

    isDead_ = false;

    for (auto& pair : weapons_)
    {
        PlayerWeaponState& weapon = pair.second;

        if (!weapon.owned)
            continue;

        weapon.isReloading = false;

        const ServerWeaponData* data = ServerWeaponTable::Find(weapon.weaponId);
        if (data == nullptr)
            continue;

        weapon.magAmmo = data->magSize;
        weapon.reserveAmmo = data->maxReserveAmmo;
    }

    bool hasFirearm = false;
    bool hasKnife = false;

    for (const auto& pair : weapons_)
    {
        const PlayerWeaponState& weapon = pair.second;
        if (!weapon.owned)
            continue;

        const ServerWeaponData* data = ServerWeaponTable::Find(weapon.weaponId);
        if (data == nullptr)
            continue;

        if (data->weaponType == ServerWeaponType::Knife)
            hasKnife = true;
        else
            hasFirearm = true;
    }

    std::array<int, WEAPON_SKIN_WEAPON_COUNT> equippedSkins{};
    WeaponSkinManager::Instance().GetEquippedSkins(
        userId_,
        activeCharacterId_,
        equippedSkins
    );

    // A knife does not count as a gun. Restore weapon 0 (P92) when the
    // player ended the previous round without any firearm.
    if (!hasFirearm)
    {
        GiveWeapon(
            static_cast<int>(ServerWeaponId::P92),
            12,
            36,
            equippedSkins[static_cast<int>(ServerWeaponId::P92)]
        );
    }

    if (!hasKnife)
    {
        GiveWeapon(
            static_cast<int>(ServerWeaponId::Knife),
            0,
            0,
            equippedSkins[static_cast<int>(ServerWeaponId::Knife)]
        );
    }

    currentWeaponId_ = SelectBestCurrentWeaponId();
}

void Session::GiveWeapon(int weaponId, int magAmmo, int reserveAmmo, int skinId)
{
    PlayerWeaponState state{};
    state.weaponId = weaponId;
    state.magAmmo = magAmmo;
    state.reserveAmmo = reserveAmmo;
    state.skinId = skinId;
    state.owned = true;
    state.isReloading = false;

    weapons_[weaponId] = state;

    if (currentWeaponId_ < 0)
    {
        currentWeaponId_ = SelectBestCurrentWeaponId();
    }
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

int Session::Damage(int amount)
{
    if (hp_ <= 0)
        return 0;

    if (amount <= 0)
        return 0;

    int healthBefore = hp_ + barrier_;

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

    return healthBefore - (hp_ + barrier_);
}

void Session::Respawn()
{
    ResetRoundStateKeepInventory();

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

bool Session::HasPurchasedSkill(PlayerSkillSlot slot) const
{
    uint8_t slotIndex = static_cast<uint8_t>(slot);
    if (slotIndex > static_cast<uint8_t>(PlayerSkillSlot::X))
        return false;

    uint8_t bit = static_cast<uint8_t>(1u << slotIndex);
    return (purchasedSkillMask_ & bit) != 0;
}

bool Session::HasAvailableSkill(PlayerSkillSlot slot) const
{
    uint8_t slotIndex = static_cast<uint8_t>(slot);
    if (slotIndex > static_cast<uint8_t>(PlayerSkillSlot::X))
        return false;

    uint8_t bit = static_cast<uint8_t>(1u << slotIndex);
    return (purchasedSkillMask_ & bit) != 0 &&
        (usedSkillMask_ & bit) == 0;
}

bool Session::PurchaseSkill(PlayerSkillSlot slot)
{
    if (HasPurchasedSkill(slot))
        return false;

    uint8_t slotIndex = static_cast<uint8_t>(slot);
    if (slotIndex > static_cast<uint8_t>(PlayerSkillSlot::X))
        return false;

    uint8_t bit = static_cast<uint8_t>(1u << slotIndex);
    purchasedSkillMask_ |= bit;
    usedSkillMask_ &= static_cast<uint8_t>(~bit);
    return true;
}

bool Session::ConsumeSkill(PlayerSkillSlot slot)
{
    if (!HasAvailableSkill(slot))
        return false;

    uint8_t bit = static_cast<uint8_t>(
        1u << static_cast<uint8_t>(slot)
    );
    usedSkillMask_ |= bit;
    return true;
}

void Session::PrepareSkillsForNextRound()
{
    // Keep only purchased skills that were not used in the previous round.
    purchasedSkillMask_ &= static_cast<uint8_t>(~usedSkillMask_);
    usedSkillMask_ = 0;
}

void Session::ResetSkills()
{
    purchasedSkillMask_ = 0;
    usedSkillMask_ = 0;
}

bool Session::RemoveWeapon(int weaponId)
{
    auto it = weapons_.find(weaponId);

    if (it == weapons_.end())
        return false;

    weapons_.erase(it);

    if (currentWeaponId_ == weaponId)
    {
        currentWeaponId_ = SelectBestCurrentWeaponId();
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
