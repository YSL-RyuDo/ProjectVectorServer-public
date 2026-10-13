#pragma once
#include "Common.h"
#include "OverlappedEx.h"
#include "HonorActivity.h"
#include "CombatReport.h"
#include "packetType.h"
#include "ReconnectJournal.h"

struct Vector3
{
    float x, y, z;
};

enum class SessionState
{
    CONNECTED,
    AUTHENTICATED,
    IN_LOBBY,
    IN_ROOM,
    IN_GAME
};

struct PlayerWeaponState
{
    int weaponId = 0;
    int magAmmo = 0;
    int reserveAmmo = 0;
    int skinId = 0;

    bool owned = false;
    bool isReloading = false;
};

enum class PlayerSkillSlot : uint8_t
{
    C = 0,
    Q = 1,
    E = 2,
    X = 3
};

struct PlayerMatchStats
{
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int damageDealt = 0;
    int headshotKills = 0;
    int meleeKills = 0;
    int spikePlants = 0;
    int spikeDefuses = 0;

    int firstHalfKills = 0;
    int firstHalfDeaths = 0;
    int firstHalfAssists = 0;

    int secondHalfKills = 0;
    int secondHalfDeaths = 0;
    int secondHalfAssists = 0;

    int overtimeKills = 0;
    int overtimeDeaths = 0;
    int overtimeAssists = 0;
};

struct AssistDamageRecord
{
    uint32_t attackerPlayerId = 0;
    int damage = 0;
    std::chrono::steady_clock::time_point lastHitTime;
};

class Session
{
public:
    explicit Session(SOCKET socket);
    ~Session();

    bool PostRecv();
    void OnRecv(int bytes, OverlappedEx* ov);
    SOCKET GetSocket() const { return socket_; }
    void Send(void* packet);

    void SetUserId(const std::string& id) { userId_ = id; }
    std::string GetUserId() const { return userId_; }

    void AddRecentPlayerUserId(const std::string& userId);
    std::vector<std::string> GetRecentPlayerUserIds() const;

    void Disconnect();
    bool IsDisconnected() const { return disconnected_.load(); }
    bool TryResume(const std::shared_ptr<Session>& transport);
    std::shared_ptr<Session> LogicalSession() const { return logicalSession_; }
    bool IsRestoring() const { return restoring_.load(); }
    uint8_t RestoreStage() const { return restoreStage_; }
    void SetRestoreStage(uint8_t value) { restoreStage_ = value; }
    void SetRestoreStreamReady(bool ready) { restoreStreamReady_ = ready; }
    void FinishRestore() { restoring_ = false; restoreStreamReady_ = true; }
    void SendRestore(void* packet);
    std::recursive_mutex& RestoreMutex() { return outboundMutex_; }
    std::vector<std::vector<char>> RestoreJournal() const { return reconnectJournal_.Snapshot(); }

    void SetAuthenticated(bool val) { isAuthenticated_ = val; }
    bool IsAuthenticated() const { return isAuthenticated_; }

    void SetState(SessionState state) { state_ = state; }
    SessionState GetState() const { return state_; }

    bool IsInLobby() const { return state_ == SessionState::IN_LOBBY; }
    bool IsInRoom() const { return state_ == SessionState::IN_ROOM; }
    bool IsInGame() const { return state_ == SessionState::IN_GAME; }

    void SetCurrentRoomId(uint32_t roomId) { if (currentRoomId_ != roomId) combatReport_.Reset(roomId); currentRoomId_ = roomId; }
    uint32_t GetCurrentRoomId() const { return currentRoomId_; }

    void SetPlayerId(uint32_t id) { playerId_ = id; }
    uint32_t GetPlayerId() const { return playerId_; }

    Vector3 GetPosition() const { return position_; }
    bool HasCurrentRoundPosition() const { return currentRoundPosition_.load(); }
    void SetPosition(float x, float y, float z)
    {
        position_.x = x;
        position_.y = y;
        position_.z = z;
        currentRoundPosition_ = true;
    }

    void SetYaw(float yaw) { rotationYaw_ = yaw; }
    float GetYaw() const { return rotationYaw_; }
    void SetPitch(float pitch) { rotationPitch_ = pitch; }
    float GetPitch() const { return rotationPitch_; }

    void SendHello(uint32_t myId);
    void SendSpawn(uint32_t playerId, float x, float y, float z);

    void Spawn(float x, float y, float z, float yaw);

    int GetHP() const { return hp_; }
    int GetMaxHP() const { return maxHp_; }

    int GetBarrier() const { return barrier_; }
    int GetMaxBarrier() const { return maxBarrier_; }

    void SetHP(int hp);
    void SetBarrier(int barrier);
    void SetArmor(int armor)
    {
        maxBarrier_ = armor;
        barrier_ = armor;

        if (maxBarrier_ < 0)
            maxBarrier_ = 0;

        if (barrier_ < 0)
            barrier_ = 0;
    }

    int Damage(int amount);

    bool IsDead() const { return hp_ <= 0; }

    void SetActiveCharacterId(int characterId) { activeCharacterId_ = characterId; }
    int GetActiveCharacterId() const { return activeCharacterId_; }

    void InitRoundState();
    void ResetRoundStateKeepInventory();

    int GetCurrentWeaponId() const { return currentWeaponId_; }
    void SetCurrentWeaponId(int weaponId) { currentWeaponId_ = weaponId; }

    bool HasWeapon(int weaponId) const;
    PlayerWeaponState* GetWeaponState(int weaponId);
    void GiveWeapon(int weaponId, int magAmmo, int reserveAmmo, int skinId = 0);

    void Respawn();

    PlayerMatchStats& GetMatchStats() { return matchStats_; }
    void ResetMatchStats() { matchStats_ = PlayerMatchStats{}; honorActivity_.Reset(currentRoomId_); }
    HonorActivity::Tracker& GetHonorActivity() { return honorActivity_; }
    CombatReport::Tracker& GetCombatReport() { return combatReport_; }

    void SetLastDamager(uint32_t playerId, bool headshot = false, int weaponId = -1) { lastDamagerPlayerId_ = playerId; lastDamageWasHeadshot_ = headshot; lastDamageWeaponId_ = weaponId; }
    uint32_t GetLastDamager() const { return lastDamagerPlayerId_; }
    bool GetLastDamageWasHeadshot() const { return lastDamageWasHeadshot_; }
    int GetLastDamageWeaponId() const { return lastDamageWeaponId_; }
    void ClearLastDamager() { lastDamagerPlayerId_ = 0; lastDamageWasHeadshot_ = false; lastDamageWeaponId_ = -1; }

    void AddAssistDamageRecord(uint32_t attackerPlayerId, int damage);
    std::vector<uint32_t> GetAssistPlayerIds(uint32_t killerPlayerId, int validSeconds) const;
    void ClearAssistDamageRecords();

    int GetCredits() const { return credits_; }

    void SetCredits(int credits)
    {
        credits_ = credits;

        if (credits_ < 0)
            credits_ = 0;

        if (credits_ > 9000)
            credits_ = 9000;
    }

    void AddCredits(int amount)
    {
        SetCredits(credits_ + amount);
    }

    void ResetCredits() { credits_ = 0; }

    bool HasPurchasedSkill(PlayerSkillSlot slot) const;
    bool HasAvailableSkill(PlayerSkillSlot slot) const;
    bool PurchaseSkill(PlayerSkillSlot slot);
    bool ConsumeSkill(PlayerSkillSlot slot);
    void PrepareSkillsForNextRound();
    void ResetSkills();
    uint8_t GetPurchasedSkillMask() const { return purchasedSkillMask_; }
    uint8_t GetUsedSkillMask() const { return usedSkillMask_; }

    void SetAnimIndex(uint8_t animIndex)
    {
        animIndex_ = animIndex;
    }

    uint8_t GetAnimIndex() const
    {
        return animIndex_;
    }

    bool RemoveWeapon(int weaponId);
    std::vector<PlayerWeaponState> GetOwnedWeapons() const;

private:
    void ProcessPackets();
    void FinalizeDisconnect();
    void CloseSocket();
    void SendRaw(void* packet);
    int SelectBestCurrentWeaponId() const;

private:
    SOCKET socket_;
    char recvBuffer_[4096];
    int recvSize_ = 0;
    bool isAuthenticated_ = false;
    SessionState state_ = SessionState::CONNECTED;
    uint32_t currentRoomId_ = 0;

    std::string userId_;

    uint32_t playerId_ = 0;
    Vector3 position_{ 0.f, 1.f, 0.f };
    std::atomic<bool> currentRoundPosition_{false};
    float rotationYaw_ = 0.f;
    float rotationPitch_ = 0.0f;

    int maxHp_ = 100;
    int hp_ = 100;

    int maxBarrier_ = 0;
    int barrier_ = 0;
    int credits_ = 0;

    uint8_t purchasedSkillMask_ = 0;
    uint8_t usedSkillMask_ = 0;

    bool isDead_ = false;

    int currentWeaponId_ = -1;
    int activeCharacterId_ = 0;
    std::unordered_map<int, PlayerWeaponState> weapons_;

    PlayerMatchStats matchStats_;
    HonorActivity::Tracker honorActivity_;
    CombatReport::Tracker combatReport_;
    uint32_t lastDamagerPlayerId_ = 0;
    bool lastDamageWasHeadshot_ = false;
    int lastDamageWeaponId_ = -1;
    std::unordered_map<uint32_t, AssistDamageRecord> assistDamageRecords_;

    uint8_t animIndex_ = 0;

    std::atomic<bool> disconnected_{ false };
    std::shared_ptr<Session> logicalSession_;
    std::weak_ptr<Session> transport_;
    std::recursive_mutex outboundMutex_;
    ReconnectJournal reconnectJournal_;
    std::atomic<bool> restoring_{false};
    bool restoreStreamReady_ = false;
    uint8_t restoreStage_ = 0;
    uint64_t disconnectGeneration_ = 0;
    std::mutex sendMutex_;
    mutable std::mutex recentPlayersMutex_;
    std::vector<std::string> recentPlayerUserIds_;
};
