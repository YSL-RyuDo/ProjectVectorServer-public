#include "PacketDispatcher.h"
#include "CareerManager.h"
#include "FriendManager.h"
#include "LobbyPartyState.h"
#include "FriendPacketService.h"
#include "ProfilePacketService.h"
#include "ShopPacketService.h"
#include "ProfileManager.h"
#include "RankRatingCalculator.h"
#include "PacketType.h"
#include "Packet.h"
#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"
#include "RoomManager.h"
#include "ServerWeaponData.h"
#include "WeaponSkinManager.h"

#include<fstream>
#include<sstream>
#include <cstring>
#include <windows.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <limits>
#include <vector>
#include <unordered_set>
#include <cmath>
#include <random>

namespace
{
    constexpr uint16_t PICK_TIME_SECONDS = 20;
    constexpr uint16_t PREPARE_TIME_SECONDS = 10;
#ifdef VECTOR_RANKED_CAPTURE_QA
    // Opt-in recording build only; never changes the normal server rules.
    constexpr uint16_t GAME_PREPARE_SECONDS = 8;
#else
    constexpr uint16_t GAME_PREPARE_SECONDS = 20;
#endif
    constexpr int DRONE_MAX_HP = 100;
    constexpr const char* TEST_MAP_NAME = "TestMap";
    constexpr const char* DESERT_MAP_NAME = "Desert";

    const char* GetMapName(uint8_t mapId)
    {
        switch (static_cast<GameMapId>(mapId))
        {
        case GameMapId::TEST_MAP:
            return TEST_MAP_NAME;
        case GameMapId::DESERT:
            return DESERT_MAP_NAME;
        default:
            return nullptr;
        }
    }

    uint8_t GetMapId(const std::string& mapName)
    {
        return mapName == DESERT_MAP_NAME
            ? static_cast<uint8_t>(GameMapId::DESERT)
            : static_cast<uint8_t>(GameMapId::TEST_MAP);
    }

    std::string ChooseRandomMatchMapName()
    {
#ifdef VECTOR_RANKED_CAPTURE_QA
        return TEST_MAP_NAME;
#else
        thread_local std::mt19937 randomEngine(std::random_device{}());
        std::uniform_int_distribution<int> distribution(0, 1);
        return distribution(randomEngine) == 0 ? TEST_MAP_NAME : DESERT_MAP_NAME;
#endif
    }

    constexpr uint16_t ROUND_END_DELAY_SECONDS = 6;
    constexpr uint16_t ROUND_PLAY_SECONDS = 100;
    constexpr uint16_t SPIKE_EXPLODE_SECONDS = 45;

    // 정규전 기준 6선승
#ifdef VECTOR_RANKED_CAPTURE_QA
    constexpr uint8_t MATCH_WIN_ROUND_COUNT = 1;
#else
    constexpr uint8_t MATCH_WIN_ROUND_COUNT = 6;
#endif
    // 5라운드 종료 후 공수 전환
    constexpr uint8_t SIDE_SWAP_AFTER_ROUND = 5;
    //5:5 이후 11라운드부터 듀스
    constexpr uint8_t OVERTIME_START_ROUND = 11;

    constexpr uint8_t OVERTIME_TRIGGER_SCORE = 5;
    constexpr uint8_t SECOND_HALF_END_ROUND = OVERTIME_START_ROUND - 1;
    constexpr int OVERTIME_ROUND_CREDIT = 6000;

    constexpr int SCANNER_MAX_HP = 25;
    constexpr float SCANNER_RADIUS = 6.5f;
    constexpr float SCANNER_RADIUS_SQ = SCANNER_RADIUS * SCANNER_RADIUS;

    constexpr uint8_t SCANNER_SHOCK_STACK = 1;
    constexpr float SCANNER_SHOCK_DURATION = 1.0f;

    constexpr int SCANNER_DESTROY_DELAY_MS = 5500;
    std::unordered_set<uint64_t> g_scannerZappedTargets;

    uint64_t MakeScannerTargetKey(uint32_t scannerId,uint32_t targetPlayerId)
    {
        return
            (static_cast<uint64_t>(scannerId) << 32) |
            targetPlayerId;
    }

    // 위치가 이 정도 이상 변하면 움직임으로 판단
    constexpr float SCANNER_MOVE_EPSILON_SQ = 0.0004f;

    // 패킷이 끊겨도 이 시간 지나면 움직이지 않는 것으로 처리
    constexpr int SCANNER_MOVING_TIMEOUT_MS = 180;

    struct ServerScannerState
    {
        uint32_t scannerId = 0;
        uint32_t ownerPlayerId = 0;
        uint32_t roomId = 0;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;

        int hp = SCANNER_MAX_HP;

        bool anchored = false;
    };

    struct PlayerScannerMoveState
    {
        bool moving = false;
        std::chrono::steady_clock::time_point lastUpdate;
    };

    std::mutex g_scannerMutex;
    std::unordered_map<uint32_t, ServerScannerState> g_scanners;
    uint32_t g_nextScannerId = 1;

    std::mutex g_scannerMoveStateMutex;
    std::unordered_map<uint32_t, PlayerScannerMoveState> g_scannerMoveStates;

    constexpr float EMP_RADIUS = 7.0f;
    constexpr float EMP_RADIUS_SQ = EMP_RADIUS * EMP_RADIUS;

    constexpr int ELECTRIC_MAX_STACK = 3;
    constexpr int ELECTRIC_STACK_EXPIRE_MS = 5000;

    struct ElectricStatusState
    {
        uint8_t shockStack = 0;

        std::chrono::steady_clock::time_point shockExpireTime;
        std::chrono::steady_clock::time_point stunEndTime;
    };

    std::mutex g_electricStatusMutex;
    std::unordered_map<uint32_t, ElectricStatusState> g_electricStatuses;

    constexpr float ULTIMATE_RANGE = 18.0f;
    constexpr float ULTIMATE_RANGE_SQ = ULTIMATE_RANGE * ULTIMATE_RANGE;

    // 전방 120도 정도
    constexpr float ULTIMATE_CONE_DOT = 0.5f;

    constexpr int32_t ULTIMATE_DAMAGE = 45;
    constexpr uint8_t ULTIMATE_STACK = 2;

    bool IsOvertimeScore(uint8_t scoreA, uint8_t scoreB)
    {
        return scoreA == OVERTIME_TRIGGER_SCORE &&
            scoreB == OVERTIME_TRIGGER_SCORE;
    }

    bool IsFirstHalfRound(uint8_t currentRound)
    {
        return currentRound <= SIDE_SWAP_AFTER_ROUND;
    }

    bool IsSecondHalfRound(uint8_t currentRound)
    {
        return currentRound > SIDE_SWAP_AFTER_ROUND &&
            currentRound < OVERTIME_START_ROUND;
    }

    bool IsOvertimeRound(uint8_t currentRound)
    {
        return currentRound >= OVERTIME_START_ROUND;
    }

    struct MatchRuntimeState
    {
        uint8_t currentRound = 1;
        uint8_t scoreA = 0;
        uint8_t scoreB = 0;

        uint8_t loseStreakA = 0;
        uint8_t loseStreakB = 0;

        uint32_t spikeCarrierPlayerId = 0;
        bool spikePlanted = false;

        uint32_t spikeDefuserPlayerId = 0;
        bool spikeHalfDefused = false;

        // 설치할 때마다 증가하는 고유 번호.
        // 이전 라운드의 분리된 타이머 스레드가 다음 라운드 스파이크를
        // 잘못 폭발시키는 것을 막는다.
        uint64_t spikePlantGeneration = 0;

        bool buyPhase = true;

        bool isOvertime = false;
        bool roundResolving = false;
        bool matchEnded = false;

        int64_t startedAtUnix = 0;
        std::chrono::steady_clock::time_point startedAtSteady;
    };

    std::mutex g_matchStateMutex;
    std::unordered_map<uint32_t, MatchRuntimeState> g_matchStates;

    std::mutex g_gameLoadReadyMutex;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> g_gameLoadReadyPlayers;
    std::unordered_set<uint32_t> g_gamePrepareStartedRooms;

    std::mutex g_resultLeaveMutex;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> g_resultLeavePlayers;

    struct MatchmakingEntry
    {
        uint32_t playerId = 0;
        uint8_t mode = 0;
        int matchmakingRating = 0;
        int visibleTierId = 0;
        // Solo/duo/trio queues use the same team optimizer and rating rules.
        uint64_t partyId = 0;
        uint8_t partySize = 1;
        std::chrono::steady_clock::time_point queuedAt;
    };

    std::recursive_mutex g_matchmakingMutex;
    std::vector<MatchmakingEntry> g_matchmakingQueue;
    std::once_flag g_matchmakingWorkerOnce;
    LobbyPartyState g_lobbyParties;
    struct ReadyMatch
    {
        uint32_t token;
        std::vector<MatchmakingEntry> group;
        std::unordered_set<uint32_t> accepted;
        std::chrono::steady_clock::time_point deadline;
    };
    std::unordered_map<uint32_t, ReadyMatch> g_readyMatches;
    uint32_t g_nextReadyToken = 0;

    bool SocialBusy(uint32_t player)
    {
        for (const auto& entry : g_matchmakingQueue) if (entry.playerId == player) return true;
        for (const auto& pair : g_readyMatches)
            for (const auto& entry : pair.second.group) if (entry.playerId == player) return true;
        return false;
    }

    std::unordered_map<std::string, int> g_creditRewards;

    constexpr float BEAST_STEALTH_DURATION = 5.0f;
    constexpr float BEAST_STEALTH_SPEED_MULTIPLIER = 1.5f;
    constexpr float BEAST_STEALTH_REVEAL_RANGE = 3.0f;

    struct BeastStealthState
    {
        uint32_t roomId = 0;
        bool active = false;

        float speedMultiplier = 1.0f;
        float revealRange = 3.0f;

        uint64_t version = 0;

        std::chrono::steady_clock::time_point endTime;
    };

    std::mutex g_beastStealthMutex;
    std::unordered_map<uint32_t, BeastStealthState> g_beastStealthStates;

    constexpr float ENTRY_SMOKE_RADIUS = 4.5f;
    constexpr float ENTRY_SMOKE_DURATION = 6.0f;

    struct EntrySmokeState
    {
        uint32_t smokeId = 0;
        uint32_t ownerPlayerId = 0;
        uint32_t roomId = 0;

        bool active = false;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        float nx = 0.0f;
        float ny = 1.0f;
        float nz = 0.0f;
    };

    std::mutex g_entrySmokeMutex;
    std::unordered_map<uint32_t, EntrySmokeState> g_entrySmokes;
    uint32_t g_nextEntrySmokeId = 1;

    constexpr float ENTRY_DASH_FORCE = 28.0f;
    constexpr float ENTRY_DASH_DURATION = 0.28f;

    constexpr uint8_t ENTRY_BLADE_MAX_COUNT = 5;
    constexpr float ENTRY_BLADE_DURATION = 12.0f;
    constexpr float ENTRY_BLADE_RANGE = 35.0f;
    constexpr float ENTRY_BLADE_RANGE_SQ = ENTRY_BLADE_RANGE * ENTRY_BLADE_RANGE;
    constexpr int32_t ENTRY_BLADE_DAMAGE = 50;
    constexpr float ENTRY_BLADE_HIT_DOT = 0.96f;

    struct EntryBladeState
    {
        uint32_t playerId = 0;
        uint32_t roomId = 0;

        bool active = false;
        uint8_t bladeCount = 0;

        std::chrono::steady_clock::time_point endTime;
    };

    std::mutex g_entryBladeMutex;
    std::unordered_map<uint32_t, EntryBladeState> g_entryBladeStates;

    constexpr float CONTROLLER_SWAMP_RADIUS = 3.0f;
    constexpr float CONTROLLER_SWAMP_DURATION = 5.0f;

    struct ControllerSwampTrapState
    {
        uint32_t trapId = 0;
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;
        bool activated = false;
    };

    std::mutex g_controllerSwampMutex;
    std::unordered_map<uint32_t, ControllerSwampTrapState> g_controllerSwampTraps;
    std::atomic<uint32_t> g_nextControllerSwampTrapId{ 1 };

    constexpr float CONTROLLER_SMOKE_RADIUS = 4.5f;
    constexpr float CONTROLLER_SMOKE_DURATION = 7.0f;

    struct ControllerSmokeState
    {
        uint32_t smokeId = 0;
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;
    };

    std::mutex g_controllerSmokeMutex;
    std::unordered_map<uint32_t, ControllerSmokeState> g_controllerSmokes;
    std::atomic<uint32_t> g_nextControllerSmokeId{ 1 };

    constexpr float CONTROLLER_DECOY_DURATION = 6.0f;

    struct ControllerDecoyState
    {
        uint32_t decoyId = 0;
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;
        bool activated = false;
    };

    std::mutex g_controllerDecoyMutex;
    std::unordered_map<uint32_t, ControllerDecoyState> g_controllerDecoys;
    std::atomic<uint32_t> g_nextControllerDecoyId{ 1 };

    constexpr float CONTROLLER_ULTIMATE_RADIUS = 15.0f;
    constexpr float CONTROLLER_ULTIMATE_DURATION = 10.0f;

    struct ControllerUltimateState
    {
        uint32_t ultimateId = 0;
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;
    };

    std::mutex g_controllerUltimateMutex;
    std::unordered_map<uint32_t, ControllerUltimateState> g_controllerUltimates;
    std::atomic<uint32_t> g_nextControllerUltimateId{ 1 };

    struct SentinelCTrapRuntime
    {
        uint32_t trapId = 0;
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;
        uint8_t ownerTeam = 0;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        float hp = 40.f;
        bool destroyed = false;
    };

    std::mutex g_sentinelCMutex;
    std::unordered_map<uint32_t, SentinelCTrapRuntime> g_sentinelCTraps;
    uint32_t g_nextSentinelCTrapId = 1;

    constexpr float SENTINEL_C_MAX_HP = 40.f;

    uint32_t CreateSentinelCTrapId()
    {
        std::lock_guard<std::mutex> lock(g_sentinelCMutex);
        return g_nextSentinelCTrapId++;
    }

    constexpr float SENTINEL_Q_MAX_HP = 40.f;
    constexpr float SENTINEL_Q_DETECTION_RADIUS = 10.f;
    constexpr float SENTINEL_Q_ZONE_RADIUS = 10.f;
    constexpr float SENTINEL_Q_ZONE_DURATION = 6.f;
    constexpr float SENTINEL_Q_SLOW_MULTIPLIER = 0.5f;

    struct SentinelQTrapRuntime
    {
        uint32_t trapId = 0;
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;
        uint8_t ownerTeam = 0;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        float hp = SENTINEL_Q_MAX_HP;
        bool activated = false;
    };

    std::mutex g_sentinelQMutex;
    std::unordered_map<uint32_t, SentinelQTrapRuntime> g_sentinelQTraps;
    uint32_t g_nextSentinelQTrapId = 1;

    uint32_t CreateSentinelQTrapId()
    {
        std::lock_guard<std::mutex> lock(g_sentinelQMutex);
        return g_nextSentinelQTrapId++;
    }

    constexpr float SENTINEL_E_MAX_HP = 100.f;
    constexpr float SENTINEL_E_PLACE_RANGE = 10.f;
    constexpr float SENTINEL_E_EFFECT_RADIUS = 8.f;
    constexpr float SENTINEL_E_SLOW_DURATION = 4.f;
    constexpr float SENTINEL_E_SLOW_MULTIPLIER = 0.5f;
    constexpr float SENTINEL_E_STUN_DURATION = 3.f;
    constexpr float SENTINEL_E_REARM_SECONDS = 1.f;
    constexpr float SENTINEL_E_RECALL_COOLDOWN = 20.f;
    constexpr float SENTINEL_E_DESTROY_COOLDOWN = 60.f;

    constexpr uint8_t SENTINEL_E_REMOVE_DESTROYED = 1;
    constexpr uint8_t SENTINEL_E_REMOVE_RECALLED = 2;
    constexpr uint8_t SENTINEL_E_REMOVE_ROUND_CLEANUP = 3;

    struct SentinelETrapRuntime
    {
        uint32_t trapId = 0;
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;
        uint8_t ownerTeam = 0;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        float nx = 0.f;
        float ny = 0.f;
        float nz = 1.f;

        float hp = SENTINEL_E_MAX_HP;
        std::chrono::steady_clock::time_point lastActivated{};
    };

    std::mutex g_sentinelEMutex;
    std::unordered_map<uint32_t, SentinelETrapRuntime> g_sentinelETraps;
    std::unordered_map<
        uint64_t,
        std::chrono::steady_clock::time_point
    > g_sentinelECooldowns;
    uint32_t g_nextSentinelETrapId = 1;

    uint64_t MakeSentinelECooldownKey(
        uint32_t roomId,
        uint32_t playerId)
    {
        return
            (static_cast<uint64_t>(roomId) << 32) |
            static_cast<uint64_t>(playerId);
    }

    constexpr float SENTINEL_X_CAST_RANGE = 20.f;
    constexpr float SENTINEL_X_RADIUS = 7.f;
    constexpr float SENTINEL_X_DURATION = 5.f;
    constexpr float SENTINEL_X_PROTECTION_DURATION = 10.f;
    constexpr float SENTINEL_X_SLOW_MULTIPLIER = 0.5f;
    constexpr int SENTINEL_X_TICK_MILLISECONDS = 200;
    constexpr int32_t SENTINEL_X_DAMAGE_PER_TICK = 10;

    struct SentinelXRuntime
    {
        uint32_t ultimateId = 0;
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;
        uint8_t ownerTeam = 0;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    std::mutex g_sentinelXMutex;
    std::unordered_map<uint32_t, SentinelXRuntime> g_sentinelXUltimates;
    std::unordered_map<
        uint64_t,
        std::chrono::steady_clock::time_point
    > g_sentinelXProtectionEnds;
    std::atomic<uint32_t> g_nextSentinelXUltimateId{ 1 };

    uint64_t MakeSentinelXProtectionKey(
        uint32_t roomId,
        uint32_t ownerPlayerId)
    {
        return
            (static_cast<uint64_t>(roomId) << 32) |
            static_cast<uint64_t>(ownerPlayerId);
    }

    bool IsSentinelSkillProtected(
        uint32_t roomId,
        uint32_t ownerPlayerId)
    {
        std::lock_guard<std::mutex> lock(g_sentinelXMutex);

        uint64_t key =
            MakeSentinelXProtectionKey(
                roomId,
                ownerPlayerId
            );
        auto it = g_sentinelXProtectionEnds.find(key);

        if (it == g_sentinelXProtectionEnds.end())
            return false;

        if (std::chrono::steady_clock::now() >= it->second)
        {
            g_sentinelXProtectionEnds.erase(it);
            return false;
        }

        return true;
    }

    struct StoreWeaponData
    {
        int weaponId = 0;
        int price = 0;
        bool purchasable = true;
    };

    struct StoreArmorData
    {
        int armorId = 0;
        int price = 0;
        int barrier = 0;
    };

    const std::unordered_map<int, StoreWeaponData> g_storeWeapons =
    {
        // 보조무기
        { 0,  { 0,  0,    true } }, // P92 기본 권총
        { 1,  { 1,  400,  true } }, // P1911
        { 2,  { 2,  800,  true } }, // R1895

        // SMG
        { 3,  { 3,  1000, true } }, // UZI
        { 4,  { 4,  1500, true } }, // UMP

        // Shotgun
        { 5,  { 5,  900,  true } }, // S1897
        { 6,  { 6,  1800, true } }, // AA-12

        // DMR
        { 7,  { 7,  2000, true } }, // Dragunov

        // AR
        { 8,  { 8,  2500, true } }, // M416
        { 9,  { 9,  2500, true } }, // AK47

        // LMG
        { 10, { 10, 3000, true } }, // M249

        // SR
        { 11, { 11, 1000, true } }, // Kar98
        { 12, { 12, 4000, true } }, // AWM

        // Knife는 상점 구매 불가
        { 13, { 13, 0,    false } }
    };

    const std::unordered_map<int, StoreArmorData> g_storeArmors =
    {
        { 0, { 0, 500,  50 } },  // 경량 베리어
        { 1, { 1, 1000, 100 } }  // 중량 베리어
    };
    std::once_flag g_skillPriceLoadOnce;
    std::unordered_map<int, int> g_skillPrices;

    constexpr const char* SKILL_PRICE_CSV_PATH = "SkillPriceTable.csv";

    void CreateDefaultSkillPriceCsvIfMissing()
    {
        std::ifstream check(SKILL_PRICE_CSV_PATH);
        if (check.good())
            return;

        std::ofstream out(SKILL_PRICE_CSV_PATH);
        out << "slot,price\n";
        out << "C,300\n";
        out << "Q,400\n";
        out << "E,400\n";
        out << "X,700\n";
    }

    void LoadSkillPriceTable()
    {
        CreateDefaultSkillPriceCsvIfMissing();

        std::ifstream file(SKILL_PRICE_CSV_PATH);
        if (!file.is_open())
            return;

        std::string line;
        std::getline(file, line);

        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            std::stringstream ss(line);
            std::string slotText;
            std::string priceText;
            std::getline(ss, slotText, ',');
            std::getline(ss, priceText, ',');

            if (slotText.empty() || priceText.empty())
                continue;

            int slot = -1;
            switch (slotText[0])
            {
            case 'C': slot = 0; break;
            case 'Q': slot = 1; break;
            case 'E': slot = 2; break;
            case 'X': slot = 3; break;
            default: break;
            }

            if (slot < 0)
                continue;

            g_skillPrices[slot] = std::max(0, std::stoi(priceText));
        }
    }

    int GetSkillPrice(int slot)
    {
        std::call_once(g_skillPriceLoadOnce, LoadSkillPriceTable);

        auto it = g_skillPrices.find(slot);
        return it != g_skillPrices.end() ? it->second : -1;
    }
    std::once_flag g_creditRewardLoadOnce;

    constexpr const char* CREDIT_REWARD_CSV_PATH = "CreditRewardTable.csv";

    void CreateDefaultCreditRewardCsvIfMissing()
    {
        std::ifstream check(CREDIT_REWARD_CSV_PATH);

        if (check.good())
            return;

        std::ofstream out(CREDIT_REWARD_CSV_PATH);

        out << "key,credit\n";
        out << "ROUND_START_HALF,800\n";
        out << "KILL,300\n";
        out << "SPIKE_PLANT,300\n";
        out << "ROUND_WIN,2500\n";
        out << "LOSE_STREAK_1,1800\n";
        out << "LOSE_STREAK_2,2100\n";
        out << "LOSE_STREAK_3_PLUS,2400\n";
        out << "SURVIVE_DEFEAT,1000\n";
        out << "OVERTIME,5000\n";

        std::cout << "[CREDIT CSV CREATED] path="
            << CREDIT_REWARD_CSV_PATH
            << std::endl;
    }

    void LoadCreditRewardTable()
    {
        CreateDefaultCreditRewardCsvIfMissing();

        std::ifstream file(CREDIT_REWARD_CSV_PATH);

        if (!file.is_open())
        {
            std::cout << "[CREDIT CSV LOAD FAILED] path="
                << CREDIT_REWARD_CSV_PATH
                << std::endl;
            return;
        }

        std::string line;

        std::getline(file, line);
        while (std::getline(file, line))
        {
            if (line.empty())
                continue;

            std::stringstream ss(line);

            std::string key;
            std::string creditText;

            std::getline(ss, key, ',');
            std::getline(ss, creditText, ',');

            if (key.empty() || creditText.empty())
                continue;

            int credit = std::stoi(creditText);
            g_creditRewards[key] = credit;

            std::cout << "[CREDIT REWARD LOADED] key="
                << key
                << " credit=" << credit
                << std::endl;
        }
    }

    int GetCreditReward(const std::string& key)
    {
        std::call_once(g_creditRewardLoadOnce, LoadCreditRewardTable);

        auto it = g_creditRewards.find(key);

        if (it != g_creditRewards.end())
            return it->second;

        std::cout << "[CREDIT REWARD MISSING] key="
            << key
            << std::endl;

        return 0;
    }

    void TryResolveRoundByElimination(uint32_t roomId);
    void StartRoundEndThread(uint32_t roomId, RoomTeam winnerTeam, bool matchEnd);
    void ResetRound(uint32_t roomId);

    bool IsKnifeWeapon(int weaponId);
    bool IsPistolWeapon(int weaponId);
    bool IsPrimaryWeapon(int weaponId);

    bool FindOwnedWeaponByPredicate(
        Session* session,
        bool (*predicate)(int),
        PlayerWeaponState& outWeapon);

    void FillRoundResetWeaponSlots(
        RoundResetMemberInfo& info,
        Session* session);

    void ClearDroppedWeaponsInRoom(uint32_t roomId);

    void StartRoundTimerThread(uint32_t roomId);
    void ResolveRoundByTimeOut(uint32_t roomId);
    RoomTeam GetDefenseTeam(uint8_t currentRound);

    RoomTeam GetAttackTeam(uint8_t currentRound);

    void BroadcastSpikePickupNotify(uint32_t roomId, uint32_t carrierPlayerId);
    void BroadcastSpikePlanted(uint32_t roomId, float x, float y, float z);

    void StartSpikeTimerThread(uint32_t roomId, uint64_t spikeGeneration);
    void ResolveRoundBySpikeExplode(uint32_t roomId, uint64_t spikeGeneration);

    void ApplyRoundEndCredits(uint32_t roomId, RoomTeam winnerTeam, RoundWinReason winReason);

    void SetAllPlayerCreditsInRoom(uint32_t roomId, int credit);

    void BroadcastSpikeDefuseState(uint32_t roomId, bool halfDefused, uint32_t defuserPlayerId);

    bool IsDroneActiveForPlayer(uint32_t playerId);
    void DestroyDroneForPlayer(uint32_t ownerPlayerId, uint8_t reason);
    void ClearDronesInRoom(uint32_t roomId, uint8_t reason);
    bool IsDroneBlockedAction(Session* session, const char* actionName);

    void ResetMatchRuntimeState(uint32_t roomId)
    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        MatchRuntimeState state{};
        state.currentRound = 1;
        state.scoreA = 0;
        state.scoreB = 0;

        state.loseStreakA = 0;
        state.loseStreakB = 0;

        state.spikeCarrierPlayerId = 0;
        state.spikePlanted = false;

        state.spikeDefuserPlayerId = 0;
        state.spikeHalfDefused = false;
        state.spikePlantGeneration = 0;

        state.buyPhase = true;

        state.isOvertime = false;
        state.roundResolving = false;
        state.matchEnded = false;
        state.startedAtUnix = static_cast<int64_t>(std::time(nullptr));
        state.startedAtSteady = std::chrono::steady_clock::now();

        g_matchStates[roomId] = state;

        std::cout << "[MATCH STATE INIT] roomId="
            << roomId
            << std::endl;
    }

    void BroadcastMatchLoadingInfo(const Room& room);
    void BroadcastMatchStart(const Room& room);

    void BroadcastGameAllReady(const Room& room);
    void BroadcastGamePrepareTimer(const Room& room, uint16_t remainingSeconds);
    void StartGamePrepareTimerThread(uint32_t roomId);

    void BroadcastPickTimer(const Room& room, PickPhase phase, uint16_t remainingSeconds)
    {
        SC_PickTimer out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PICK_TIMER);

        out.phase = static_cast<uint8_t>(phase);
        out.remainingSeconds = remainingSeconds;

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[PICK TIMER BROADCAST] roomId="
            << room.roomId
            << " phase=" << static_cast<int>(phase)
            << " remaining=" << remainingSeconds
            << std::endl;
    }
    std::string WideToUTF8(const wchar_t* wstr)
    {
        if (wstr == nullptr)
            return "";

        int sizeNeeded = WideCharToMultiByte(
            CP_UTF8,
            0,
            wstr,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr
        );

        if (sizeNeeded <= 0)
            return "";

        std::string result;
        result.resize(sizeNeeded);

        int converted = WideCharToMultiByte(
            CP_UTF8,
            0,
            wstr,
            -1,
            &result[0],
            sizeNeeded,
            nullptr,
            nullptr
        );

        if (converted <= 0)
            return "";

        if (!result.empty() && result.back() == '\0')
            result.pop_back();

        return result;
    }

    void FillRoomMemberInfo(RoomMemberInfo& out, const RoomMember& member)
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

    void FillRoomCreateResultPacket(SC_RoomCreateResult& out, const Room& room, uint8_t result, uint32_t receiverPlayerId)
    {
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_CREATE_RESULT);

        out.result = result;
        out.roomId = room.roomId;

        memset(out.roomName, 0, ROOM_NAME_LEN);
        memset(out.mapName, 0, MAP_NAME_LEN);

        strncpy_s(out.roomName, ROOM_NAME_LEN, room.roomName.c_str(), _TRUNCATE);
        strncpy_s(out.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);

        out.isLeader = room.leaderPlayerId == receiverPlayerId ? 1 : 0;
        out.canStart = RoomManager::Instance().CanStartGame(room.roomId) ? 1 : 0;

        uint8_t count = static_cast<uint8_t>(room.members.size());
        if (count > ROOM_MAX_MEMBER_COUNT)
            count = ROOM_MAX_MEMBER_COUNT;

        out.memberCount = count;

        for (uint8_t i = 0; i < count; ++i)
        {
            FillRoomMemberInfo(out.members[i], room.members[i]);
        }
    }

    void FillRoomStatePacket(SC_RoomState& out, const Room& room)
    {
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
            FillRoomMemberInfo(out.members[i], room.members[i]);
        }
    }

    void BroadcastRoomState(const Room& room)
    {
        SC_RoomState out{};
        FillRoomStatePacket(out, room);

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[ROOM STATE BROADCAST] roomId=" << room.roomId
            << " memberCount=" << (int)out.memberCount
            << std::endl;
    }

    void SendRoomMapSetResult(Session* session, RoomMapSetResult result, uint8_t mapId)
    {
        if (session == nullptr)
            return;

        SC_RoomMapSetResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_MAP_SET_RESULT);
        out.result = static_cast<uint8_t>(result);
        out.mapId = mapId;
        session->Send(&out);
    }

    void SendRoomGameStartResult(Session* session, RoomGameStartResult result)
    {
        if (session == nullptr)
            return;

        SC_RoomGameStartResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_GAME_START_RESULT);
        out.result = static_cast<uint8_t>(result);

        session->Send(&out);
    }

    void BroadcastRoomGameStartResult(const Room& room, RoomGameStartResult result)
    {
        SC_RoomGameStartResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_GAME_START_RESULT);
        out.result = static_cast<uint8_t>(result);

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[ROOM GAME START BROADCAST] roomId="
            << room.roomId
            << " result=" << static_cast<int>(result)
            << " memberCount=" << room.members.size()
            << std::endl;
    }

    struct PendingSlotSwap
    {
        uint32_t roomId = 0;

        uint32_t requesterPlayerId = 0;
        uint32_t targetPlayerId = 0;

        RoomTeam requesterTeam = RoomTeam::A;
        uint8_t requesterSlotIndex = 0;

        RoomTeam targetTeam = RoomTeam::A;
        uint8_t targetSlotIndex = 0;
    };

    std::mutex g_pendingSlotSwapMutex;
    std::unordered_map<uint32_t, PendingSlotSwap> g_pendingSlotSwapsByTarget;

    bool AddPendingSlotSwap(const PendingSlotSwap& request)
    {
        std::lock_guard<std::mutex> lock(g_pendingSlotSwapMutex);

        if (g_pendingSlotSwapsByTarget.count(request.targetPlayerId) > 0)
            return false;

        g_pendingSlotSwapsByTarget[request.targetPlayerId] = request;
        return true;
    }

    bool TakePendingSlotSwap(uint32_t targetPlayerId, uint32_t requesterPlayerId, PendingSlotSwap& outRequest)
    {
        std::lock_guard<std::mutex> lock(g_pendingSlotSwapMutex);

        auto it = g_pendingSlotSwapsByTarget.find(targetPlayerId);
        if (it == g_pendingSlotSwapsByTarget.end())
            return false;

        if (it->second.requesterPlayerId != requesterPlayerId)
            return false;

        outRequest = it->second;
        g_pendingSlotSwapsByTarget.erase(it);

        return true;
    }

    void SendRoomSlotChangeResult(Session* session, RoomSlotChangeResult result, RoomTeam targetTeam, uint8_t targetSlotIndex, const std::string& targetNickname = "")
    {
        if (session == nullptr)
            return;

        SC_RoomSlotChangeResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_SLOT_CHANGE_RESULT);

        out.result = static_cast<uint8_t>(result);
        out.targetTeam = static_cast<uint8_t>(targetTeam);
        out.targetSlotIndex = targetSlotIndex;

        memset(out.targetNickname, 0, sizeof(out.targetNickname));
        if (!targetNickname.empty())
            strncpy_s(out.targetNickname, sizeof(out.targetNickname), targetNickname.c_str(), _TRUNCATE);

        session->Send(&out);
    }

    void SendRoomTransferLeaderResult(Session* session, RoomAdminResult result, uint32_t targetPlayerId, const std::string& targetNickname = "")
    {
        if (session == nullptr)
            return;

        SC_RoomTransferLeaderResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_TRANSFER_LEADER_RESULT);

        out.result = static_cast<uint8_t>(result);
        out.targetPlayerId = targetPlayerId;

        memset(out.targetNickname, 0, sizeof(out.targetNickname));
        if (!targetNickname.empty())
            strncpy_s(out.targetNickname, sizeof(out.targetNickname), targetNickname.c_str(), _TRUNCATE);

        session->Send(&out);
    }

    void SendRoomKickResult(Session* session, RoomAdminResult result, uint32_t targetPlayerId, const std::string& targetNickname = "")
    {
        if (session == nullptr)
            return;

        SC_RoomKickResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_KICK_RESULT);

        out.result = static_cast<uint8_t>(result);
        out.targetPlayerId = targetPlayerId;

        memset(out.targetNickname, 0, sizeof(out.targetNickname));
        if (!targetNickname.empty())
            strncpy_s(out.targetNickname, sizeof(out.targetNickname), targetNickname.c_str(), _TRUNCATE);

        session->Send(&out);
    }

    void SendRoomKicked(Session* session, uint32_t roomId, const std::string& kickerNickname)
    {
        if (session == nullptr)
            return;

        SC_RoomKicked out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_KICKED);

        out.roomId = roomId;

        memset(out.kickerNickname, 0, sizeof(out.kickerNickname));
        strncpy_s(out.kickerNickname, sizeof(out.kickerNickname), kickerNickname.c_str(), _TRUNCATE);

        session->Send(&out);
    }

    void FillPickTeamMemberInfo(PickTeamMemberInfo& out, const RoomMember& member, uint32_t receiverPlayerId)
    {
        out.playerId = member.playerId;

        memset(out.nickname, 0, sizeof(out.nickname));
        strncpy_s(out.nickname, sizeof(out.nickname), member.nickname.c_str(), _TRUNCATE);

        out.slotIndex = member.slotIndex;

        out.hoverCharacterId = member.hoverCharacterId;
        out.lockedCharacterId = member.lockedCharacterId;

        out.isLocked = member.isPickLocked ? 1 : 0;
        out.isMe = member.playerId == receiverPlayerId ? 1 : 0;
    }

    void FillPickStatePacket(SC_PickState& out, const Room& room, const RoomMember& receiverMember, uint32_t receiverPlayerId)
    {
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PICK_STATE);

        out.roomId = room.roomId;

        memset(out.mapName, 0, MAP_NAME_LEN);
        strncpy_s(out.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);

        out.myTeam = static_cast<uint8_t>(receiverMember.team);

        if (receiverMember.team == RoomTeam::A)
        {
            out.firstRole = static_cast<uint8_t>(PickRole::ATTACK);
            out.secondRole = static_cast<uint8_t>(PickRole::DEFENSE);
        }
        else if (receiverMember.team == RoomTeam::B)
        {
            out.firstRole = static_cast<uint8_t>(PickRole::DEFENSE);
            out.secondRole = static_cast<uint8_t>(PickRole::ATTACK);
        }
        else
        {
            out.firstRole = static_cast<uint8_t>(PickRole::ATTACK);
            out.secondRole = static_cast<uint8_t>(PickRole::DEFENSE);
        }

        out.remainingSeconds = room.pickRemainingSeconds;;
        out.blueScore = 0;
        out.redScore = 0;

        uint8_t count = 0;

        for (uint8_t slot = 0; slot < 3; ++slot)
        {
            for (const RoomMember& member : room.members)
            {
                if (member.team != receiverMember.team)
                    continue;

                if (member.slotIndex != slot)
                    continue;

                if (count >= 3)
                    break;

                FillPickTeamMemberInfo(
                    out.members[count],
                    member,
                    receiverPlayerId
                );

                ++count;
                break;
            }
        }

        out.memberCount = count;
    }

    bool SendPickState(Session* session)
    {
        if (session == nullptr)
            return false;

        uint32_t roomId = session->GetCurrentRoomId();

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            std::cout << "[PICK STATE FAIL] room not found roomId="
                << roomId
                << std::endl;
            return false;
        }

        RoomMember receiverMember{};
        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            receiverMember))
        {
            std::cout << "[PICK STATE FAIL] member not found playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            return false;
        }

        SC_PickState out{};
        FillPickStatePacket(
            out,
            room,
            receiverMember,
            session->GetPlayerId()
        );

        session->Send(&out);

        std::cout << "[PICK STATE SENT] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << " myTeam=" << static_cast<int>(out.myTeam)
            << " memberCount=" << static_cast<int>(out.memberCount)
            << std::endl;

        return true;
    }

    void BroadcastPickStateToTeam(const Room& room, RoomTeam team)
    {
        for (const RoomMember& member : room.members)
        {
            if (member.team != team)
                continue;

            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            RoomMember receiverMember{};
            if (!RoomManager::Instance().GetMember(
                room.roomId,
                member.playerId,
                receiverMember))
            {
                continue;
            }

            SC_PickState out{};
            FillPickStatePacket(
                out,
                room,
                receiverMember,
                member.playerId
            );

            target->Send(&out);
        }

        std::cout << "[PICK STATE BROADCAST TEAM] roomId="
            << room.roomId
            << " team=" << static_cast<int>(team)
            << std::endl;
    }

    void SendPickLockResult(Session* session, PickLockResult result, int characterId)
    {
        if (session == nullptr)
            return;

        SC_PickLockResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PICK_LOCK_RESULT);

        out.result = static_cast<uint8_t>(result);
        out.characterId = characterId;

        session->Send(&out);
    }

    void BroadcastPickStateToRoom(const Room& room)
    {
        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            RoomMember receiverMember{};
            if (!RoomManager::Instance().GetMember(
                room.roomId,
                member.playerId,
                receiverMember))
            {
                continue;
            }

            SC_PickState out{};
            FillPickStatePacket(
                out,
                room,
                receiverMember,
                member.playerId
            );

            target->Send(&out);
        }

        std::cout << "[PICK STATE BROADCAST ROOM] roomId="
            << room.roomId
            << std::endl;
    }

    void StartPickTimerThread(uint32_t roomId)
    {
        std::thread([roomId]()
            {
                // 1) 20초 픽
                for (int remain = PICK_TIME_SECONDS; remain >= 0; --remain)
                {
                    Room updatedRoom{};

                    bool ok = RoomManager::Instance().SetPickRemainingSeconds(
                        roomId,
                        static_cast<uint16_t>(remain),
                        updatedRoom
                    );

                    if (!ok)
                        return;

                    BroadcastPickTimer(updatedRoom, PickPhase::PICKING, static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                // 2) 자동 픽 확정
                Room finalPickRoom{};
                bool forceOk = RoomManager::Instance().ForceLockMissingPicks(roomId, finalPickRoom);

                if (!forceOk)
                    return;

                BroadcastPickStateToRoom(finalPickRoom);

                std::cout << "[PICK AUTO LOCK DONE] roomId="
                    << roomId
                    << std::endl;

                // 3) 10초 준비 시간
                for (int remain = PREPARE_TIME_SECONDS; remain >= 0; --remain)
                {
                    Room room{};
                    if (!RoomManager::Instance().GetRoom(roomId, room))
                        return;

                    BroadcastPickTimer(
                        room,
                        PickPhase::PREPARE,
                        static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                // 4) 준비 끝나면 VS/로딩 화면 정보 전송
                Room loadingRoom{};
                if (!RoomManager::Instance().GetRoom(roomId, loadingRoom))
                    return;

                BroadcastMatchLoadingInfo(loadingRoom);

                std::cout << "[MATCH LOADING WAIT CLIENT LOAD] roomId=" << roomId << std::endl;

            }).detach();
    }

    void FillMatchLoadingMemberInfo(MatchLoadingMemberInfo& out, const RoomMember& member)
    {
        out.playerId = member.playerId;

        memset(out.nickname, 0, sizeof(out.nickname));
        strncpy_s(out.nickname, sizeof(out.nickname), member.nickname.c_str(), _TRUNCATE);

        out.slotIndex = member.slotIndex;
        out.characterId = member.lockedCharacterId;
        out.rankImageId = member.rankImageId;
    }

    void BroadcastMatchLoadingInfo(const Room& room)
    {
        SC_MatchLoadingInfo out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_MATCH_LOADING_INFO);

        out.roomId = room.roomId;

        out.attackTeam = 0;   // A
        out.defenseTeam = 1;  // B

        out.attackCount = 0;
        out.defenseCount = 0;

        std::vector<RoomMember> aMembers;
        std::vector<RoomMember> bMembers;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::A)
                aMembers.push_back(member);
            else if (member.team == RoomTeam::B)
                bMembers.push_back(member);
        }

        std::sort(
            aMembers.begin(),
            aMembers.end(),
            [](const RoomMember& a, const RoomMember& b)
            {
                return a.slotIndex < b.slotIndex;
            }
        );

        std::sort(
            bMembers.begin(),
            bMembers.end(),
            [](const RoomMember& a, const RoomMember& b)
            {
                return a.slotIndex < b.slotIndex;
            }
        );

        for (size_t i = 0; i < aMembers.size() && i < MATCH_TEAM_MEMBER_COUNT; ++i)
        {
            FillMatchLoadingMemberInfo(out.attackMembers[i], aMembers[i]);
            out.attackCount++;
        }

        for (size_t i = 0; i < bMembers.size() && i < MATCH_TEAM_MEMBER_COUNT; ++i)
        {
            FillMatchLoadingMemberInfo(out.defenseMembers[i], bMembers[i]);
            out.defenseCount++;
        }

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[MATCH LOADING INFO BROADCAST] roomId="
            << room.roomId
            << " attackCount=" << static_cast<int>(out.attackCount)
            << " defenseCount=" << static_cast<int>(out.defenseCount)
            << std::endl;
    }

    void BroadcastMatchStart(const Room& room)
    {
        SC_MatchStart out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_MATCH_START);
        out.roomId = room.roomId;

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[MATCH START BROADCAST] roomId="
            << room.roomId
            << std::endl;
    }

    void FillGameReadyMemberInfo(GameReadyMemberInfo& out, const RoomMember& member)
    {
        out.playerId = member.playerId;

        memset(out.userId, 0, sizeof(out.userId));
        strncpy_s(
            out.userId,
            sizeof(out.userId),
            member.userId.c_str(),
            _TRUNCATE
        );

        memset(out.nickname, 0, sizeof(out.nickname));
        strncpy_s(
            out.nickname,
            sizeof(out.nickname),
            member.nickname.c_str(),
            _TRUNCATE
        );

        out.team = static_cast<uint8_t>(member.team);
        out.slotIndex = member.slotIndex;
        out.characterId = member.lockedCharacterId;
        out.rankImageId = member.rankImageId;

        std::array<int, WEAPON_SKIN_WEAPON_COUNT> equippedSkins{};
        WeaponSkinManager::Instance().GetEquippedSkins(member.userId, member.lockedCharacterId, equippedSkins);

        for (int weaponId = 0; weaponId < MAX_WEAPON_SKIN_LOADOUT_COUNT; ++weaponId)
            out.weaponSkinIds[weaponId] = equippedSkins[weaponId];
    }

    void ResetGameLoadRuntimeState(uint32_t roomId)
    {
        std::lock_guard<std::mutex> lock(g_gameLoadReadyMutex);

        g_gameLoadReadyPlayers.erase(roomId);
        g_gamePrepareStartedRooms.erase(roomId);

        std::cout << "[GAME LOAD RUNTIME RESET] roomId="
            << roomId
            << std::endl;
    }

    bool MarkGameLoadReadyAndCheckAll(uint32_t roomId, uint32_t playerId, Room& outRoom)
    {
        if (!RoomManager::Instance().GetRoom(roomId, outRoom))
            return false;

        bool requesterExists = false;
        int playMemberCount = 0;

        for (const RoomMember& member : outRoom.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.slotIndex >= 3)
                continue;

            playMemberCount++;

            if (member.playerId == playerId)
                requesterExists = true;
        }

        if (!requesterExists)
            return false;

        if (playMemberCount != 6)
            return false;

        std::lock_guard<std::mutex> lock(g_gameLoadReadyMutex);

        if (g_gamePrepareStartedRooms.count(roomId) > 0)
            return false;

        g_gameLoadReadyPlayers[roomId].insert(playerId);

        for (const RoomMember& member : outRoom.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.slotIndex >= 3)
                continue;

            if (g_gameLoadReadyPlayers[roomId].count(member.playerId) == 0)
                return false;
        }

        g_gamePrepareStartedRooms.insert(roomId);
        return true;
    }

    void FillHPUpdatePacket(SC_HPUpdate& out, Session* target)
    {
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_HP_UPDATE);

        out.playerId = target->GetPlayerId();

        out.hp = target->GetHP();
        out.barrier = target->GetBarrier();

        out.maxHp = target->GetMaxHP();
        out.maxBarrier = target->GetMaxBarrier();
    }

    void BroadcastInitialHPToRoom(const Room& room)
    {
        for (const RoomMember& hpOwner : room.members)
        {
            if (hpOwner.team == RoomTeam::OBSERVER)
                continue;

            auto ownerSession = SessionManager::Instance().Find(hpOwner.playerId);
            if (ownerSession == nullptr)
                continue;

            SC_HPUpdate hpPkt{};
            FillHPUpdatePacket(hpPkt, ownerSession.get());

            for (const RoomMember& receiver : room.members)
            {
                if (receiver.team == RoomTeam::OBSERVER)
                    continue;

                auto receiverSession = SessionManager::Instance().Find(receiver.playerId);
                if (receiverSession == nullptr)
                    continue;

                receiverSession->Send(&hpPkt);
            }
        }

        std::cout << "[INITIAL HP BROADCAST] roomId="
            << room.roomId
            << " memberCount=" << room.members.size()
            << std::endl;
    }

    void BroadcastScoreboardUpdate(uint32_t roomId)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        for (const RoomMember& receiver : room.members)
        {
            if (receiver.team == RoomTeam::OBSERVER)
                continue;

            auto receiverSession = SessionManager::Instance().Find(receiver.playerId);
            if (receiverSession == nullptr)
                continue;

            SC_ScoreboardUpdate out{};

            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SCOREBOARD_UPDATE);

            out.roomId = roomId;
            out.myTeam = static_cast<uint8_t>(receiver.team);

            uint8_t count = 0;

            for (const RoomMember& member : room.members)
            {
                if (member.team == RoomTeam::OBSERVER)
                    continue;

                if (member.slotIndex >= 3)
                    continue;

                if (count >= GAME_MEMBER_COUNT)
                    break;

                auto memberSession = SessionManager::Instance().Find(member.playerId);
                if (memberSession == nullptr)
                    continue;

                const PlayerMatchStats& stats = memberSession->GetMatchStats();

                ScoreboardMemberInfo& info = out.members[count];

                info.playerId = member.playerId;
                info.team = static_cast<uint8_t>(member.team);
                info.slotIndex = member.slotIndex;
                info.characterId = member.lockedCharacterId;

                memset(info.nickname, 0, sizeof(info.nickname));
                strncpy_s(info.nickname, sizeof(info.nickname), member.nickname.c_str(), _TRUNCATE);

                info.kills = stats.kills;
                info.deaths = stats.deaths;
                info.assists = stats.assists;
                info.credits = memberSession->GetCredits();

                count++;
            }

            out.memberCount = count;

            receiverSession->Send(&out);
        }

        std::cout << "[SCOREBOARD UPDATE BROADCAST] roomId="
            << roomId
            << " memberCount=" << room.members.size()
            << std::endl;
    }

    void BroadcastGameAllReady(const Room& room)
    {
        ClearDronesInRoom(room.roomId, 3);

        ResetMatchRuntimeState(room.roomId);
        ClearDroppedWeaponsInRoom(room.roomId);

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            auto playerSession = SessionManager::Instance().Find(member.playerId);
            if (playerSession == nullptr)
                continue;

            playerSession->ResetMatchStats();
            playerSession->ResetCredits();
            playerSession->SetCredits(GetCreditReward("ROUND_START_HALF"));

            playerSession->ClearLastDamager();
            playerSession->ClearAssistDamageRecords();
            playerSession->ResetSkills();
            playerSession->SetActiveCharacterId(
                member.lockedCharacterId >= 0 ? member.lockedCharacterId : 0
            );
            playerSession->InitRoundState();
            playerSession->SetState(SessionState::IN_GAME);
        }

        SC_GameAllReady out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_GAME_ALL_READY);

        out.roomId = room.roomId;
        strncpy_s(out.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);
        out.prepareSeconds = PREPARE_TIME_SECONDS;

        uint8_t count = static_cast<uint8_t>(room.members.size());

        if (count > GAME_READY_MEMBER_COUNT)
            count = GAME_READY_MEMBER_COUNT;

        out.memberCount = count;

        for (uint8_t i = 0; i < count; ++i)
        {
            FillGameReadyMemberInfo(out.members[i], room.members[i]);
        }

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);

            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        BroadcastInitialHPToRoom(room);
        BroadcastScoreboardUpdate(room.roomId);
    }

    bool IsBuyPhase(uint32_t roomId)
    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);

        if (it == g_matchStates.end())
            return false;

        return it->second.buyPhase &&
            !it->second.roundResolving &&
            !it->second.matchEnded;
    }

    void SetBuyPhase(uint32_t roomId, bool value)
    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);

        if (it == g_matchStates.end())
            return;

        it->second.buyPhase = value;

        std::cout << "[BUY PHASE SET] roomId="
            << roomId
            << " value=" << value
            << std::endl;
    }

    void BroadcastGamePrepareTimer(const Room& room, uint16_t remainingSeconds)
    {
        SC_GamePrepareTimer out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_GAME_PREPARE_TIMER);
        out.remainingSeconds = remainingSeconds;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout << "[GAME PREPARE TIMER BROADCAST] roomId="
            << room.roomId
            << " remaining=" << remainingSeconds
            << std::endl;
    }

    void BroadcastPacketToRoomInternal(
        uint32_t roomId,
        void* packet)
    {
        if (roomId == 0 || packet == nullptr)
            return;

        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            std::cout
                << "[BROADCAST ROOM FAIL] room not found roomId="
                << roomId
                << std::endl;

            return;
        }

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            auto receiver =
                SessionManager::Instance().Find(member.playerId);

            if (receiver == nullptr)
                continue;

            receiver->Send(packet);
        }
    }

    void BroadcastSentinelCDestroy(
        uint32_t roomId,
        uint32_t trapId)
    {
        SC_SentinelCDestroy out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_C_DESTROY
            );
        out.trapId = trapId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SENTINEL C DESTROY BROADCAST] roomId=" << roomId
            << " trapId=" << trapId
            << std::endl;
    }

    void ClearSentinelCTrapsInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> trapIds;

        {
            std::lock_guard<std::mutex> lock(g_sentinelCMutex);

            for (auto it = g_sentinelCTraps.begin();
                it != g_sentinelCTraps.end();)
            {
                if (it->second.roomId == roomId)
                {
                    trapIds.push_back(it->first);
                    it = g_sentinelCTraps.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t trapId : trapIds)
        {
            BroadcastSentinelCDestroy(roomId, trapId);
        }

        if (!trapIds.empty())
        {
            std::cout
                << "[SENTINEL C CLEAR ROOM] roomId=" << roomId
                << " count=" << trapIds.size()
                << std::endl;
        }
    }

    void BroadcastSentinelQDestroy(
        uint32_t roomId,
        uint32_t trapId)
    {
        SC_SentinelQDestroy out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_Q_DESTROY
            );
        out.trapId = trapId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SENTINEL Q DESTROY BROADCAST] roomId=" << roomId
            << " trapId=" << trapId
            << std::endl;
    }

    void ClearSentinelQTrapsInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> trapIds;

        {
            std::lock_guard<std::mutex> lock(g_sentinelQMutex);

            for (auto it = g_sentinelQTraps.begin();
                it != g_sentinelQTraps.end();)
            {
                if (it->second.roomId == roomId)
                {
                    trapIds.push_back(it->first);
                    it = g_sentinelQTraps.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t trapId : trapIds)
        {
            BroadcastSentinelQDestroy(roomId, trapId);
        }

        if (!trapIds.empty())
        {
            std::cout
                << "[SENTINEL Q CLEAR ROOM] roomId=" << roomId
                << " count=" << trapIds.size()
                << std::endl;
        }
    }

    void BroadcastSentinelEDestroy(
        uint32_t roomId,
        uint32_t trapId,
        uint8_t reason,
        float cooldownSeconds)
    {
        SC_SentinelEDestroy out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_E_DESTROY
            );
        out.trapId = trapId;
        out.reason = reason;
        out.cooldownSeconds = cooldownSeconds;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SENTINEL E DESTROY BROADCAST] roomId=" << roomId
            << " trapId=" << trapId
            << " reason=" << static_cast<int>(reason)
            << " cooldown=" << cooldownSeconds
            << std::endl;
    }

    constexpr int MATCHMAKING_PLAYER_COUNT = 6;
    constexpr int MATCHMAKING_MIN_WAIT_SECONDS = 5;
    constexpr int MATCHMAKING_RANKED_INITIAL_RANGE = 2;
    constexpr int MATCHMAKING_NORMAL_INITIAL_RANGE = 3;
    constexpr int MATCHMAKING_RANKED_MAX_RANGE = 8;
    constexpr int MATCHMAKING_NORMAL_MAX_RANGE = 25;
    constexpr int MATCHMAKING_EXPAND_SECONDS = 10;

    uint8_t GetInitialMatchmakingRange(uint8_t mode)
    {
        return static_cast<uint8_t>(mode == 1
            ? MATCHMAKING_RANKED_INITIAL_RANGE
            : MATCHMAKING_NORMAL_INITIAL_RANGE);
    }

    uint8_t GetMatchmakingTierRange(
        const MatchmakingEntry& entry,
        std::chrono::steady_clock::time_point now)
    {
        int elapsedSeconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                now - entry.queuedAt).count());
        int initialRange = GetInitialMatchmakingRange(entry.mode);
        int maxRange = entry.mode == 1
            ? MATCHMAKING_RANKED_MAX_RANGE
            : MATCHMAKING_NORMAL_MAX_RANGE;
        int expanded = initialRange +
            elapsedSeconds / MATCHMAKING_EXPAND_SECONDS;
        return static_cast<uint8_t>(std::min(expanded, maxRange));
    }

    uint16_t GetMatchmakingElapsedSeconds(
        const MatchmakingEntry& entry,
        std::chrono::steady_clock::time_point now)
    {
        int64_t elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(
                now - entry.queuedAt).count();
        return static_cast<uint16_t>(
            std::max<int64_t>(0, std::min<int64_t>(elapsed, 65535)));
    }

    void SendMatchmakingState(
        Session* session,
        uint8_t result,
        uint8_t state,
        uint8_t mode,
        uint16_t elapsedSeconds,
        uint8_t tierRange,
        uint32_t roomId)
    {
        if (session == nullptr)
            return;

        SC_TempGameStartResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_TEMP_GAME_START_RESULT);
        out.result = result;
        out.state = state;
        out.mode = mode;
        out.elapsedSeconds = elapsedSeconds;
        out.tierRange = tierRange;
        out.roomId = roomId;
        session->Send(&out);
    }

    bool BuildMatchmakingRoomMember(
        Session* session,
        RoomMember& outMember)
    {
        if (session == nullptr || session->GetState() != SessionState::IN_LOBBY)
            return false;

        PlayerData data{};
        if (!UserManager::Instance().GetPlayerData(
                session->GetUserId(),
                data))
        {
            return false;
        }

        outMember = RoomMember{};
        outMember.playerId = session->GetPlayerId();
        outMember.userId = session->GetUserId();
        outMember.nickname = data.nickname;
        outMember.level = data.level;
        outMember.playerIconId = data.playerIconId;
        outMember.rankImageId = data.rankImageId;
        return true;
    }

    bool AreMatchmakingEntriesCompatible(
        const MatchmakingEntry& lhs,
        const MatchmakingEntry& rhs,
        std::chrono::steady_clock::time_point now)
    {
        int allowedRange = std::min(
            static_cast<int>(GetMatchmakingTierRange(lhs, now)),
            static_cast<int>(GetMatchmakingTierRange(rhs, now)));
        int ratingDifference = std::abs(
            lhs.matchmakingRating - rhs.matchmakingRating);
        if (ratingDifference > allowedRange * 100)
            return false;

        if (lhs.mode == 1)
        {
            int visibleTierDifference = std::abs(
                lhs.visibleTierId - rhs.visibleTierId);
            if (visibleTierDifference > allowedRange)
                return false;
        }

        return true;
    }

    int CountSelectedPlayers(uint32_t mask)
    {
        int count = 0;
        while (mask != 0)
        {
            count += static_cast<int>(mask & 1u);
            mask >>= 1u;
        }
        return count;
    }

    bool DoesTeamMaskSplitParty(
        const std::vector<MatchmakingEntry>& group,
        uint32_t teamAMask)
    {
        for (size_t i = 0; i < group.size(); ++i)
        {
            if (group[i].partyId == 0 || group[i].partySize <= 1)
                continue;

            bool iOnTeamA = (teamAMask & (1u << i)) != 0;
            for (size_t j = i + 1; j < group.size(); ++j)
            {
                if (group[j].partyId != group[i].partyId)
                    continue;
                bool jOnTeamA = (teamAMask & (1u << j)) != 0;
                if (iOnTeamA != jOnTeamA)
                    return true;
            }
        }
        return false;
    }

    int CalculateTeamPartyRatingBonus(
        const std::vector<MatchmakingEntry>& group,
        uint32_t teamAMask,
        bool forTeamA)
    {
        std::unordered_map<uint64_t, int> partySizes;
        for (size_t i = 0; i < group.size(); ++i)
        {
            bool isOnTeamA = (teamAMask & (1u << i)) != 0;
            if (isOnTeamA != forTeamA ||
                group[i].partyId == 0 ||
                group[i].partySize <= 1)
            {
                continue;
            }

            partySizes[group[i].partyId] = std::max(
                partySizes[group[i].partyId],
                static_cast<int>(group[i].partySize));
        }

        int bonus = 0;
        for (const auto& pair : partySizes)
        {
            if (pair.second == 2)
                bonus += 35;
            else if (pair.second >= 3)
                bonus += 70;
        }
        return std::min(bonus, 70);
    }

    bool BuildBalancedMatchmakingGroup(
        const std::vector<MatchmakingEntry>& group,
        std::vector<MatchmakingEntry>& outBalancedGroup,
        double& outTeamAExpectedWinRate)
    {
        if (group.size() != MATCHMAKING_PLAYER_COUNT)
            return false;

        double bestCost = std::numeric_limits<double>::max();
        uint32_t bestTeamAMask = 0;
        double bestExpectedWinRate = 0.5;
        uint32_t maskLimit = 1u << MATCHMAKING_PLAYER_COUNT;

        for (uint32_t teamAMask = 0; teamAMask < maskLimit; ++teamAMask)
        {
            if ((teamAMask & 1u) == 0 ||
                CountSelectedPlayers(teamAMask) != 3 ||
                DoesTeamMaskSplitParty(group, teamAMask))
            {
                continue;
            }

            double teamATotal = 0.0;
            double teamBTotal = 0.0;
            for (size_t i = 0; i < group.size(); ++i)
            {
                if ((teamAMask & (1u << i)) != 0)
                    teamATotal += group[i].matchmakingRating;
                else
                    teamBTotal += group[i].matchmakingRating;
            }

            int teamAPartyBonus = CalculateTeamPartyRatingBonus(
                group,
                teamAMask,
                true);
            int teamBPartyBonus = CalculateTeamPartyRatingBonus(
                group,
                teamAMask,
                false);
            double teamAEffectiveAverage =
                teamATotal / 3.0 + teamAPartyBonus;
            double teamBEffectiveAverage =
                teamBTotal / 3.0 + teamBPartyBonus;
            double expectedWinRate =
                RankRatingCalculator::CalculateExpectedWinRate(
                    teamAEffectiveAverage,
                    teamBEffectiveAverage);
            double partyMismatch = std::abs(
                teamAPartyBonus - teamBPartyBonus);
            double cost =
                std::abs(0.5 - expectedWinRate) * 1000.0 +
                partyMismatch * 0.25;

            if (cost < bestCost)
            {
                bestCost = cost;
                bestTeamAMask = teamAMask;
                bestExpectedWinRate = expectedWinRate;
            }
        }

        if (bestTeamAMask == 0)
            return false;

        std::vector<MatchmakingEntry> teamA;
        std::vector<MatchmakingEntry> teamB;
        for (size_t i = 0; i < group.size(); ++i)
        {
            if ((bestTeamAMask & (1u << i)) != 0)
                teamA.push_back(group[i]);
            else
                teamB.push_back(group[i]);
        }

        outBalancedGroup.clear();
        outBalancedGroup.reserve(MATCHMAKING_PLAYER_COUNT);
        for (size_t i = 0; i < teamA.size(); ++i)
        {
            outBalancedGroup.push_back(teamA[i]);
            outBalancedGroup.push_back(teamB[i]);
        }
        outTeamAExpectedWinRate = bestExpectedWinRate;
        return true;
    }

    bool ContainsCompleteMatchmakingParties(
        const std::vector<MatchmakingEntry>& group)
    {
        std::unordered_map<uint64_t, int> selectedPartyMembers;
        std::unordered_map<uint64_t, int> declaredPartySizes;
        for (const MatchmakingEntry& entry : group)
        {
            if (entry.partyId == 0 || entry.partySize <= 1)
                continue;
            selectedPartyMembers[entry.partyId]++;
            declaredPartySizes[entry.partyId] = std::max(
                declaredPartySizes[entry.partyId],
                static_cast<int>(entry.partySize));
        }

        for (const auto& pair : declaredPartySizes)
        {
            if (selectedPartyMembers[pair.first] != pair.second)
                return false;
        }
        return true;
    }

    double GetAllowedWinRateDeviation(
        const std::vector<MatchmakingEntry>& group,
        std::chrono::steady_clock::time_point now)
    {
        int longestWait = 0;
        for (const MatchmakingEntry& entry : group)
        {
            longestWait = std::max(
                longestWait,
                static_cast<int>(GetMatchmakingElapsedSeconds(entry, now)));
        }

        double expansion = std::clamp(
            static_cast<double>(
                std::max(0, longestWait - MATCHMAKING_MIN_WAIT_SECONDS)) /
                120.0,
            0.0,
            1.0);
        return 0.05 + expansion * 0.07;
    }

    bool TryTakeMatchmakingGroup(
        uint8_t mode,
        std::vector<MatchmakingEntry>& outGroup)
    {
        std::lock_guard<std::recursive_mutex> lock(g_matchmakingMutex);
        auto now = std::chrono::steady_clock::now();

        g_matchmakingQueue.erase(
            std::remove_if(
                g_matchmakingQueue.begin(),
                g_matchmakingQueue.end(),
                [](const MatchmakingEntry& entry)
                {
                    auto session =
                        SessionManager::Instance().Find(entry.playerId);
                    return session == nullptr ||
                        session->GetState() != SessionState::IN_LOBBY;
                }),
            g_matchmakingQueue.end());

        for (size_t anchorIndex = 0;
            anchorIndex < g_matchmakingQueue.size();
            ++anchorIndex)
        {
            const MatchmakingEntry& anchor =
                g_matchmakingQueue[anchorIndex];
            if (anchor.mode != mode ||
                GetMatchmakingElapsedSeconds(anchor, now) <
                    MATCHMAKING_MIN_WAIT_SECONDS)
            {
                continue;
            }

            // Search whole parties, not individual players. A greedy first six
            // can get stuck on three duos even when two solos are waiting.
            std::vector<std::vector<MatchmakingEntry>> parties;
            std::unordered_set<uint64_t> seenParties;
            auto appendParty = [&](uint64_t partyId)
            {
                if (!seenParties.insert(partyId).second) return;
                std::vector<MatchmakingEntry> party;
                for (const auto& entry : g_matchmakingQueue)
                    if (entry.partyId == partyId && entry.mode == mode &&
                        GetMatchmakingElapsedSeconds(entry, now) >= MATCHMAKING_MIN_WAIT_SECONDS)
                        party.push_back(entry);
                if (!party.empty() && party.size() == party.front().partySize)
                    parties.push_back(party);
            };
            appendParty(anchor.partyId);
            if (parties.empty()) continue;
            for (const auto& entry : g_matchmakingQueue)
                if (parties.size() < 64) appendParty(entry.partyId);

            std::vector<MatchmakingEntry> selectedGroup = parties.front();
            std::vector<MatchmakingEntry> balancedGroup;
            int attempts = 0;
            auto search = [&](auto&& recurse, size_t next) -> bool
            {
                if (++attempts > 4096) return false;
                if (selectedGroup.size() == MATCHMAKING_PLAYER_COUNT)
                {
                    double expected = .5;
                    return ContainsCompleteMatchmakingParties(selectedGroup) &&
                        BuildBalancedMatchmakingGroup(selectedGroup, balancedGroup, expected) &&
                        std::abs(expected - .5) <= GetAllowedWinRateDeviation(balancedGroup, now);
                }
                for (size_t i = next; i < parties.size(); ++i)
                {
                    if (selectedGroup.size() + parties[i].size() > MATCHMAKING_PLAYER_COUNT) continue;
                    bool compatible = true;
                    for (const auto& candidate : parties[i])
                        for (const auto& selected : selectedGroup)
                            compatible &= AreMatchmakingEntriesCompatible(candidate, selected, now);
                    if (!compatible) continue;
                    const auto before = selectedGroup.size();
                    selectedGroup.insert(selectedGroup.end(), parties[i].begin(), parties[i].end());
                    if (recurse(recurse, i + 1)) return true;
                    selectedGroup.resize(before);
                }
                return false;
            };
            if (!search(search, 1)) continue;

            std::unordered_set<uint32_t> selectedPlayerIds;
            for (const MatchmakingEntry& entry : balancedGroup)
            {
                outGroup.push_back(entry);
                selectedPlayerIds.insert(entry.playerId);
            }

            g_matchmakingQueue.erase(
                std::remove_if(
                    g_matchmakingQueue.begin(),
                    g_matchmakingQueue.end(),
                    [&selectedPlayerIds](const MatchmakingEntry& entry)
                    {
                        return selectedPlayerIds.count(entry.playerId) > 0;
                    }),
                g_matchmakingQueue.end());
            return true;
        }

        return false;
    }

    bool CreateMatchmakingRoom(
        const std::vector<MatchmakingEntry>& group)
    {
        if (group.size() != MATCHMAKING_PLAYER_COUNT)
            return false;

        std::vector<std::shared_ptr<Session>> sessions;
        std::vector<RoomMember> members;
        sessions.reserve(group.size());
        members.reserve(group.size());

        for (const MatchmakingEntry& entry : group)
        {
            auto session = SessionManager::Instance().Find(entry.playerId);
            RoomMember member{};
            if (!BuildMatchmakingRoomMember(session.get(), member))
                return false;
            member.matchmakingPartyId = entry.partyId;
            member.matchmakingPartySize = entry.partySize;
            sessions.push_back(session);
            members.push_back(member);
        }

        uint8_t mode = group[0].mode == 1 ? 1 : 0;
        std::string roomName = mode == 1
            ? "Ranked Match"
            : "Normal Match";
        uint32_t roomId = RoomManager::Instance().CreateRoom(
            roomName,
            ChooseRandomMatchMapName(),
            members[0].nickname,
            members[0],
            MATCHMAKING_PLAYER_COUNT,
            mode,
            true);
        if (roomId == 0)
            return false;

        Room room{};
        auto rollback = [&]()
        {
            for (const auto& member : members)
            {
                bool deleted = false; Room unused{};
                RoomManager::Instance().LeaveRoom(roomId, member.playerId, deleted, unused);
            }
            for (const auto& peer : sessions)
                if (peer->GetCurrentRoomId() == roomId)
                { peer->SetCurrentRoomId(0); peer->SetState(SessionState::IN_LOBBY); }
        };
        for (size_t i = 1; i < members.size(); ++i)
        {
            if (!RoomManager::Instance().EnterRoom(roomId, members[i], room))
            { rollback(); return false; }
        }

        for (const std::shared_ptr<Session>& session : sessions)
        {
            if (session->IsDisconnected()) { rollback(); return false; }
            session->SetCurrentRoomId(roomId);
            session->SetState(SessionState::IN_ROOM);
        }

        Room startedRoom{};
        if (!RoomManager::Instance().StartGame(
                roomId,
                members[0].playerId,
                startedRoom))
        {
            rollback();
            return false;
        }

        ResetGameLoadRuntimeState(roomId);
        {
            std::lock_guard<std::mutex> lock(g_resultLeaveMutex);
            g_resultLeavePlayers.erase(roomId);
        }

        auto now = std::chrono::steady_clock::now();
        for (size_t i = 0; i < sessions.size(); ++i)
        {
            SendMatchmakingState(
                sessions[i].get(),
                0,
                1,
                mode,
                GetMatchmakingElapsedSeconds(group[i], now),
                GetMatchmakingTierRange(group[i], now),
                roomId);
        }

        uint32_t teamAMask = 0;
        for (size_t i = 0; i < group.size(); i += 2)
            teamAMask |= 1u << i;
        double teamATotal = 0.0;
        double teamBTotal = 0.0;
        for (size_t i = 0; i < group.size(); ++i)
        {
            if ((teamAMask & (1u << i)) != 0)
                teamATotal += group[i].matchmakingRating;
            else
                teamBTotal += group[i].matchmakingRating;
        }
        double teamAEffectiveAverage = teamATotal / 3.0 +
            CalculateTeamPartyRatingBonus(group, teamAMask, true);
        double teamBEffectiveAverage = teamBTotal / 3.0 +
            CalculateTeamPartyRatingBonus(group, teamAMask, false);
        int expectedWinRatePermille = static_cast<int>(std::lround(
            RankRatingCalculator::CalculateExpectedWinRate(
                teamAEffectiveAverage,
                teamBEffectiveAverage) * 1000.0));

        std::cout << "[MATCHMAKING FOUND] roomId=" << roomId
            << " mode=" << static_cast<int>(mode)
            << " players=" << sessions.size()
            << " teamAMmr=" << static_cast<int>(std::lround(
                teamAEffectiveAverage))
            << " teamBMmr=" << static_cast<int>(std::lround(
                teamBEffectiveAverage))
            << " teamAExpectedPermille=" << expectedWinRatePermille
            << std::endl;
        return true;
    }

    #include "LobbySocial.inl"

    void RunMatchmakingWorker()
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            TickSocialLobby();
            for (uint8_t mode = 0; mode <= 1; ++mode)
            {
                while (true)
                {
                    std::lock_guard<std::recursive_mutex> socialLock(g_matchmakingMutex);
                    std::vector<MatchmakingEntry> group;
                    if (!TryTakeMatchmakingGroup(mode, group))
                        break;

                    if (!BeginReadyMatch(group))
                    {
                        for (const MatchmakingEntry& entry : group)
                        {
                            auto session =
                                SessionManager::Instance().Find(entry.playerId);
                            SendMatchmakingState(
                                session.get(),
                                1,
                                3,
                                entry.mode,
                                0,
                                GetInitialMatchmakingRange(entry.mode),
                                0);
                        }
                    }
                }
            }
        }
    }

    void EnsureMatchmakingWorkerStarted()
    {
        std::call_once(
            g_matchmakingWorkerOnce,
            []()
            {
                std::thread(RunMatchmakingWorker).detach();
            });
    }

    void ClearSentinelETrapsInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> trapIds;

        {
            std::lock_guard<std::mutex> lock(g_sentinelEMutex);

            for (auto it = g_sentinelETraps.begin();
                it != g_sentinelETraps.end();)
            {
                if (it->second.roomId == roomId)
                {
                    trapIds.push_back(it->first);
                    it = g_sentinelETraps.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            for (auto it = g_sentinelECooldowns.begin();
                it != g_sentinelECooldowns.end();)
            {
                uint32_t cooldownRoomId =
                    static_cast<uint32_t>(it->first >> 32);

                if (cooldownRoomId == roomId)
                    it = g_sentinelECooldowns.erase(it);
                else
                    ++it;
            }
        }

        for (uint32_t trapId : trapIds)
        {
            BroadcastSentinelEDestroy(
                roomId,
                trapId,
                SENTINEL_E_REMOVE_ROUND_CLEANUP,
                0.f
            );
        }

        if (!trapIds.empty())
        {
            std::cout
                << "[SENTINEL E CLEAR ROOM] roomId=" << roomId
                << " count=" << trapIds.size()
                << std::endl;
        }
    }

    void BroadcastSentinelXDestroy(
        uint32_t roomId,
        uint32_t ultimateId)
    {
        SC_SentinelXDestroy out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_X_DESTROY
            );
        out.ultimateId = ultimateId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SENTINEL X DESTROY BROADCAST] roomId=" << roomId
            << " ultimateId=" << ultimateId
            << std::endl;
    }

    void ClearSentinelXUltimatesInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> ultimateIds;

        {
            std::lock_guard<std::mutex> lock(g_sentinelXMutex);

            for (auto it = g_sentinelXUltimates.begin();
                it != g_sentinelXUltimates.end();)
            {
                if (it->second.roomId == roomId)
                {
                    ultimateIds.push_back(it->first);
                    it = g_sentinelXUltimates.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            for (auto it = g_sentinelXProtectionEnds.begin();
                it != g_sentinelXProtectionEnds.end();)
            {
                uint32_t protectionRoomId =
                    static_cast<uint32_t>(it->first >> 32);

                if (protectionRoomId == roomId)
                    it = g_sentinelXProtectionEnds.erase(it);
                else
                    ++it;
            }
        }

        for (uint32_t ultimateId : ultimateIds)
            BroadcastSentinelXDestroy(roomId, ultimateId);

        if (!ultimateIds.empty())
        {
            std::cout
                << "[SENTINEL X CLEAR ROOM] roomId=" << roomId
                << " count=" << ultimateIds.size()
                << std::endl;
        }
    }

    void BroadcastScannerSpawn(
        uint32_t roomId,
        uint32_t scannerId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float vx,
        float vy,
        float vz)
    {
        SC_ScannerSpawn out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_SCANNER_SPAWN);

        out.scannerId = scannerId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.vx = vx;
        out.vy = vy;
        out.vz = vz;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SCANNER SPAWN BROADCAST] "
            << "roomId=" << roomId
            << " scannerId=" << scannerId
            << " owner=" << ownerPlayerId
            << " pos=(" << x << "," << y << "," << z << ")"
            << " velocity=(" << vx << "," << vy << "," << vz << ")"
            << std::endl;
    }

    void BroadcastScannerAnchored(
        uint32_t roomId,
        uint32_t scannerId,
        float x,
        float y,
        float z)
    {
        SC_ScannerAnchored out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_SCANNER_ANCHORED);

        out.scannerId = scannerId;

        out.x = x;
        out.y = y;
        out.z = z;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SCANNER ANCHORED BROADCAST] "
            << "roomId=" << roomId
            << " scannerId=" << scannerId
            << " pos=(" << x << "," << y << "," << z << ")"
            << std::endl;
    }

    void BroadcastScannerZap(
        uint32_t roomId,
        uint32_t scannerId,
        uint32_t targetPlayerId,
        uint8_t stack,
        float duration)
    {
        SC_ScannerZap out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_SCANNER_ZAP);

        out.scannerId = scannerId;
        out.targetPlayerId = targetPlayerId;
        out.stack = stack;
        out.duration = duration;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SCANNER ZAP BROADCAST] "
            << "roomId=" << roomId
            << " scannerId=" << scannerId
            << " target=" << targetPlayerId
            << " stack=" << static_cast<int>(stack)
            << " duration=" << duration
            << std::endl;
    }

    void BroadcastScannerDestroy(
        uint32_t roomId,
        uint32_t scannerId)
    {
        SC_ScannerDestroy out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_SCANNER_DESTROY);

        out.scannerId = scannerId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SCANNER DESTROY BROADCAST] "
            << "roomId=" << roomId
            << " scannerId=" << scannerId
            << std::endl;
    }

    void BroadcastEntryDashCast(
        uint32_t roomId,
        uint32_t playerId,
        float dirX,
        float dirY,
        float dirZ,
        float dashForce,
        float dashDuration)
    {
        SC_EntryDashCast out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ENTRY_DASH_CAST
                );

        out.playerId =
            playerId;

        out.dirX = dirX;
        out.dirY = dirY;
        out.dirZ = dirZ;

        out.dashForce =
            dashForce;

        out.dashDuration =
            dashDuration;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[ENTRY DASH CAST BROADCAST] roomId="
            << roomId
            << " playerId="
            << playerId
            << " dir=("
            << dirX << ", " << dirY << ", " << dirZ
            << ") force="
            << dashForce
            << " duration="
            << dashDuration
            << std::endl;
    }

    void BroadcastEntryDashEnd(
        uint32_t roomId,
        uint32_t playerId)
    {
        SC_EntryDashEnd out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ENTRY_DASH_END
                );

        out.playerId =
            playerId;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[ENTRY DASH END BROADCAST] roomId="
            << roomId
            << " playerId="
            << playerId
            << std::endl;
    }

    void BroadcastEntrySmokeState(
        uint32_t roomId,
        uint32_t smokeId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float rotX,
        float rotY,
        float rotZ,
        float rotW)
    {
        SC_EntrySmokeState out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ENTRY_SMOKE_STATE
                );

        out.smokeId =
            smokeId;

        out.ownerPlayerId =
            ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.rotX = rotX;
        out.rotY = rotY;
        out.rotZ = rotZ;
        out.rotW = rotW;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );
    }

    void BroadcastEntrySmokeActivate(
        uint32_t roomId,
        uint32_t smokeId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float nx,
        float ny,
        float nz,
        float radius,
        float duration)
    {
        SC_EntrySmokeActivate out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ENTRY_SMOKE_ACTIVATE
                );

        out.smokeId =
            smokeId;

        out.ownerPlayerId =
            ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.nx = nx;
        out.ny = ny;
        out.nz = nz;

        out.radius =
            radius;

        out.duration =
            duration;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[ENTRY SMOKE ACTIVATE BROADCAST] roomId="
            << roomId
            << " smokeId="
            << smokeId
            << " owner="
            << ownerPlayerId
            << " pos=("
            << x << ", " << y << ", " << z << ")"
            << std::endl;
    }

    void BroadcastEntrySmokeDestroy(
        uint32_t roomId,
        uint32_t smokeId)
    {
        SC_EntrySmokeDestroy out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ENTRY_SMOKE_DESTROY
                );

        out.smokeId =
            smokeId;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[ENTRY SMOKE DESTROY BROADCAST] roomId="
            << roomId
            << " smokeId="
            << smokeId
            << std::endl;
    }


    void BroadcastEntrySmokeSpawn(
        uint32_t roomId,
        uint32_t smokeId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float vx,
        float vy,
        float vz)
    {
        SC_EntrySmokeSpawn out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ENTRY_SMOKE_SPAWN
                );

        out.smokeId =
            smokeId;

        out.ownerPlayerId =
            ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.vx = vx;
        out.vy = vy;
        out.vz = vz;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[ENTRY SMOKE SPAWN BROADCAST] roomId="
            << roomId
            << " smokeId="
            << smokeId
            << " owner="
            << ownerPlayerId
            << std::endl;
    }

    void StartEntrySmokeAutoActivateTimer(
        uint32_t roomId,
        uint32_t smokeId)
    {
        std::thread(
            [roomId, smokeId]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(2500)
                );

                uint32_t ownerPlayerId = 0;

                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;

                float nx = 0.0f;
                float ny = 1.0f;
                float nz = 0.0f;

                bool shouldActivate = false;

                {
                    std::lock_guard<std::mutex> lock(g_entrySmokeMutex);

                    auto it =
                        g_entrySmokes.find(smokeId);

                    if (it == g_entrySmokes.end())
                        return;

                    EntrySmokeState& state =
                        it->second;

                    if (state.roomId != roomId)
                        return;

                    if (state.active)
                        return;

                    state.active = true;

                    ownerPlayerId =
                        state.ownerPlayerId;

                    x = state.x;
                    y = state.y;
                    z = state.z;

                    nx = state.nx;
                    ny = state.ny;
                    nz = state.nz;

                    shouldActivate = true;
                }

                if (!shouldActivate)
                    return;

                std::cout
                    << "[ENTRY SMOKE AUTO ACTIVATE] roomId="
                    << roomId
                    << " smokeId="
                    << smokeId
                    << std::endl;

                BroadcastEntrySmokeActivate(
                    roomId,
                    smokeId,
                    ownerPlayerId,
                    x,
                    y,
                    z,
                    nx,
                    ny,
                    nz,
                    ENTRY_SMOKE_RADIUS,
                    ENTRY_SMOKE_DURATION
                );

                std::thread(
                    [roomId, smokeId]()
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(
                                static_cast<int>(
                                    ENTRY_SMOKE_DURATION * 1000.0f
                                    )
                            )
                        );

                        bool shouldDestroy = false;

                        {
                            std::lock_guard<std::mutex> lock(g_entrySmokeMutex);

                            auto it =
                                g_entrySmokes.find(smokeId);

                            if (it != g_entrySmokes.end())
                            {
                                g_entrySmokes.erase(it);
                                shouldDestroy = true;
                            }
                        }

                        if (shouldDestroy)
                        {
                            BroadcastEntrySmokeDestroy(
                                roomId,
                                smokeId
                            );
                        }
                    }
                ).detach();
            }
        ).detach();
    }

    

    
    void ClearEntrySmokesInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> removedSmokeIds;

        {
            std::lock_guard<std::mutex> lock(g_entrySmokeMutex);

            for (auto it = g_entrySmokes.begin();
                it != g_entrySmokes.end();)
            {
                if (it->second.roomId == roomId)
                {
                    removedSmokeIds.push_back(
                        it->first
                    );

                    it =
                        g_entrySmokes.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t smokeId : removedSmokeIds)
        {
            BroadcastEntrySmokeDestroy(
                roomId,
                smokeId
            );
        }

        if (!removedSmokeIds.empty())
        {
            std::cout
                << "[ENTRY SMOKE CLEAR ROOM] roomId="
                << roomId
                << " count="
                << removedSmokeIds.size()
                << std::endl;
        }
    }

    void BroadcastDroneSpawn(
        uint32_t roomId,
        uint32_t droneId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float yaw,
        float pitch)
    {
        SC_DroneSpawn out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_DRONE_SPAWN);

        out.droneId = droneId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.yaw = yaw;
        out.pitch = pitch;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[DRONE SPAWN BROADCAST] "
            << "roomId=" << roomId
            << " droneId=" << droneId
            << " owner=" << ownerPlayerId
            << " pos=(" << x << "," << y << "," << z << ")"
            << " yaw=" << yaw
            << " pitch=" << pitch
            << std::endl;
    }

    void BroadcastDroneState(
        uint32_t roomId,
        uint32_t droneId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float yaw,
        float pitch)
    {
        SC_DroneState out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_DRONE_STATE);

        out.droneId = droneId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.yaw = yaw;
        out.pitch = pitch;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void BroadcastDroneDestroy(
        uint32_t roomId,
        uint32_t droneId,
        uint32_t ownerPlayerId,
        uint8_t reason)
    {
        SC_DroneDestroy out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_DRONE_DESTROY);

        out.droneId = droneId;
        out.ownerPlayerId = ownerPlayerId;
        out.reason = reason;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[DRONE DESTROY BROADCAST] "
            << "roomId=" << roomId
            << " droneId=" << droneId
            << " owner=" << ownerPlayerId
            << " reason=" << static_cast<int>(reason)
            << std::endl;
    }

    void BroadcastDroneHpUpdate(
        uint32_t roomId,
        uint32_t droneId,
        uint32_t ownerPlayerId,
        int32_t hp,
        int32_t maxHp)
    {
        SC_DroneHpUpdate out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_DRONE_HP_UPDATE);

        out.droneId = droneId;
        out.ownerPlayerId = ownerPlayerId;
        out.hp = hp;
        out.maxHp = maxHp;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[DRONE HP UPDATE BROADCAST] "
            << "roomId=" << roomId
            << " droneId=" << droneId
            << " owner=" << ownerPlayerId
            << " hp=" << hp
            << "/" << maxHp
            << std::endl;
    }

    void BroadcastRoundTimer(uint32_t roomId, uint16_t remainingSeconds)
    {
        SC_RoundTimer out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROUND_TIMER);
        out.remainingSeconds = remainingSeconds;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[ROUND TIMER BROADCAST] roomId="
            << roomId
            << " remaining=" << remainingSeconds
            << std::endl;
    }

    void StartGamePrepareTimerThread(uint32_t roomId)
    {
        std::thread([roomId]()
            {
                for (int remain = GAME_PREPARE_SECONDS; remain >= 0; --remain)
                {
                    Room room{};
                    if (!RoomManager::Instance().GetRoom(roomId, room))
                        return;

                    BroadcastGamePrepareTimer(
                        room,
                        static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                std::cout << "[GAME PREPARE DONE] roomId="
                    << roomId
                    << std::endl;

                SetBuyPhase(roomId, false);

                StartRoundTimerThread(roomId);
            }
        ).detach();
    }

    void SendAmmoUpdate(
        Session* session,
        const PlayerWeaponState& weaponState)
    {
        if (session == nullptr)
            return;

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            return;

        SC_AmmoUpdate out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_AMMO_UPDATE
                );

        out.playerId =
            session->GetPlayerId();

        out.weaponId =
            weaponState.weaponId;

        out.magAmmo =
            weaponState.magAmmo;

        out.reserveAmmo =
            weaponState.reserveAmmo;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );
    }

    void BroadcastRoundResult(uint32_t roomId, RoomTeam winnerTeam, uint8_t scoreA, uint8_t scoreB, uint8_t currentRound, bool matchEnd, RoundWinReason winReason)
    {
        SC_RoundResult out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROUND_RESULT);

        out.winnerTeam = static_cast<uint8_t>(winnerTeam);
        out.scoreA = scoreA;
        out.scoreB = scoreB;
        out.currentRound = currentRound;
        out.matchEnd = matchEnd ? 1 : 0;
        out.winReason = static_cast<uint8_t>(winReason);
        out.delaySeconds = ROUND_END_DELAY_SECONDS;

        BroadcastPacketToRoomInternal(roomId, &out);
        ClearSentinelCTrapsInRoom(roomId);
        ClearSentinelQTrapsInRoom(roomId);
        ClearSentinelETrapsInRoom(roomId);
        ClearSentinelXUltimatesInRoom(roomId);

        std::cout << "[ROUND RESULT] roomId="
            << roomId
            << " winnerTeam=" << static_cast<int>(out.winnerTeam)
            << " scoreA=" << static_cast<int>(scoreA)
            << " scoreB=" << static_cast<int>(scoreB)
            << " currentRound=" << static_cast<int>(currentRound)
            << " matchEnd=" << matchEnd
            << std::endl;
    }

    int GetKdaScore(const PlayerMatchStats& stats)
    {
        return stats.kills * 100 + stats.assists * 50 - stats.deaths * 20;
    }

    void BroadcastMatchResultDetail(uint32_t roomId, RoomTeam winnerTeam, uint8_t scoreA, uint8_t scoreB)
    {
        // End-of-round callbacks must not settle the same match twice.
        // Keep rank, MVP history and wallet settlement behind one completion gate.
        static std::mutex completionMutex;
        std::lock_guard<std::mutex> completionLock(completionMutex);
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;
        if (room.matchResultReady) return;

        SC_MatchResultDetail out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_MATCH_RESULT_DETAIL);

        out.winnerTeam = static_cast<uint8_t>(winnerTeam);
        out.scoreA = scoreA;
        out.scoreB = scoreB;
        out.matchType = room.matchType == 1 ? 1 : 0;

        int bestScore = -999999;
        uint32_t mvpPlayerId = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.team != winnerTeam)
                continue;

            auto s = SessionManager::Instance().Find(member.playerId);
            if (s == nullptr)
                continue;

            int score = GetKdaScore(s->GetMatchStats());

            if (score > bestScore)
            {
                bestScore = score;
                mvpPlayerId = member.playerId;
            }
        }

        uint8_t count = 0;
        std::vector<CareerMatchPlayer> careerPlayers;
        std::vector<std::string> resultUserIds;
        std::unordered_map<std::string, CareerOverview> rankOverviewBefore;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (count >= GAME_MEMBER_COUNT)
                break;

            auto s = SessionManager::Instance().Find(member.playerId);
            if (s == nullptr)
                continue;

            const PlayerMatchStats& stats = s->GetMatchStats();

            MatchResultMemberInfo& info = out.members[count];

            info.playerId = member.playerId;
            info.team = static_cast<uint8_t>(member.team);
            info.slotIndex = member.slotIndex;
            info.characterId = member.lockedCharacterId;

            memset(info.nickname, 0, sizeof(info.nickname));
            strncpy_s(info.nickname, sizeof(info.nickname), member.nickname.c_str(), _TRUNCATE);

            info.kills = stats.kills;
            info.deaths = stats.deaths;
            info.assists = stats.assists;

            info.firstHalfKills = stats.firstHalfKills;
            info.firstHalfDeaths = stats.firstHalfDeaths;
            info.firstHalfAssists = stats.firstHalfAssists;

            info.secondHalfKills = stats.secondHalfKills;
            info.secondHalfDeaths = stats.secondHalfDeaths;
            info.secondHalfAssists = stats.secondHalfAssists;

            info.overtimeKills = stats.overtimeKills;
            info.overtimeDeaths = stats.overtimeDeaths;
            info.overtimeAssists = stats.overtimeAssists;

            info.isMvp = member.playerId == mvpPlayerId ? 1 : 0;

            if (room.matchType == 1)
            {
                CareerOverview beforeOverview{};
                if (CareerManager::Instance().GetOverview(
                        member.userId,
                        beforeOverview))
                {
                    rankOverviewBefore[member.userId] = beforeOverview;
                    info.rankPointsBefore = beforeOverview.stats.rankPoints;
                    info.rankPointsAfter = beforeOverview.stats.rankPoints;
                    info.rankTierIdBefore = beforeOverview.tier.tierId;
                    info.rankTierIdAfter = beforeOverview.tier.tierId;
                }
            }

            if (room.isMatchmade)
            {
                bool won = member.team == winnerTeam;
                CareerMatchReward reward =
                    CareerManager::Instance().GrantMatchReward(
                        member.userId,
                        won);
                info.freeCurrencyReward = reward.freeCurrencyReward;
                info.victoryCurrencyReward = reward.victoryCurrencyReward;
                info.totalFreeCurrency = reward.totalFreeCurrency;
                info.totalVictoryCurrency = reward.totalVictoryCurrency;
            }

            CareerMatchPlayer careerPlayer{};
            careerPlayer.userId = member.userId;
            careerPlayer.nickname = member.nickname;
            careerPlayer.team = static_cast<int>(member.team);
            careerPlayer.characterId = member.lockedCharacterId;
            careerPlayer.combatScore = GetKdaScore(stats);
            careerPlayer.attackScore = stats.kills * 100;
            careerPlayer.skillScore = 0;
            careerPlayer.teamworkScore = stats.assists * 50;
            careerPlayer.kills = stats.kills;
            careerPlayer.deaths = stats.deaths;
            careerPlayer.assists = stats.assists;
            careerPlayer.damage = stats.damageDealt;
            careerPlayer.headshotKills = stats.headshotKills;
            careerPlayer.meleeKills = stats.meleeKills;
            careerPlayer.spikePlants = stats.spikePlants;
            careerPlayer.spikeDefuses = stats.spikeDefuses;
            careerPlayer.isMvp = member.playerId == mvpPlayerId;
            careerPlayer.partyId = member.matchmakingPartyId;
            careerPlayer.partySize = member.matchmakingPartySize;
            careerPlayers.push_back(careerPlayer);
            resultUserIds.push_back(member.userId);

            count++;
        }

        out.memberCount = count;

        int64_t startedAtUnix = static_cast<int64_t>(std::time(nullptr));
        int durationSeconds = 0;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);
            auto stateIt = g_matchStates.find(roomId);
            if (stateIt != g_matchStates.end())
            {
                startedAtUnix = stateIt->second.startedAtUnix;
                durationSeconds = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() -
                        stateIt->second.startedAtSteady
                    ).count()
                );
            }
        }

        std::vector<CareerRankAdjustment> rankAdjustments;
        CareerManager::Instance().RecordMatch(
            room.mapName,
            startedAtUnix,
            durationSeconds,
            scoreA,
            scoreB,
            static_cast<int>(winnerTeam),
            careerPlayers,
            room.isMatchmade ? room.matchType : 2,
            room.isMatchmade,
            &rankAdjustments
        );

        for (const std::string& userId : resultUserIds)
        {
            std::shared_ptr<Session> recentOwner =
                SessionManager::Instance().FindByUserId(userId);
            if (recentOwner == nullptr)
                continue;

            for (const std::string& peerUserId : resultUserIds)
                recentOwner->AddRecentPlayerUserId(peerUserId);
        }

        std::unordered_map<std::string, CareerRankAdjustment>
            rankAdjustmentsByUser;
        for (const CareerRankAdjustment& adjustment : rankAdjustments)
            rankAdjustmentsByUser[adjustment.userId] = adjustment;

        if (room.matchType == 1)
        {
            for (size_t i = 0;
                i < resultUserIds.size() && i < count;
                ++i)
            {
                MatchResultMemberInfo& info = out.members[i];
                CareerOverview afterOverview{};
                if (!CareerManager::Instance().GetOverview(
                        resultUserIds[i],
                        afterOverview))
                {
                    continue;
                }

                auto beforeIt = rankOverviewBefore.find(resultUserIds[i]);
                int rankPointsBefore = beforeIt != rankOverviewBefore.end()
                    ? beforeIt->second.stats.rankPoints
                    : afterOverview.stats.rankPoints;
                int rankTierBefore = beforeIt != rankOverviewBefore.end()
                    ? beforeIt->second.tier.tierId
                    : afterOverview.tier.tierId;

                info.rankPointsBefore = rankPointsBefore;
                info.rankPointsAfter = afterOverview.stats.rankPoints;
                info.rankPointDelta =
                    info.rankPointsAfter - info.rankPointsBefore;
                info.rankTierIdBefore = rankTierBefore;
                info.rankTierIdAfter = afterOverview.tier.tierId;

                auto adjustmentIt =
                    rankAdjustmentsByUser.find(resultUserIds[i]);
                if (adjustmentIt != rankAdjustmentsByUser.end())
                {
                    info.rankExpectedWinRatePermille =
                        adjustmentIt->second.expectedWinRatePermille;
                    info.rankModifierFlags =
                        adjustmentIt->second.modifierFlags;
                }
            }
        }

        std::vector<CareerRankingEntry> refreshedRankings =
            CareerManager::Instance().GetRankings(
                std::numeric_limits<size_t>::max()
            );

        std::vector<std::pair<std::string, int>> refreshedRankImages;
        refreshedRankImages.reserve(refreshedRankings.size());
        for (const CareerRankingEntry& ranking : refreshedRankings)
        {
            refreshedRankImages.emplace_back(
                ranking.userId,
                ranking.tier.rankImageId
            );
        }
        UserManager::Instance().SetRankImages(refreshedRankImages);

        RoomManager::Instance().MarkMatchResultReady(roomId);
        // Send cosmetics first on the same ordered connection. No account/schema migration.
        SC_MatchAppearance appearance{};
        appearance.header = {sizeof(appearance), static_cast<uint16_t>(PacketType::PKT_SC_MATCH_APPEARANCE)};
        appearance.roomId = roomId;
        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER || appearance.memberCount >= 6) continue;
            PlayerData player{}; ChipCraftRecord craft{};
            if (!UserManager::Instance().GetPlayerData(member.userId, player) ||
                !ProfileManager::Instance().GetChipCraft(member.userId, craft)) continue;
            auto& target = appearance.members[appearance.memberCount++];
            target.playerId = member.playerId; target.playerIconId = player.playerIconId;
            target.logoBorderId = player.logoBorderId; target.rankImageId = player.rankImageId; target.level = player.level;
            target.preset.cardId = static_cast<uint8_t>(player.playerIconId);
            target.preset.caseId = craft.preset.caseId; target.preset.medalCount = craft.preset.medalCount;
            for (size_t i = 0; i < craft.preset.medals.size(); ++i)
            {
                const auto& from = craft.preset.medals[i]; auto& to = target.preset.medals[i];
                to.medalId = from.id; to.x = from.x; to.y = from.y;
                to.rotation = from.rotation; to.scale = from.scale; to.flipped = from.flipped ? 1 : 0;
            }
        }
        BroadcastPacketToRoomInternal(roomId, &appearance);
        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[MATCH RESULT DETAIL] roomId="
            << roomId
            << " winnerTeam=" << static_cast<int>(out.winnerTeam)
            << " scoreA=" << static_cast<int>(out.scoreA)
            << " scoreB=" << static_cast<int>(out.scoreB)
            << " memberCount=" << static_cast<int>(out.memberCount)
            << " mvpPlayerId=" << mvpPlayerId
            << std::endl;
    }

    bool IsATeamAttackRound(uint8_t currentRound)
    {
        // 1 ~ SIDE_SWAP_AFTER_ROUND 라운드: A팀 공격
        if (currentRound <= SIDE_SWAP_AFTER_ROUND)
            return true;

        // SIDE_SWAP_AFTER_ROUND + 1 ~ OVERTIME_START_ROUND - 1 라운드: A팀 수비
        if (currentRound < OVERTIME_START_ROUND)
            return false;

        // OVERTIME_START_ROUND 라운드부터 듀스, 매 라운드 공수 교대
        return ((currentRound - OVERTIME_START_ROUND) % 2) == 0;
    }

    RoundWinReason GetEliminationWinReason(RoomTeam winnerTeam, uint8_t currentRound)
    {
        bool aTeamAttack = IsATeamAttackRound(currentRound);

        RoomTeam attackTeam = aTeamAttack ? RoomTeam::A : RoomTeam::B;
        RoomTeam defenseTeam = aTeamAttack ? RoomTeam::B : RoomTeam::A;

        if (winnerTeam == attackTeam)
            return RoundWinReason::ATTACK_ELIMINATION;

        if (winnerTeam == defenseTeam)
            return RoundWinReason::DEFENSE_ELIMINATION;

        return RoundWinReason::NONE;
    }

    RoomTeam GetSpawnRoleTeam(RoomTeam originalTeam, uint8_t currentRound)
    {
        bool aTeamAttack = IsATeamAttackRound(currentRound);

        // 주의:
        // 여기서 반환하는 team은 실제 팀 변경이 아니다.
        // 클라이언트 MapInfo에서 어떤 스폰 포인트를 쓸지 알려주는 "스폰 역할 team"이다.
        // 0 = AttackSpawnPoints
        // 1 = DefendSpawnPoints

        if (originalTeam == RoomTeam::A)
        {
            return aTeamAttack ? RoomTeam::A : RoomTeam::B;
        }

        if (originalTeam == RoomTeam::B)
        {
            return aTeamAttack ? RoomTeam::B : RoomTeam::A;
        }

        return originalTeam;
    }

    void BroadcastControllerUltimateSpawn(
        uint32_t roomId,
        uint32_t ultimateId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float radius,
        float duration)
    {
        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        SC_ControllerUltimateSpawn out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CONTROLLER_ULTIMATE_SPAWN);

        out.ultimateId = ultimateId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.radius = radius;
        out.duration = duration;

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout
            << "[CONTROLLER ULTIMATE SPAWN BROADCAST] roomId=" << roomId
            << " ultimateId=" << ultimateId
            << " owner=" << ownerPlayerId
            << " pos=(" << x << ", " << y << ", " << z << ")"
            << " radius=" << radius
            << " duration=" << duration
            << std::endl;
    }

    void BroadcastControllerUltimateDestroy(uint32_t roomId, uint32_t ultimateId)
    {
        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        SC_ControllerUltimateDestroy out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CONTROLLER_ULTIMATE_DESTROY);
        out.ultimateId = ultimateId;

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout
            << "[CONTROLLER ULTIMATE DESTROY BROADCAST] roomId=" << roomId
            << " ultimateId=" << ultimateId
            << std::endl;
    }

    void ScheduleControllerUltimateDestroy(uint32_t roomId, uint32_t ultimateId, float duration)
    {
        std::thread([roomId, ultimateId, duration]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<int>(duration * 1000.0f)
                    )
                );

                bool shouldBroadcast = false;

                {
                    std::lock_guard<std::mutex> lock(g_controllerUltimateMutex);

                    auto it = g_controllerUltimates.find(ultimateId);
                    if (it != g_controllerUltimates.end())
                    {
                        g_controllerUltimates.erase(it);
                        shouldBroadcast = true;
                    }
                }

                if (shouldBroadcast)
                    BroadcastControllerUltimateDestroy(roomId, ultimateId);

            }).detach();
    }

    void BroadcastControllerSwampSpawn(
        uint32_t roomId,
        uint32_t trapId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float vx,
        float vy,
        float vz)
    {
        SC_ControllerSwampSpawn out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_CONTROLLER_SWAMP_SPAWN
                );

        out.trapId = trapId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.vx = vx;
        out.vy = vy;
        out.vz = vz;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[CONTROLLER SWAMP SPAWN BROADCAST] "
            << "roomId=" << roomId
            << " trapId=" << trapId
            << " owner=" << ownerPlayerId
            << std::endl;
    }

    void BroadcastControllerSwampActivate(
        uint32_t roomId,
        uint32_t trapId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float normalX,
        float normalY,
        float normalZ)
    {
        SC_ControllerSwampActivate out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_CONTROLLER_SWAMP_ACTIVATE
                );

        out.trapId = trapId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.normalX = normalX;
        out.normalY = normalY;
        out.normalZ = normalZ;

        out.radius = CONTROLLER_SWAMP_RADIUS;
        out.duration = CONTROLLER_SWAMP_DURATION;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[CONTROLLER SWAMP ACTIVATE BROADCAST] "
            << "roomId=" << roomId
            << " trapId=" << trapId
            << " owner=" << ownerPlayerId
            << std::endl;
    }

    void BroadcastControllerDecoyDestroy(uint32_t roomId, uint32_t decoyId)
    {
        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        SC_ControllerDecoyDestroy out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CONTROLLER_DECOY_DESTROY);
        out.decoyId = decoyId;

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout
            << "[CONTROLLER DECOY DESTROY BROADCAST] roomId=" << roomId
            << " decoyId=" << decoyId
            << std::endl;
    }


    void BroadcastControllerSwampDestroy(
        uint32_t roomId,
        uint32_t trapId)
    {
        SC_ControllerSwampDestroy out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_CONTROLLER_SWAMP_DESTROY
                );

        out.trapId = trapId;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[CONTROLLER SWAMP DESTROY BROADCAST] "
            << "roomId=" << roomId
            << " trapId=" << trapId
            << std::endl;
    }

    void ClearControllerDecoysInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> removedIds;

        {
            std::lock_guard<std::mutex> lock(g_controllerDecoyMutex);

            for (auto it = g_controllerDecoys.begin(); it != g_controllerDecoys.end();)
            {
                if (it->second.roomId == roomId)
                {
                    removedIds.push_back(it->first);
                    it = g_controllerDecoys.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t decoyId : removedIds)
        {
            BroadcastControllerDecoyDestroy(roomId, decoyId);
        }

        std::cout
            << "[CONTROLLER DECOY CLEAR ROOM] roomId="
            << roomId
            << " count="
            << removedIds.size()
            << std::endl;
    }

    void ClearControllerUltimatesInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> removedIds;

        {
            std::lock_guard<std::mutex> lock(g_controllerUltimateMutex);

            for (auto it = g_controllerUltimates.begin(); it != g_controllerUltimates.end();)
            {
                if (it->second.roomId == roomId)
                {
                    removedIds.push_back(it->first);
                    it = g_controllerUltimates.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t ultimateId : removedIds)
        {
            BroadcastControllerUltimateDestroy(roomId, ultimateId);
        }

        std::cout
            << "[CONTROLLER ULTIMATE CLEAR ROOM] roomId="
            << roomId
            << " count="
            << removedIds.size()
            << std::endl;
    }

    void ClearControllerSwampsInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> trapIds;

        {
            std::lock_guard<std::mutex> lock(
                g_controllerSwampMutex
            );

            for (auto it = g_controllerSwampTraps.begin();
                it != g_controllerSwampTraps.end();)
            {
                if (it->second.roomId == roomId)
                {
                    trapIds.push_back(
                        it->first
                    );

                    it =
                        g_controllerSwampTraps.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t trapId : trapIds)
        {
            BroadcastControllerSwampDestroy(
                roomId,
                trapId
            );
        }

        std::cout
            << "[CONTROLLER SWAMP CLEAR ROOM] roomId="
            << roomId
            << std::endl;
    }

    void BroadcastEntryBladeState(
        uint32_t roomId,
        uint32_t playerId,
        bool active,
        uint8_t bladeCount,
        float duration)
    {
        SC_EntryBladeState out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ENTRY_BLADE_STATE
                );

        out.playerId =
            playerId;

        out.active =
            active ? 1 : 0;

        out.bladeCount =
            bladeCount;

        out.duration =
            duration;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[ENTRY BLADE STATE BROADCAST] roomId="
            << roomId
            << " playerId="
            << playerId
            << " active="
            << active
            << " bladeCount="
            << static_cast<int>(bladeCount)
            << " duration="
            << duration
            << std::endl;
    }

    void BroadcastEntryBladeFire(
        uint32_t roomId,
        uint32_t shooterPlayerId,
        uint32_t targetPlayerId,
        bool hit,
        uint8_t remainingBlades,
        float startX,
        float startY,
        float startZ,
        float endX,
        float endY,
        float endZ)
    {
        SC_EntryBladeFire out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ENTRY_BLADE_FIRE
                );

        out.shooterPlayerId =
            shooterPlayerId;

        out.targetPlayerId =
            targetPlayerId;

        out.hit =
            hit ? 1 : 0;

        out.remainingBlades =
            remainingBlades;

        out.startX = startX;
        out.startY = startY;
        out.startZ = startZ;

        out.endX = endX;
        out.endY = endY;
        out.endZ = endZ;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[ENTRY BLADE FIRE BROADCAST] roomId="
            << roomId
            << " shooter="
            << shooterPlayerId
            << " target="
            << targetPlayerId
            << " hit="
            << hit
            << " remaining="
            << static_cast<int>(remainingBlades)
            << std::endl;
    }

    bool IsEntryBladeActive(uint32_t playerId)
    {
        std::lock_guard<std::mutex> lock(g_entryBladeMutex);

        auto it =
            g_entryBladeStates.find(playerId);

        if (it == g_entryBladeStates.end())
            return false;

        if (!it->second.active)
            return false;

        if (it->second.bladeCount == 0)
            return false;

        if (std::chrono::steady_clock::now() > it->second.endTime)
            return false;

        return true;
    }

    void EndEntryBladeUltimate(
        uint32_t roomId,
        uint32_t playerId,
        const char* reason)
    {
        bool shouldBroadcast = false;

        {
            std::lock_guard<std::mutex> lock(g_entryBladeMutex);

            auto it =
                g_entryBladeStates.find(playerId);

            if (it != g_entryBladeStates.end())
            {
                g_entryBladeStates.erase(it);
                shouldBroadcast = true;
            }
        }

        if (!shouldBroadcast)
            return;

        BroadcastEntryBladeState(
            roomId,
            playerId,
            false,
            0,
            0.0f
        );

        std::cout
            << "[ENTRY BLADE END] playerId="
            << playerId
            << " reason="
            << reason
            << std::endl;
    }

    void ClearEntryBladesInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> removedPlayerIds;

        {
            std::lock_guard<std::mutex> lock(g_entryBladeMutex);

            for (auto it = g_entryBladeStates.begin();
                it != g_entryBladeStates.end();)
            {
                if (it->second.roomId == roomId)
                {
                    removedPlayerIds.push_back(it->first);
                    it = g_entryBladeStates.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t playerId : removedPlayerIds)
        {
            BroadcastEntryBladeState(
                roomId,
                playerId,
                false,
                0,
                0.0f
            );
        }

        if (!removedPlayerIds.empty())
        {
            std::cout
                << "[ENTRY BLADE CLEAR ROOM] roomId="
                << roomId
                << " count="
                << removedPlayerIds.size()
                << std::endl;
        }
    }

    void BroadcastBeastStealthState(
        uint32_t roomId,
        uint32_t playerId,
        bool active,
        float duration,
        float speedMultiplier,
        float revealRange)
    {
        SC_BeastStealthState out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_BEAST_STEALTH_STATE
                );

        out.playerId = playerId;
        out.active = active ? 1 : 0;
        out.duration = duration;
        out.speedMultiplier = speedMultiplier;
        out.revealRange = revealRange;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[BEAST STEALTH STATE BROADCAST] roomId="
            << roomId
            << " playerId="
            << playerId
            << " active="
            << active
            << " duration="
            << duration
            << " speedMultiplier="
            << speedMultiplier
            << " revealRange="
            << revealRange
            << std::endl;
    }

    void ClearBeastStealthInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> clearedPlayers;

        {
            std::lock_guard<std::mutex> lock(g_beastStealthMutex);

            for (auto it = g_beastStealthStates.begin();
                it != g_beastStealthStates.end();)
            {
                if (it->second.roomId == roomId)
                {
                    clearedPlayers.push_back(it->first);
                    it = g_beastStealthStates.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t playerId : clearedPlayers)
        {
            BroadcastBeastStealthState(
                roomId,
                playerId,
                false,
                0.0f,
                1.0f,
                BEAST_STEALTH_REVEAL_RANGE
            );
        }

        std::cout
            << "[BEAST STEALTH CLEAR ROOM] roomId="
            << roomId
            << " count="
            << clearedPlayers.size()
            << std::endl;
    }

    void ClearElectricStatusInRoom(uint32_t roomId)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        std::lock_guard<std::mutex> lock(g_electricStatusMutex);

        for (const RoomMember& member : room.members)
        {
            g_electricStatuses.erase(member.playerId);
        }

        std::cout
            << "[ELECTRIC STATUS CLEAR ROOM] roomId="
            << roomId
            << std::endl;
    }

    void BroadcastControllerSmokeSpawn(uint32_t roomId, uint32_t smokeId, uint32_t ownerPlayerId, float x, float y, float z)
    {
        SC_ControllerSmokeSpawn out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CONTROLLER_SMOKE_SPAWN);

        out.smokeId = smokeId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.radius = CONTROLLER_SMOKE_RADIUS;
        out.duration = CONTROLLER_SMOKE_DURATION;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[CONTROLLER SMOKE SPAWN BROADCAST] roomId=" << roomId << " smokeId=" << smokeId << " owner=" << ownerPlayerId << " pos=(" << x << "," << y << "," << z << ")" << std::endl;
    }

    void BroadcastControllerSmokeDestroy(uint32_t roomId, uint32_t smokeId)
    {
        SC_ControllerSmokeDestroy out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CONTROLLER_SMOKE_DESTROY);

        out.smokeId = smokeId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[CONTROLLER SMOKE DESTROY BROADCAST] roomId=" << roomId << " smokeId=" << smokeId << std::endl;
    }

    void ClearControllerSmokesInRoom(uint32_t roomId)
    {
        std::vector<uint32_t> smokeIds;

        {
            std::lock_guard<std::mutex> lock(g_controllerSmokeMutex);

            for (auto it = g_controllerSmokes.begin(); it != g_controllerSmokes.end();)
            {
                if (it->second.roomId == roomId)
                {
                    smokeIds.push_back(it->first);
                    it = g_controllerSmokes.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t smokeId : smokeIds)
        {
            BroadcastControllerSmokeDestroy(roomId, smokeId);
        }

        if (!smokeIds.empty())
        {
            std::cout << "[CONTROLLER SMOKE CLEAR ROOM] roomId=" << roomId << " count=" << smokeIds.size() << std::endl;
        }
    }

    void ResetRound(uint32_t roomId)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        ClearElectricStatusInRoom(roomId);
        ClearBeastStealthInRoom(roomId);
        ClearEntrySmokesInRoom(roomId);
        ClearEntryBladesInRoom(roomId);
        ClearControllerSwampsInRoom(roomId);
        ClearControllerSmokesInRoom(roomId);
        ClearControllerDecoysInRoom(roomId);
        ClearControllerUltimatesInRoom(roomId);
        ClearSentinelCTrapsInRoom(roomId);
        ClearSentinelQTrapsInRoom(roomId);
        ClearSentinelETrapsInRoom(roomId);
        ClearSentinelXUltimatesInRoom(roomId);

        ClearDronesInRoom(roomId, 2);
        ClearDroppedWeaponsInRoom(roomId);

        SC_RoundReset out{};

        bool isSideSwapStartRound = false;
        bool isOvertimeStartRound = false;
        bool isOvertimeRound = false;
        bool forceDefaultInventoryReset = false;

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROUND_RESET);

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            MatchRuntimeState& state = g_matchStates[roomId];

            state.currentRound++;
            state.roundResolving = false;

            if (state.currentRound == SIDE_SWAP_AFTER_ROUND + 1)
            {
                state.loseStreakA = 0;
                state.loseStreakB = 0;
                isSideSwapStartRound = true;
            }

            // 연장전 시작
            if (state.currentRound == OVERTIME_START_ROUND)
            {
                state.loseStreakA = 0;
                state.loseStreakB = 0;
                isOvertimeStartRound = true;
            }

            // 연장전은 매 라운드 공수 교대 + 6000 크레딧
            if (state.currentRound >= OVERTIME_START_ROUND)
            {
                isOvertimeRound = true;
            }

            // 공수 변경 시점, 연장전 라운드는 기본 상태로 초기화
            forceDefaultInventoryReset =
                isSideSwapStartRound ||
                isOvertimeRound;

            state.spikeCarrierPlayerId = 0;
            state.spikePlanted = false;

            state.spikeDefuserPlayerId = 0;
            state.spikeHalfDefused = false;

            // 이전 라운드에 남아 있는 스파이크 타이머를 무효화한다.
            state.spikePlantGeneration++;

            state.buyPhase = true;

            out.currentRound = state.currentRound;
            out.scoreA = state.scoreA;
            out.scoreB = state.scoreB;
        }

        uint8_t count = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.slotIndex >= 3)
                continue;

            if (count >= GAME_MEMBER_COUNT)
                break;

            auto playerSession =
                SessionManager::Instance().Find(member.playerId);

            if (playerSession == nullptr)
                continue;

            bool wasDead = playerSession->IsDead();

            if (forceDefaultInventoryReset || wasDead)
            {
                // 죽은 플레이어, 공수 변경, 연장전은 기본 장비로 초기화
                playerSession->InitRoundState();
            }
            else
            {
                // 생존자는 들고 있던 무기 유지
                playerSession->ResetRoundStateKeepInventory();
            }

            if (forceDefaultInventoryReset)
                playerSession->ResetSkills();
            else
                playerSession->PrepareSkillsForNextRound();

            playerSession->ClearLastDamager();
            playerSession->ClearAssistDamageRecords();

            RoundResetMemberInfo& info = out.members[count];

            info.playerId = playerSession->GetPlayerId();

            RoomTeam spawnRoleTeam = GetSpawnRoleTeam(
                member.team,
                out.currentRound
            );

            info.actualTeam = static_cast<uint8_t>(member.team);
            info.spawnTeam = static_cast<uint8_t>(spawnRoleTeam);

            info.slotIndex = member.slotIndex;

            info.hp = playerSession->GetHP();
            info.barrier = playerSession->GetBarrier();
            info.maxHp = playerSession->GetMaxHP();
            info.maxBarrier = playerSession->GetMaxBarrier();

            info.currentWeaponId = playerSession->GetCurrentWeaponId();

            PlayerWeaponState* weaponState =
                playerSession->GetWeaponState(info.currentWeaponId);

            if (weaponState != nullptr)
            {
                info.magAmmo = weaponState->magAmmo;
                info.reserveAmmo = weaponState->reserveAmmo;
            }
            else
            {
                info.magAmmo = 0;
                info.reserveAmmo = 0;
            }
            FillRoundResetWeaponSlots(info, playerSession.get());
            info.purchasedSkillMask = playerSession->GetPurchasedSkillMask();
            info.usedSkillMask = playerSession->GetUsedSkillMask();
            count++;
        }

        out.memberCount = count;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[ROUND RESET] roomId="
            << roomId
            << " round=" << static_cast<int>(out.currentRound)
            << " scoreA=" << static_cast<int>(out.scoreA)
            << " scoreB=" << static_cast<int>(out.scoreB)
            << " memberCount=" << static_cast<int>(out.memberCount)
            << std::endl;

        if (isSideSwapStartRound && !isOvertimeRound)
        {
            int halfStartCredit = GetCreditReward("ROUND_START_HALF");

            SetAllPlayerCreditsInRoom(roomId, halfStartCredit);
            BroadcastScoreboardUpdate(roomId);

            std::cout << "[CREDIT HALF START] roomId="
                << roomId
                << " credit=" << halfStartCredit
                << std::endl;
        }

        if (isOvertimeRound)
        {
            SetAllPlayerCreditsInRoom(roomId, OVERTIME_ROUND_CREDIT);
            BroadcastScoreboardUpdate(roomId);

            std::cout << "[CREDIT OVERTIME ROUND] roomId="
                << roomId
                << " credit=" << OVERTIME_ROUND_CREDIT
                << std::endl;
        }

        StartGamePrepareTimerThread(roomId);
    }

    bool FindOwnedWeaponByPredicate(
        Session* session,
        bool (*predicate)(int),
        PlayerWeaponState& outWeapon)
    {
        if (session == nullptr)
            return false;

        std::vector<PlayerWeaponState> ownedWeapons =
            session->GetOwnedWeapons();

        for (const PlayerWeaponState& weapon : ownedWeapons)
        {
            if (!weapon.owned)
                continue;

            if (predicate == nullptr)
                continue;

            if (!predicate(weapon.weaponId))
                continue;

            outWeapon = weapon;
            return true;
        }

        return false;
    }

    void FillRoundResetWeaponSlots(
        RoundResetMemberInfo& info,
        Session* session)
    {
        info.primaryWeaponId = -1;
        info.primaryMagAmmo = 0;
        info.primaryReserveAmmo = 0;
        info.primarySkinId = 0;

        info.pistolWeaponId = -1;
        info.pistolMagAmmo = 0;
        info.pistolReserveAmmo = 0;
        info.pistolSkinId = 0;

        info.knifeWeaponId = -1;
        info.knifeMagAmmo = 0;
        info.knifeReserveAmmo = 0;
        info.knifeSkinId = 0;

        if (session == nullptr)
            return;

        PlayerWeaponState weapon{};

        if (FindOwnedWeaponByPredicate(session, IsPrimaryWeapon, weapon))
        {
            info.primaryWeaponId = weapon.weaponId;
            info.primaryMagAmmo = weapon.magAmmo;
            info.primaryReserveAmmo = weapon.reserveAmmo;
            info.primarySkinId = weapon.skinId;
        }

        if (FindOwnedWeaponByPredicate(session, IsPistolWeapon, weapon))
        {
            info.pistolWeaponId = weapon.weaponId;
            info.pistolMagAmmo = weapon.magAmmo;
            info.pistolReserveAmmo = weapon.reserveAmmo;
            info.pistolSkinId = weapon.skinId;
        }

        if (FindOwnedWeaponByPredicate(session, IsKnifeWeapon, weapon))
        {
            info.knifeWeaponId = weapon.weaponId;
            info.knifeMagAmmo = weapon.magAmmo;
            info.knifeReserveAmmo = weapon.reserveAmmo;
            info.knifeSkinId = weapon.skinId;
        }
    }

    void StartRoundEndThread(uint32_t roomId, RoomTeam winnerTeam, bool matchEnd)
    {
        std::thread([roomId, winnerTeam, matchEnd]()
            {
                std::this_thread::sleep_for(
                    std::chrono::seconds(ROUND_END_DELAY_SECONDS)
                );

                uint8_t scoreA = 0;
                uint8_t scoreB = 0;

                {
                    std::lock_guard<std::mutex> lock(g_matchStateMutex);

                    auto it = g_matchStates.find(roomId);
                    if (it == g_matchStates.end())
                        return;

                    scoreA = it->second.scoreA;
                    scoreB = it->second.scoreB;
                }

                if (matchEnd)
                {
                    BroadcastMatchResultDetail(
                        roomId,
                        winnerTeam,
                        scoreA,
                        scoreB
                    );

                    return;
                }

                ResetRound(roomId);
            }
        ).detach();
    }

    bool IsTeamEliminated(const Room& room, RoomTeam team)
    {
        int teamPlayerCount = 0;
        int aliveCount = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team != team)
                continue;

            if (member.slotIndex >= 3)
                continue;

            teamPlayerCount++;

            auto playerSession =
                SessionManager::Instance().Find(member.playerId);

            if (playerSession == nullptr)
                continue;

            if (!playerSession->IsDead())
                aliveCount++;
        }

        return teamPlayerCount > 0 && aliveCount == 0;
    }

    bool ShouldEndMatch(uint8_t scoreA, uint8_t scoreB, bool isOvertime)
    {
        uint8_t highScore = scoreA > scoreB ? scoreA : scoreB;
        uint8_t lowScore = scoreA > scoreB ? scoreB : scoreA;

        if (!isOvertime)
        {
            // 정규전에서는 3:0 또는 3:1이면 종료
            return highScore >= MATCH_WIN_ROUND_COUNT;
        }

        // 듀스에서는 2점 차가 나야 종료
        return highScore >= MATCH_WIN_ROUND_COUNT &&
            (highScore - lowScore) >= 2;
    }

    void TryResolveRoundByElimination(uint32_t roomId)
    {
        bool spikePlanted = false;
        uint8_t ruleRound = 1;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                return;

            if (it->second.roundResolving || it->second.matchEnded)
                return;

            spikePlanted = it->second.spikePlanted;
            ruleRound = it->second.currentRound;
        }

        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        bool teamADead = IsTeamEliminated(room, RoomTeam::A);
        bool teamBDead = IsTeamEliminated(room, RoomTeam::B);

        if (!teamADead && !teamBDead)
            return;

        RoomTeam attackTeam = GetAttackTeam(ruleRound);
        RoomTeam defenseTeam = GetDefenseTeam(ruleRound);

        RoomTeam winnerTeam = RoomTeam::A;

        if (spikePlanted)
        {
            bool attackDead =
                attackTeam == RoomTeam::A ? teamADead : teamBDead;

            bool defenseDead =
                defenseTeam == RoomTeam::A ? teamADead : teamBDead;

            if (defenseDead)
            {
                // 스파이크 설치 후 수비팀 전멸 = 공격팀 승리
                winnerTeam = attackTeam;
            }
            else if (attackDead)
            {
                // 스파이크 설치 후 공격팀 전멸 = 아직 라운드 종료 아님
                // 수비팀은 반드시 해체해야 승리
                std::cout << "[ELIMINATION BLOCK] attack team dead but spike planted. need defuse. roomId="
                    << roomId
                    << std::endl;
                return;
            }
            else
            {
                return;
            }
        }
        else
        {
            if (teamADead && teamBDead)
            {
                std::cout << "[ROUND RESULT BLOCK] both teams dead roomId="
                    << roomId
                    << std::endl;
                return;
            }

            winnerTeam = teamADead ? RoomTeam::B : RoomTeam::A;
        }

        uint8_t scoreA = 0;
        uint8_t scoreB = 0;
        uint8_t currentRound = 1;
        bool matchEnd = false;
        bool isOvertime = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            MatchRuntimeState& state = g_matchStates[roomId];

            if (state.roundResolving || state.matchEnded)
                return;

            state.roundResolving = true;

            if (winnerTeam == RoomTeam::A)
                state.scoreA++;
            else
                state.scoreB++;

            if (IsOvertimeScore(state.scoreA, state.scoreB))
            {
                state.isOvertime = true;
                std::cout << "[OVERTIME START] roomId=" << roomId << std::endl;
            }

            currentRound = state.currentRound;
            scoreA = state.scoreA;
            scoreB = state.scoreB;
            isOvertime = state.isOvertime;

            matchEnd = ShouldEndMatch(scoreA, scoreB, isOvertime);

            if (matchEnd)
                state.matchEnded = true;
        }

        RoundWinReason winReason =
            GetEliminationWinReason(winnerTeam, currentRound);

        BroadcastRoundResult(
            roomId,
            winnerTeam,
            scoreA,
            scoreB,
            currentRound,
            matchEnd,
            winReason
        );

        ApplyRoundEndCredits(roomId, winnerTeam, winReason);

        BroadcastScoreboardUpdate(roomId);

        StartRoundEndThread(
            roomId,
            winnerTeam,
            matchEnd
        );
    }

    bool AreAllPlayersReturnedFromResult(uint32_t roomId, const Room& room)
    {
        std::lock_guard<std::mutex> lock(g_resultLeaveMutex);

        auto it = g_resultLeavePlayers.find(roomId);

        if (it == g_resultLeavePlayers.end())
            return true;

        int playMemberCount = 0;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.slotIndex >= 3)
                continue;

            playMemberCount++;

            if (it->second.count(member.playerId) == 0)
            {
                std::cout << "[RESULT RETURN CHECK] not returned. "
                    << "roomId=" << roomId
                    << " playerId=" << member.playerId
                    << std::endl;

                return false;
            }
        }

        return playMemberCount == 6;
    }

    RoomTeam GetDefenseTeam(uint8_t currentRound)
    {
        bool aTeamAttack = IsATeamAttackRound(currentRound);

        return aTeamAttack ? RoomTeam::B : RoomTeam::A;
    }

    RoomTeam GetAttackTeam(uint8_t currentRound)
    {
        bool aTeamAttack = IsATeamAttackRound(currentRound);
        return aTeamAttack ? RoomTeam::A : RoomTeam::B;
    }

    void BroadcastControllerDecoySpawn(
        uint32_t roomId,
        uint32_t decoyId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float vx,
        float vy,
        float vz)
    {
        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        SC_ControllerDecoySpawn out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CONTROLLER_DECOY_SPAWN);

        out.decoyId = decoyId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.vx = vx;
        out.vy = vy;
        out.vz = vz;

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout
            << "[CONTROLLER DECOY SPAWN BROADCAST] roomId=" << roomId
            << " decoyId=" << decoyId
            << " owner=" << ownerPlayerId
            << " pos=(" << x << ", " << y << ", " << z << ")"
            << " velocity=(" << vx << ", " << vy << ", " << vz << ")"
            << std::endl;
    }

    void BroadcastControllerDecoyActivate(
        uint32_t roomId,
        uint32_t decoyId,
        uint32_t ownerPlayerId,
        float x,
        float y,
        float z,
        float normalX,
        float normalY,
        float normalZ,
        float duration)
    {
        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        SC_ControllerDecoyActivate out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CONTROLLER_DECOY_ACTIVATE);

        out.decoyId = decoyId;
        out.ownerPlayerId = ownerPlayerId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.normalX = normalX;
        out.normalY = normalY;
        out.normalZ = normalZ;

        out.duration = duration;

        for (const RoomMember& member : room.members)
        {
            auto target = SessionManager::Instance().Find(member.playerId);
            if (target == nullptr)
                continue;

            target->Send(&out);
        }

        std::cout
            << "[CONTROLLER DECOY ACTIVATE BROADCAST] roomId=" << roomId
            << " decoyId=" << decoyId
            << " owner=" << ownerPlayerId
            << " pos=(" << x << ", " << y << ", " << z << ")"
            << " normal=(" << normalX << ", " << normalY << ", " << normalZ << ")"
            << " duration=" << duration
            << std::endl;
    }

    
    void ScheduleControllerDecoyDestroy(uint32_t roomId, uint32_t decoyId, float duration)
    {
        std::thread([roomId, decoyId, duration]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<int>(duration * 1000.0f)
                    )
                );

                bool shouldBroadcast = false;

                {
                    std::lock_guard<std::mutex> lock(g_controllerDecoyMutex);

                    auto it = g_controllerDecoys.find(decoyId);
                    if (it != g_controllerDecoys.end())
                    {
                        g_controllerDecoys.erase(it);
                        shouldBroadcast = true;
                    }
                }

                if (shouldBroadcast)
                    BroadcastControllerDecoyDestroy(roomId, decoyId);

            }).detach();
    }

    void BroadcastSpikePickupNotify(uint32_t roomId, uint32_t carrierPlayerId)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        RoomMember carrierMember{};
        if (!RoomManager::Instance().GetMember(roomId, carrierPlayerId, carrierMember))
            return;

        for (const RoomMember& receiver : room.members)
        {
            if (receiver.team == RoomTeam::OBSERVER)
                continue;

            auto target = SessionManager::Instance().Find(receiver.playerId);
            if (target == nullptr)
                continue;

            SC_SpikePickupNotify out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_PICKUP_NOTIFY);

            out.carrierPlayerId = carrierPlayerId;

            // 같은 공격팀에게만 누가 먹었는지 공개
            out.showCarrierInfo = receiver.team == carrierMember.team ? 1 : 0;

            memset(out.carrierNickname, 0, sizeof(out.carrierNickname));

            if (out.showCarrierInfo != 0)
            {
                strncpy_s(
                    out.carrierNickname,
                    sizeof(out.carrierNickname),
                    carrierMember.nickname.c_str(),
                    _TRUNCATE
                );
            }

            target->Send(&out);
        }

        std::cout << "[SPIKE PICKUP NOTIFY] roomId="
            << roomId
            << " carrier=" << carrierPlayerId
            << std::endl;
    }

    void BroadcastSpikePlanted(uint32_t roomId, float x, float y, float z)
    {
        SC_SpikePlanted out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_PLANTED);

        out.x = x;
        out.y = y;
        out.z = z;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SPIKE PLANTED] roomId="
            << roomId
            << " pos=(" << x << ", " << y << ", " << z << ")"
            << std::endl;
    }

    void ResolveRoundBySpikeExplode(uint32_t roomId, uint64_t spikeGeneration)
    {
        RoomTeam winnerTeam = RoomTeam::A;

        uint8_t scoreA = 0;
        uint8_t scoreB = 0;
        uint8_t currentRound = 1;
        bool matchEnd = false;
        bool isOvertime = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                return;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                return;

            if (!state.spikePlanted)
                return;

            // 이 스레드가 시작될 때의 설치와 현재 설치가 다르면
            // 이전 라운드에서 남아 있던 타이머이므로 무시한다.
            if (state.spikePlantGeneration != spikeGeneration)
            {
                std::cout << "[SPIKE TIMER STALE] roomId="
                    << roomId
                    << " timerGeneration=" << spikeGeneration
                    << " currentGeneration=" << state.spikePlantGeneration
                    << std::endl;
                return;
            }

            state.roundResolving = true;
            state.spikePlanted = false;

            // 같은 설치에 대해 남아 있을 수 있는 다른 타이머도 무효화한다.
            state.spikePlantGeneration++;

            winnerTeam = GetAttackTeam(state.currentRound);

            if (winnerTeam == RoomTeam::A)
                state.scoreA++;
            else
                state.scoreB++;

            if (IsOvertimeScore(state.scoreA, state.scoreB))
                state.isOvertime = true;

            currentRound = state.currentRound;
            scoreA = state.scoreA;
            scoreB = state.scoreB;
            isOvertime = state.isOvertime;

            matchEnd = ShouldEndMatch(scoreA, scoreB, isOvertime);

            if (matchEnd)
                state.matchEnded = true;
        }

        BroadcastRoundResult(
            roomId,
            winnerTeam,
            scoreA,
            scoreB,
            currentRound,
            matchEnd,
            RoundWinReason::ATTACK_SPIKE_EXPLODE
        );

        ApplyRoundEndCredits(
            roomId,
            winnerTeam,
            RoundWinReason::ATTACK_SPIKE_EXPLODE
        );

        BroadcastScoreboardUpdate(roomId);

        StartRoundEndThread(
            roomId,
            winnerTeam,
            matchEnd
        );

        std::cout << "[SPIKE EXPLODE ROUND WIN] roomId="
            << roomId
            << " winnerTeam=" << static_cast<int>(winnerTeam)
            << std::endl;
    }

    void StartSpikeTimerThread(uint32_t roomId, uint64_t spikeGeneration)
    {
        std::thread([roomId, spikeGeneration]()
            {
                std::this_thread::sleep_for(
                    std::chrono::seconds(SPIKE_EXPLODE_SECONDS)
                );

                ResolveRoundBySpikeExplode(
                    roomId,
                    spikeGeneration
                );
            }
        ).detach();

        std::cout << "[SPIKE TIMER START] roomId="
            << roomId
            << " generation=" << spikeGeneration
            << " seconds=" << SPIKE_EXPLODE_SECONDS
            << std::endl;
    }

    void ResolveRoundByTimeOut(uint32_t roomId)
    {
        RoomTeam winnerTeam = RoomTeam::B;

        uint8_t scoreA = 0;
        uint8_t scoreB = 0;
        uint8_t currentRound = 1;
        bool matchEnd = false;
        bool isOvertime = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                return;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                return;

            state.roundResolving = true;

            winnerTeam = GetDefenseTeam(state.currentRound);

            if (winnerTeam == RoomTeam::A)
                state.scoreA++;
            else
                state.scoreB++;

            if (IsOvertimeScore(state.scoreA, state.scoreB))
                state.isOvertime = true;

            currentRound = state.currentRound;
            scoreA = state.scoreA;
            scoreB = state.scoreB;
            isOvertime = state.isOvertime;

            matchEnd = ShouldEndMatch(scoreA, scoreB, isOvertime);

            if (matchEnd)
                state.matchEnded = true;
        }

        BroadcastRoundResult(
            roomId,
            winnerTeam,
            scoreA,
            scoreB,
            currentRound,
            matchEnd,
            RoundWinReason::DEFENSE_TIME_OUT
        );

        ApplyRoundEndCredits(
            roomId,
            winnerTeam,
            RoundWinReason::DEFENSE_TIME_OUT
        );

        BroadcastScoreboardUpdate(roomId);

        StartRoundEndThread(
            roomId,
            winnerTeam,
            matchEnd
        );

        std::cout << "[ROUND TIME OUT] roomId="
            << roomId
            << " winnerTeam=" << static_cast<int>(winnerTeam)
            << " scoreA=" << static_cast<int>(scoreA)
            << " scoreB=" << static_cast<int>(scoreB)
            << " currentRound=" << static_cast<int>(currentRound)
            << std::endl;
    }

    void StartRoundTimerThread(uint32_t roomId)
    {
        std::thread([roomId]()
            {
                for (int remain = ROUND_PLAY_SECONDS; remain >= 0; --remain)
                {
                    {
                        std::lock_guard<std::mutex> lock(g_matchStateMutex);

                        auto it = g_matchStates.find(roomId);
                        if (it == g_matchStates.end())
                            return;

                        if (it->second.roundResolving || it->second.matchEnded || it->second.spikePlanted)
                            return;
                    }

                    BroadcastRoundTimer(
                        roomId,
                        static_cast<uint16_t>(remain)
                    );

                    if (remain == 0)
                        break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                ResolveRoundByTimeOut(roomId);
            }
        ).detach();

        std::cout << "[ROUND TIMER START] roomId="
            << roomId
            << " seconds=" << ROUND_PLAY_SECONDS
            << std::endl;
    }

    void BroadcastSpikeDropped(uint32_t roomId, float x, float y, float z, float dirX, float dirY, float dirZ, uint32_t dropperPlayerId)
    {
        SC_SpikeDropped out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_DROPPED);

        out.x = x;
        out.y = y;
        out.z = z;

        out.dirX = dirX;
        out.dirY = dirY;
        out.dirZ = dirZ;

        out.dropperPlayerId = dropperPlayerId;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void BroadcastSpikeDefused(uint32_t roomId, uint32_t defuserPlayerId)
    {
        SC_SpikeDefused out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_DEFUSED);
        out.defuserPlayerId = defuserPlayerId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SPIKE DEFUSED] roomId="
            << roomId
            << " defuser=" << defuserPlayerId
            << std::endl;
    }

    void ResolveRoundBySpikeDefuse(uint32_t roomId, uint32_t defuserPlayerId)
    {
        RoomTeam winnerTeam = RoomTeam::B;

        uint8_t scoreA = 0;
        uint8_t scoreB = 0;
        uint8_t currentRound = 1;
        bool matchEnd = false;
        bool isOvertime = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                return;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                return;

            if (!state.spikePlanted)
                return;

            state.roundResolving = true;
            state.spikePlanted = false;
            state.spikeCarrierPlayerId = 0;

            // 설치 당시 시작된 폭발 타이머를 무효화한다.
            state.spikePlantGeneration++;

            state.spikeDefuserPlayerId = 0;
            state.spikeHalfDefused = false;

            winnerTeam = GetDefenseTeam(state.currentRound);

            if (winnerTeam == RoomTeam::A)
                state.scoreA++;
            else
                state.scoreB++;

            if (IsOvertimeScore(state.scoreA, state.scoreB))
                state.isOvertime = true;

            currentRound = state.currentRound;
            scoreA = state.scoreA;
            scoreB = state.scoreB;
            isOvertime = state.isOvertime;

            matchEnd = ShouldEndMatch(scoreA, scoreB, isOvertime);

            if (matchEnd)
                state.matchEnded = true;
        }

        BroadcastSpikeDefused(roomId, defuserPlayerId);

        BroadcastRoundResult(
            roomId,
            winnerTeam,
            scoreA,
            scoreB,
            currentRound,
            matchEnd,
            RoundWinReason::DEFENSE_SPIKE_DEFUSE
        );

        StartRoundEndThread(roomId, winnerTeam, matchEnd);
    }

    void BroadcastSpikeDefuseState(uint32_t roomId, uint8_t state, bool halfDefused, uint32_t defuserPlayerId)
    {
        SC_SpikeDefuseState out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SPIKE_DEFUSE_STATE);

        out.state = state;
        out.halfDefused = halfDefused ? 1 : 0;
        out.defuserPlayerId = defuserPlayerId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SPIKE DEFUSE STATE] roomId="
            << roomId
            << " state=" << static_cast<int>(state)
            << " half=" << static_cast<int>(out.halfDefused)
            << " defuser=" << defuserPlayerId
            << std::endl;
    }

    void SetAllPlayerCreditsInRoom(uint32_t roomId, int credit)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            auto s = SessionManager::Instance().Find(member.playerId);

            if (s == nullptr)
                continue;

            s->SetCredits(credit);
        }
        std::cout << "[CREDITS SET] roomId="
            << roomId
            << " credits=" << credit
            << std::endl;
    }

    std::string GetLoseStreakRewardKey(uint8_t loseStreak)
    {
        if (loseStreak <= 1)
            return "LOSE_STREAK_1";

        if (loseStreak == 2)
            return "LOSE_STREAK_2";

        return "LOSE_STREAK_3_PLUS";
    }

    void ApplyRoundEndCredits(uint32_t roomId, RoomTeam winnerTeam, RoundWinReason winReason)
    {
        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            return;

        RoomTeam loserTeam = winnerTeam == RoomTeam::A ? RoomTeam::B : RoomTeam::A;

        uint8_t loserStreak = 1;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            MatchRuntimeState& state = g_matchStates[roomId];

            if (winnerTeam == RoomTeam::A)
            {
                state.loseStreakA = 0;

                if (state.loseStreakB < 3)
                    state.loseStreakB++;

                loserStreak = state.loseStreakB;
            }
            else
            {
                state.loseStreakB = 0;

                if (state.loseStreakA < 3)
                    state.loseStreakA++;

                loserStreak = state.loseStreakA;
            }
        }

        int winCredit = GetCreditReward("ROUND_WIN");
        int loseCredit = GetCreditReward(GetLoseStreakRewardKey(loserStreak));
        int surviveDefeatCredit = GetCreditReward("SURVIVE_DEFEAT");

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            auto s = SessionManager::Instance().Find(member.playerId);

            if (s == nullptr)
                continue;

            if (member.team == winnerTeam)
            {
                s->AddCredits(winCredit);

                std::cout << "[CREDIT ROUND WIN] playerId="
                    << s->GetPlayerId()
                    << " add=" << winCredit
                    << " total=" << s->GetCredits()
                    << std::endl;

                continue;
            }

            bool isSurviveDefeat =
                member.team == loserTeam &&
                winReason == RoundWinReason::DEFENSE_TIME_OUT &&
                !s->IsDead();

            if (isSurviveDefeat)
            {
                s->AddCredits(surviveDefeatCredit);

                std::cout << "[CREDIT SURVIVE DEFEAT] playerId="
                    << s->GetPlayerId()
                    << " add=" << surviveDefeatCredit
                    << " total=" << s->GetCredits()
                    << std::endl;
            }
            else
            {
                s->AddCredits(loseCredit);

                std::cout << "[CREDIT ROUND LOSE] playerId="
                    << s->GetPlayerId()
                    << " streak=" << static_cast<int>(loserStreak)
                    << " add=" << loseCredit
                    << " total=" << s->GetCredits()
                    << std::endl;
            }
        }
    }

    struct DroppedWeaponState
    {
        uint32_t dropId = 0;
        uint32_t roomId = 0;

        int weaponId = 0;
        int magAmmo = 0;
        int reserveAmmo = 0;
        int skinId = 0;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        float dirX = 0.f;
        float dirY = 0.f;
        float dirZ = 0.f;
    };

    std::mutex g_droppedWeaponMutex;
    std::unordered_map<uint32_t, DroppedWeaponState> g_droppedWeapons;
    uint32_t g_nextDroppedWeaponId = 1;

    void ClearDroppedWeaponsInRoom(uint32_t roomId)
    {
        std::lock_guard<std::mutex> lock(g_droppedWeaponMutex);

        int removedCount = 0;

        for (auto it = g_droppedWeapons.begin(); it != g_droppedWeapons.end();)
        {
            if (it->second.roomId == roomId)
            {
                it = g_droppedWeapons.erase(it);
                removedCount++;
            }
            else
            {
                ++it;
            }
        }

        std::cout << "[DROPPED WEAPONS CLEAR] roomId="
            << roomId
            << " removedCount=" << removedCount
            << std::endl;
    }

    bool IsKnifeWeapon(int weaponId)
    {
        const ServerWeaponData* data = ServerWeaponTable::Find(weaponId);

        if (data == nullptr)
            return false;

        return data->weaponType == ServerWeaponType::Knife;
    }

    bool IsPistolWeapon(int weaponId)
    {
        const ServerWeaponData* data = ServerWeaponTable::Find(weaponId);

        if (data == nullptr)
            return false;

        return data->weaponType == ServerWeaponType::Pistol;
    }

    bool IsPrimaryWeapon(int weaponId)
    {
        const ServerWeaponData* data =
            ServerWeaponTable::Find(weaponId);

        if (data == nullptr)
            return false;

        return
            data->weaponType == ServerWeaponType::AR ||
            data->weaponType == ServerWeaponType::SMG ||
            data->weaponType == ServerWeaponType::LMG ||
            data->weaponType == ServerWeaponType::SR ||
            data->weaponType == ServerWeaponType::Shotgun ||
            data->weaponType == ServerWeaponType::DMR;
    }

    int GetEquippedSkinIdForUser(const std::string& userId, int characterId, int weaponId)
    {
        if (weaponId < 0 || weaponId >= WEAPON_SKIN_WEAPON_COUNT)
            return 0;

        std::array<int, WEAPON_SKIN_WEAPON_COUNT> equippedSkins{};

        if (!WeaponSkinManager::Instance().GetEquippedSkins(userId, characterId, equippedSkins))
            return 0;

        return equippedSkins[weaponId];
    }

    int FindReplaceWeaponIdForPickup(Session* session, int pickedWeaponId)
    {
        if (session == nullptr)
            return -1;

        std::vector<PlayerWeaponState> ownedWeapons =
            session->GetOwnedWeapons();

        if (IsKnifeWeapon(pickedWeaponId))
        {
            for (const PlayerWeaponState& weapon : ownedWeapons)
            {
                if (IsKnifeWeapon(weapon.weaponId))
                    return weapon.weaponId;
            }

            return -1;
        }

        if (IsPistolWeapon(pickedWeaponId))
        {
            for (const PlayerWeaponState& weapon : ownedWeapons)
            {
                if (IsPistolWeapon(weapon.weaponId))
                    return weapon.weaponId;
            }

            return -1;
        }

        if (IsPrimaryWeapon(pickedWeaponId))
        {
            for (const PlayerWeaponState& weapon : ownedWeapons)
            {
                if (IsPrimaryWeapon(weapon.weaponId))
                    return weapon.weaponId;
            }

            return -1;
        }

        return -1;
    }

    uint32_t CreateDroppedWeapon(uint32_t roomId, uint32_t dropperPlayerId, int weaponId, int magAmmo, int reserveAmmo, int skinId, float x, float y, float z, float dirX, float dirY, float dirZ)
    {
        uint32_t dropId = 0;

        {
            std::lock_guard<std::mutex> lock(g_droppedWeaponMutex);

            dropId = g_nextDroppedWeaponId++;

            DroppedWeaponState state{};
            state.dropId = dropId;
            state.roomId = roomId;
            state.weaponId = weaponId;
            state.magAmmo = magAmmo;
            state.reserveAmmo = reserveAmmo;
            state.skinId = skinId;
            state.x = x;
            state.y = y;
            state.z = z;
            state.dirX = dirX;
            state.dirY = dirY;
            state.dirZ = dirZ;

            g_droppedWeapons[dropId] = state;
        }

        SC_WeaponDropped out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_WEAPON_DROPPED);

        out.dropId = dropId;
        out.dropperPlayerId = dropperPlayerId;
        out.weaponId = weaponId;
        out.magAmmo = magAmmo;
        out.reserveAmmo = reserveAmmo;
        out.skinId = skinId;

        out.x = x;
        out.y = y;
        out.z = z;

        out.dirX = dirX;
        out.dirY = dirY;
        out.dirZ = dirZ;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[WEAPON DROPPED] roomId="
            << roomId
            << " dropId=" << dropId
            << " weaponId=" << weaponId
            << " ammo=" << magAmmo << "/" << reserveAmmo
            << " skinId=" << skinId
            << std::endl;

        return dropId;
    }

    void BroadcastWeaponPickupNotify(uint32_t roomId, uint32_t dropId, uint32_t pickerPlayerId, int weaponId, int magAmmo, int reserveAmmo, int skinId)
    {
        SC_WeaponPickupNotify out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_WEAPON_PICKUP_NOTIFY
            );

        out.dropId = dropId;
        out.pickerPlayerId = pickerPlayerId;
        out.weaponId = weaponId;
        out.magAmmo = magAmmo;
        out.reserveAmmo = reserveAmmo;
        out.skinId = skinId;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void BroadcastWeaponSwitchNotify(uint32_t roomId, uint32_t playerId, int weaponId)
    {
        SC_WeaponSwitchNotify out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_WEAPON_SWITCH_NOTIFY
            );

        out.playerId = playerId;
        out.weaponId = weaponId;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void BroadcastReloadVisualInternal(uint32_t roomId, uint32_t playerId, int weaponId, bool isReloading)
    {
        SC_ReloadVisual out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_RELOAD_VISUAL
            );

        out.playerId = playerId;
        out.weaponId = weaponId;
        out.isReloading = isReloading ? 1 : 0;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void SendStoreBuyResult(
        Session* session,
        StoreBuyResult result,
        StoreItemType itemType,
        int itemId,
        int price,
        int weaponId,
        int magAmmo,
        int reserveAmmo)
    {
        if (session == nullptr)
            return;

        SC_StoreBuyResult out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_STORE_BUY_RESULT
            );

        out.result = static_cast<uint8_t>(result);
        out.itemType = static_cast<uint8_t>(itemType);

        out.itemId = itemId;
        out.price = price;
        out.remainCredits = session->GetCredits();

        out.weaponId = weaponId;
        out.magAmmo = magAmmo;
        out.reserveAmmo = reserveAmmo;

        out.barrier = session->GetBarrier();
        out.maxBarrier = session->GetMaxBarrier();
        out.purchasedSkillMask = session->GetPurchasedSkillMask();
        out.usedSkillMask = session->GetUsedSkillMask();

        session->Send(&out);
    }
    void SendSkillState(Session* session)
    {
        if (session == nullptr)
            return;

        SC_SkillState out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_SKILL_STATE
        );
        out.purchasedSkillMask = session->GetPurchasedSkillMask();
        out.usedSkillMask = session->GetUsedSkillMask();
        session->Send(&out);
    }
    struct ServerDroneState
    {
        uint32_t droneId = 0;
        uint32_t ownerPlayerId = 0;
        uint32_t roomId = 0;

        int hp = DRONE_MAX_HP;

        bool taserUsed = false;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        float yaw = 0.f;
        float pitch = 0.f;
    };

    std::mutex g_droneMutex;
    std::unordered_map<uint32_t, ServerDroneState> g_drones;
    std::unordered_map<uint32_t, uint32_t> g_ownerDroneIds;
    uint32_t g_nextDroneId = 1;

    bool IsDroneActiveForPlayer(uint32_t playerId)
    {
        std::lock_guard<std::mutex> lock(g_droneMutex);
        return g_ownerDroneIds.find(playerId) != g_ownerDroneIds.end();
    }

    void BroadcastDroneTaserFire(
        uint32_t roomId,
        uint32_t droneId,
        uint32_t ownerPlayerId,
        uint32_t targetPlayerId,
        bool hit,
        uint8_t stack,
        float duration,
        int32_t damage)
    {
        SC_DroneTaserFire out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_DRONE_TASER_FIRE
            );

        out.droneId = droneId;
        out.ownerPlayerId = ownerPlayerId;
        out.targetPlayerId = targetPlayerId;

        out.hit = hit ? 1 : 0;
        out.stack = stack;

        out.duration = duration;
        out.damage = damage;

        BroadcastPacketToRoomInternal(roomId, &out);
    }

    void DestroyDroneForPlayer(uint32_t ownerPlayerId, uint8_t reason)
    {
        uint32_t roomId = 0;
        uint32_t droneId = 0;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto ownerIt = g_ownerDroneIds.find(ownerPlayerId);

            if (ownerIt == g_ownerDroneIds.end())
                return;

            droneId = ownerIt->second;

            auto droneIt = g_drones.find(droneId);

            if (droneIt == g_drones.end())
            {
                g_ownerDroneIds.erase(ownerIt);
                return;
            }

            roomId = droneIt->second.roomId;

            g_drones.erase(droneIt);
            g_ownerDroneIds.erase(ownerIt);
        }

        BroadcastDroneDestroy(roomId, droneId, ownerPlayerId, reason);

        std::cout << "[DRONE DESTROY] owner="
            << ownerPlayerId
            << " droneId=" << droneId
            << " roomId=" << roomId
            << " reason=" << static_cast<int>(reason)
            << std::endl;
    }

    void DestroyDroneById(uint32_t droneId, uint8_t reason)
    {
        uint32_t roomId = 0;
        uint32_t ownerPlayerId = 0;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto droneIt = g_drones.find(droneId);
            if (droneIt == g_drones.end())
                return;

            roomId = droneIt->second.roomId;
            ownerPlayerId = droneIt->second.ownerPlayerId;

            g_drones.erase(droneIt);
            g_ownerDroneIds.erase(ownerPlayerId);
        }

        BroadcastDroneDestroy(
            roomId,
            droneId,
            ownerPlayerId,
            reason
        );

        std::cout
            << "[DRONE DESTROY BY ID] droneId="
            << droneId
            << " owner="
            << ownerPlayerId
            << " reason="
            << static_cast<int>(reason)
            << std::endl;
    }

    

    bool IsBeastStealthActive(uint32_t playerId)
    {
        std::lock_guard<std::mutex> lock(g_beastStealthMutex);

        auto it = g_beastStealthStates.find(playerId);

        if (it == g_beastStealthStates.end())
            return false;

        return it->second.active;
    }

    void CancelBeastStealth(
        uint32_t playerId,
        const char* reason)
    {
        uint32_t roomId = 0;
        bool shouldBroadcast = false;

        {
            std::lock_guard<std::mutex> lock(g_beastStealthMutex);

            auto it = g_beastStealthStates.find(playerId);

            if (it == g_beastStealthStates.end())
                return;

            if (!it->second.active)
                return;

            roomId = it->second.roomId;
            it->second.active = false;
            it->second.version++;

            shouldBroadcast = true;
        }

        if (!shouldBroadcast || roomId == 0)
            return;

        BroadcastBeastStealthState(
            roomId,
            playerId,
            false,
            0.0f,
            1.0f,
            BEAST_STEALTH_REVEAL_RANGE
        );

        std::cout
            << "[BEAST STEALTH CANCEL] playerId="
            << playerId
            << " reason="
            << reason
            << std::endl;
    }

    void StartBeastStealth(Session* session)
    {
        if (session == nullptr)
            return;

        uint32_t playerId =
            session->GetPlayerId();

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            return;

        uint64_t version = 0;

        {
            std::lock_guard<std::mutex> lock(g_beastStealthMutex);

            BeastStealthState& state =
                g_beastStealthStates[playerId];

            state.roomId = roomId;
            state.active = true;
            state.speedMultiplier = BEAST_STEALTH_SPEED_MULTIPLIER;
            state.revealRange = BEAST_STEALTH_REVEAL_RANGE;
            state.version++;

            state.endTime =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(
                    static_cast<int>(
                        BEAST_STEALTH_DURATION * 1000.0f
                        )
                );

            version = state.version;
        }

        BroadcastBeastStealthState(
            roomId,
            playerId,
            true,
            BEAST_STEALTH_DURATION,
            BEAST_STEALTH_SPEED_MULTIPLIER,
            BEAST_STEALTH_REVEAL_RANGE
        );

        std::cout
            << "[BEAST STEALTH START] playerId="
            << playerId
            << " roomId="
            << roomId
            << std::endl;

        std::thread(
            [playerId, roomId, version]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<int>(
                            BEAST_STEALTH_DURATION * 1000.0f
                            )
                    )
                );

                bool shouldEnd = false;

                {
                    std::lock_guard<std::mutex> lock(g_beastStealthMutex);

                    auto it =
                        g_beastStealthStates.find(playerId);

                    if (it == g_beastStealthStates.end())
                        return;

                    if (!it->second.active)
                        return;

                    if (it->second.version != version)
                        return;

                    it->second.active = false;
                    it->second.version++;

                    shouldEnd = true;
                }

                if (shouldEnd)
                {
                    BroadcastBeastStealthState(
                        roomId,
                        playerId,
                        false,
                        0.0f,
                        1.0f,
                        BEAST_STEALTH_REVEAL_RANGE
                    );

                    std::cout
                        << "[BEAST STEALTH END AUTO] playerId="
                        << playerId
                        << std::endl;
                }
            }
        ).detach();
    }

    void BroadcastElectricStatus(
        uint32_t roomId,
        uint32_t playerId,
        uint8_t shockStack,
        bool stunned,
        float stunSeconds)
    {
        SC_ElectricStatus out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_ELECTRIC_STATUS);

        out.playerId = playerId;
        out.shockStack = shockStack;
        out.stunned = stunned ? 1 : 0;
        out.stunSeconds = stunSeconds;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[ELECTRIC STATUS BROADCAST] "
            << "roomId=" << roomId
            << " playerId=" << playerId
            << " stack=" << static_cast<int>(shockStack)
            << " stunned=" << stunned
            << " stunSeconds=" << stunSeconds
            << std::endl;
    }

    uint8_t ConsumeElectricShockStack(uint32_t playerId)
    {
        auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(g_electricStatusMutex);

        auto it = g_electricStatuses.find(playerId);

        if (it == g_electricStatuses.end())
            return 0;

        if (it->second.shockStack == 0)
            return 0;

        if (now > it->second.shockExpireTime)
        {
            it->second.shockStack = 0;
            return 0;
        }

        uint8_t stack = it->second.shockStack;

        it->second.shockStack = 0;

        return stack;
    }

    void ApplyElectricStun(
        uint32_t roomId,
        uint32_t playerId,
        float stunSeconds)
    {
        if (stunSeconds <= 0.0f)
            return;

        {
            std::lock_guard<std::mutex> lock(g_electricStatusMutex);

            ElectricStatusState& status =
                g_electricStatuses[playerId];

            auto stunEnd =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(
                    static_cast<int>(stunSeconds * 1000.0f)
                );

            if (stunEnd > status.stunEndTime)
                status.stunEndTime = stunEnd;
        }

        BroadcastElectricStatus(
            roomId,
            playerId,
            0,
            true,
            stunSeconds
        );
    }

    void ClearDronesInRoom(uint32_t roomId, uint8_t reason)
    {
        std::vector<ServerDroneState> removedDrones;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            for (auto it = g_drones.begin(); it != g_drones.end();)
            {
                if (it->second.roomId != roomId)
                {
                    ++it;
                    continue;
                }

                ServerDroneState removed = it->second;
                removedDrones.push_back(removed);

                g_ownerDroneIds.erase(removed.ownerPlayerId);
                it = g_drones.erase(it);
            }
        }

        for (const ServerDroneState& drone : removedDrones)
        {
            BroadcastDroneDestroy(
                roomId,
                drone.droneId,
                drone.ownerPlayerId,
                reason
            );
        }

        if (!removedDrones.empty())
        {
            std::cout << "[DRONE CLEAR ROOM] roomId="
                << roomId
                << " count=" << removedDrones.size()
                << " reason=" << static_cast<int>(reason)
                << std::endl;
        }
    }

    bool IsDroneBlockedAction(Session* session, const char* actionName)
    {
        if (session == nullptr)
            return true;

        if (!IsDroneActiveForPlayer(session->GetPlayerId()))
            return false;
        
        std::cout << "[DRONE ACTION BLOCK] action="
            << actionName
            << " playerId="
            << session->GetPlayerId()
            << std::endl;

        return true;
    }

    void UpdateScannerMoveState(
        uint32_t playerId,
        bool moving)
    {
        std::lock_guard<std::mutex> lock(g_scannerMoveStateMutex);

        PlayerScannerMoveState& state =
            g_scannerMoveStates[playerId];

        state.moving = moving;
        state.lastUpdate = std::chrono::steady_clock::now();
    }

    bool IsPlayerMovingForScanner(uint32_t playerId)
    {
        auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(g_scannerMoveStateMutex);

        auto it = g_scannerMoveStates.find(playerId);

        if (it == g_scannerMoveStates.end())
            return false;

        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.lastUpdate
            ).count();

        if (elapsed > SCANNER_MOVING_TIMEOUT_MS)
            return false;

        return it->second.moving;
    }

    void AddElectricShockStack(
        uint32_t roomId,
        uint32_t playerId,
        uint8_t amount)
    {
        if (amount == 0)
            return;

        uint8_t nextStack = 0;

        {
            std::lock_guard<std::mutex> lock(g_electricStatusMutex);

            ElectricStatusState& status =
                g_electricStatuses[playerId];

            int stack =
                static_cast<int>(status.shockStack) +
                static_cast<int>(amount);

            if (stack > ELECTRIC_MAX_STACK)
                stack = ELECTRIC_MAX_STACK;

            status.shockStack =
                static_cast<uint8_t>(stack);

            status.shockExpireTime =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(ELECTRIC_STACK_EXPIRE_MS);

            nextStack = status.shockStack;
        }

        BroadcastElectricStatus(
            roomId,
            playerId,
            nextStack,
            false,
            0.0f
        );
    }

    void TryTriggerScannersByMovement(Session* actor)
    {
        if (actor == nullptr)
            return;

        if (actor->GetState() != SessionState::IN_GAME)
            return;

        if (actor->IsDead())
            return;

        uint32_t roomId = actor->GetCurrentRoomId();

        if (roomId == 0)
            return;

        uint32_t actorPlayerId = actor->GetPlayerId();

        RoomMember actorMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            actorPlayerId,
            actorMember))
        {
            return;
        }

        Vector3 actorPos = actor->GetPosition();

        auto now = std::chrono::steady_clock::now();

        std::vector<uint32_t> triggeredScannerIds;

        {
            std::lock_guard<std::mutex> lock(g_scannerMutex);

            for (auto& pair : g_scanners)
            {
                ServerScannerState& scanner = pair.second;

                if (!scanner.anchored)
                    continue;

                if (scanner.roomId != roomId)
                    continue;

                if (scanner.ownerPlayerId == actorPlayerId)
                    continue;

                RoomMember ownerMember{};

                if (!RoomManager::Instance().GetMember(
                    roomId,
                    scanner.ownerPlayerId,
                    ownerMember))
                {
                    continue;
                }

                // 핵심: 적군만 감지
                if (ownerMember.team == actorMember.team)
                    continue;

                float dx = actorPos.x - scanner.x;
                float dy = actorPos.y - scanner.y;
                float dz = actorPos.z - scanner.z;

                float distSq =
                    dx * dx +
                    dy * dy +
                    dz * dz;

                if (distSq > SCANNER_RADIUS_SQ)
                    continue;

                uint64_t key =
                    MakeScannerTargetKey(
                        scanner.scannerId,
                        actorPlayerId
                    );
                
                if (g_scannerZappedTargets.find(key) != g_scannerZappedTargets.end())
                    continue;

                g_scannerZappedTargets.insert(key);

                triggeredScannerIds.push_back(scanner.scannerId);
            }
        }

        for (uint32_t scannerId : triggeredScannerIds)
        {
            BroadcastScannerZap(
                roomId,
                scannerId,
                actorPlayerId,
                SCANNER_SHOCK_STACK,
                SCANNER_SHOCK_DURATION
            );

            AddElectricShockStack(
                roomId,
                actorPlayerId,
                1
            );

            std::cout
                << "[SCANNER MOVE TRIGGER] "
                << "scannerId=" << scannerId
                << " target=" << actorPlayerId
                << std::endl;
        }
    }

    void ClearScannerZapRecords(uint32_t scannerId)
    {
        for (auto it = g_scannerZappedTargets.begin();
            it != g_scannerZappedTargets.end();)
        {
            uint32_t recordScannerId =
                static_cast<uint32_t>((*it) >> 32);

            if (recordScannerId == scannerId)
                it = g_scannerZappedTargets.erase(it);
            else
                ++it;
        }
    }

    float GetStunDurationByStack(uint8_t stack)
    {
        /*if (stack == 1)
            return 0.8f;

        if (stack == 2)
            return 1.3f;

        if (stack >= 3)
            return 1.8f;*/

        if (stack == 1)
            return 3.0f;

        if (stack == 2)
            return 4.0f;

        if (stack >= 3)
            return 5.0f;

        return 0.0f;
    }

    bool IsPlayerStunned(uint32_t playerId)
    {
        auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(g_electricStatusMutex);

        auto it = g_electricStatuses.find(playerId);

        if (it == g_electricStatuses.end())
            return false;

        return now < it->second.stunEndTime;
    }

    

    void BroadcastEmpCast(
        uint32_t roomId,
        uint32_t casterPlayerId,
        const std::vector<uint32_t>& targetPlayerIds)
    {
        SC_EmpCast out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(PacketType::PKT_SC_EMP_CAST);

        out.casterPlayerId = casterPlayerId;

        uint8_t count =
            static_cast<uint8_t>(
                std::min<size_t>(
                    targetPlayerIds.size(),
                    EMP_MAX_TARGET_COUNT
                )
                );

        out.targetCount = count;

        for (int i = 0; i < count; i++)
        {
            out.targetPlayerIds[i] = targetPlayerIds[i];
        }

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[EMP CAST BROADCAST] "
            << "roomId=" << roomId
            << " caster=" << casterPlayerId
            << " targetCount=" << static_cast<int>(count)
            << std::endl;
    }

    void BroadcastUltimateCast(
        uint32_t roomId,
        uint32_t casterPlayerId,
        float dirX,
        float dirY,
        float dirZ,
        const std::vector<uint32_t>& targetPlayerIds,
        int32_t damage,
        uint8_t stack)
    {
        SC_UltimateCast out{};

        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_ULTIMATE_CAST
                );

        out.casterPlayerId = casterPlayerId;

        out.dirX = dirX;
        out.dirY = dirY;
        out.dirZ = dirZ;

        uint8_t count =
            static_cast<uint8_t>(
                std::min<size_t>(
                    targetPlayerIds.size(),
                    ULTIMATE_MAX_TARGET_COUNT
                )
                );

        out.targetCount = count;

        for (int i = 0; i < count; i++)
        {
            out.targetPlayerIds[i] =
                targetPlayerIds[i];
        }

        out.damage = damage;
        out.stack = stack;

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout
            << "[ULTIMATE CAST BROADCAST] roomId="
            << roomId
            << " caster="
            << casterPlayerId
            << " targetCount="
            << static_cast<int>(count)
            << " damage="
            << damage
            << " stack="
            << static_cast<int>(stack)
            << std::endl;
    }


}

namespace
{
    bool TryGetConsumingSkillSlot(
        PacketType packetType,
        PlayerSkillSlot& outSlot)
    {
        switch (packetType)
        {
        case PacketType::PKT_CS_SCANNER_THROW:
        case PacketType::PKT_CS_ENTRY_DASH_CAST:
        case PacketType::PKT_CS_CONTROLLER_DECOY_THROW:
        case PacketType::PKT_CS_SENTINEL_C_THROW:
            outSlot = PlayerSkillSlot::C;
            return true;

        case PacketType::PKT_CS_DRONE_DEPLOY:
        case PacketType::PKT_CS_ENTRY_SMOKE_THROW:
        case PacketType::PKT_CS_CONTROLLER_SWAMP_THROW:
        case PacketType::PKT_CS_SENTINEL_Q_PLACE:
            outSlot = PlayerSkillSlot::Q;
            return true;

        case PacketType::PKT_CS_EMP_CAST:
        case PacketType::PKT_CS_BEAST_STEALTH_CAST:
        case PacketType::PKT_CS_CONTROLLER_SMOKE_DROP:
        case PacketType::PKT_CS_SENTINEL_E_PLACE:
            outSlot = PlayerSkillSlot::E;
            return true;

        case PacketType::PKT_CS_ULTIMATE_CAST:
        case PacketType::PKT_CS_ENTRY_BLADE_ACTIVATE:
        case PacketType::PKT_CS_CONTROLLER_ULTIMATE_CAST:
        case PacketType::PKT_CS_SENTINEL_X_CAST:
            outSlot = PlayerSkillSlot::X;
            return true;

        default:
            return false;
        }
    }

    bool TryConsumeSkillForPacket(Session* session, PacketType packetType)
    {
        PlayerSkillSlot slot = PlayerSkillSlot::C;
        if (!TryGetConsumingSkillSlot(packetType, slot))
            return true;

        bool validPlayer =
            session != nullptr &&
            session->GetState() == SessionState::IN_GAME &&
            session->GetCurrentRoomId() != 0 &&
            !session->IsDead();

        if (!validPlayer || !session->ConsumeSkill(slot))
        {
            SendSkillState(session);
            std::cout << "[SKILL USE BLOCK] playerId="
                << (session != nullptr ? session->GetPlayerId() : 0)
                << " slot=" << static_cast<int>(slot)
                << std::endl;
            return false;
        }

        SendSkillState(session);
        std::cout << "[SKILL CONSUMED] playerId="
            << session->GetPlayerId()
            << " slot=" << static_cast<int>(slot)
            << std::endl;
        return true;
    }
}

void PacketDispatcher::Dispatch(Session* session, PacketHeader* header)
{
    PacketType type = (PacketType)header->type;

    std::cout << "[DISPATCH BEGIN] session=" << session
        << " playerId=" << session->GetPlayerId()
        << " userId=" << session->GetUserId()
        << " type=" << (uint16_t)type
        << " size=" << header->size
        << std::endl;

    switch (type)
    {
    case PacketType::PKT_CS_REGISTER:
    case PacketType::PKT_CS_LOGIN:
        break;

    default:
    {
        if (!session->IsAuthenticated())
        {
            std::cout << "[AUTH BLOCK] session=" << session
                << " playerId=" << session->GetPlayerId()
                << " type=" << (uint16_t)type
                << " userId=" << session->GetUserId()
                << std::endl;
            return;
        }
        break;
    }
    }

    if (!TryConsumeSkillForPacket(session, type))
        return;

    // Serialize lobby state transitions against ready-check completion.
    std::unique_lock<std::recursive_mutex> socialLock(g_matchmakingMutex, std::defer_lock);
    if (type == PacketType::PKT_CS_ROOM_CREATE || type == PacketType::PKT_CS_ROOM_ENTER ||
        type == PacketType::PKT_CS_TEMP_GAME_ROOM_ENTER || type == PacketType::PKT_CS_TEMP_GAME_START ||
        type == PacketType::PKT_CS_MATCHMAKING_CANCEL || type == PacketType::PKT_CS_LOBBY_ENTER ||
        type == PacketType::PKT_CS_RESULT_LEAVE)
        socialLock.lock();
    if (type == PacketType::PKT_CS_ROOM_CREATE || type == PacketType::PKT_CS_ROOM_ENTER)
    {
        if (SocialBusy(session->GetPlayerId()) || !g_lobbyParties.IsSolo(session->GetPlayerId()))
        { SocialResult(session, 4); return; }
        g_lobbyParties.Invalidate(session->GetPlayerId());
    }
    if ((type >= PacketType::PKT_CS_PARTY_INVITE && type <= PacketType::PKT_CS_PARTY_STATE) || type == PacketType::PKT_CS_PARTY_MODE)
    {
        EnsureMatchmakingWorkerStarted();
        HandleSocialPacket(session, header);
        return;
    }

    switch (type)
    {
    case PacketType::PKT_CS_REGISTER:
    {
        auto* pkt = reinterpret_cast<CS_Register*>(header);

        std::cout << "[REGISTER PACKET] id=" << pkt->id
            << " nick=" << pkt->nickname
            << std::endl;

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

        std::cout << "[REGISTER RESULT SENT] result=" << (int)result << std::endl;
        break;
    }

    case PacketType::PKT_CS_LOGIN:
    {
        auto* pkt = reinterpret_cast<CS_Login*>(header);

        std::cout << "[LOGIN PACKET] id=" << pkt->id
            << " session=" << session
            << " alreadyAuth=" << session->IsAuthenticated()
            << std::endl;

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

        if (result == (uint8_t)LoginResult::SUCCESS)
        {
            bool loginOk = UserManager::Instance().OnLogin(pkt->id, session);

            if (!loginOk)
            {
                result = (uint8_t)LoginResult::ALREADY_LOGIN;

                std::cout << "[LOGIN FAIL] already logged in id=" << pkt->id
                    << " session=" << session
                    << std::endl;
            }
            else
            {
                session->SetAuthenticated(true);
                session->SetState(SessionState::AUTHENTICATED);

                uint32_t newPlayerId = SessionManager::Instance().AllocatePlayerId();
                session->SetPlayerId(newPlayerId);
                SessionManager::Instance().Add(session);

                std::cout << "[LOGIN SUCCESS READY] id=" << pkt->id
                    << " playerId=" << newPlayerId
                    << " session=" << session
                    << std::endl;
            }
        }

        SC_LoginResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOGIN_RESULT;
        out.result = result;

        session->Send(&out);

        std::cout << "[LOGIN RESULT SENT] id=" << pkt->id
            << " result=" << (int)result
            << " playerId=" << session->GetPlayerId()
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_LOBBY_ENTER:
    {
        PlayerData data{};
        bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);
        if (!ok)
            break;

        session->SetState(SessionState::IN_LOBBY);

        SC_LobbyEnter out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_ENTER;

        strncpy_s(out.nickname, data.nickname.c_str(), _TRUNCATE);

        out.level = data.level;
        out.currentExp = data.currentExp;
        out.requiredExp = UserManager::Instance().GetRequiredExpForLevel(data.level);

        out.playerIconId = data.playerIconId;
        out.lobbyCharacterModelId = data.lobbyCharacterModelId;
        out.lobbyBackgroundId = data.lobbyBackgroundId;
        out.selfPlayerId = session->GetPlayerId();

        CareerWallet wallet{};
        if (CareerManager::Instance().GetWallet(session->GetUserId(), wallet))
        {
            out.freeCurrency = wallet.freeCurrency;
            out.victoryCurrency = wallet.victoryCurrency;
        }

        session->Send(&out);

        std::cout << "[LOBBY ENTER] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " nickname=" << data.nickname
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_LOBBY_CHARACTER_SELECT:
    {
        auto* pkt = reinterpret_cast<CS_LobbyCharacterSelect*>(header);

        bool owned = UserManager::Instance().IsCharacterOwned(
            session->GetUserId(),
            pkt->lobbyCharacterModelId
        );

        bool ok = false;

        if (owned)
        {
            ok = UserManager::Instance().SetLobbyCharacterModel(
                session->GetUserId(),
                pkt->lobbyCharacterModelId
            );
        }

        SC_LobbyCharacterSelectResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_CHARACTER_SELECT_RESULT;
        out.result = ok ? 1 : 0;
        out.lobbyCharacterModelId = pkt->lobbyCharacterModelId;

        session->Send(&out);

        break;
    }

    case PacketType::PKT_CS_LOBBY_BACKGROUND_SELECT:
    {
        auto* pkt = reinterpret_cast<CS_LobbyBackgroundSelect*>(header);

        bool ok = UserManager::Instance().SetLobbyBackground(
            session->GetUserId(),
            pkt->lobbyBackgroundId
        );

        SC_LobbyBackgroundSelectResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_BACKGROUND_SELECT_RESULT;
        out.result = ok ? 1 : 0;
        out.lobbyBackgroundId = pkt->lobbyBackgroundId;

        session->Send(&out);

        break;
    }

    case PacketType::PKT_CS_WEAPON_SKIN_LOADOUT_REQUEST:
    {
        if (header->size != sizeof(CS_WeaponSkinLoadoutRequest))
            break;

        auto* pkt = reinterpret_cast<CS_WeaponSkinLoadoutRequest*>(header);
        std::array<int, WEAPON_SKIN_WEAPON_COUNT> equippedSkins{};
        bool ok = WeaponSkinManager::Instance().GetEquippedSkins(
            session->GetUserId(),
            pkt->characterId,
            equippedSkins
        );

        if (!ok)
        {
            WeaponSkinManager::Instance().InitializeUser(session->GetUserId());
            ok = WeaponSkinManager::Instance().GetEquippedSkins(
                session->GetUserId(),
                pkt->characterId,
                equippedSkins
            );
        }

        SC_WeaponSkinLoadout out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_WEAPON_SKIN_LOADOUT
        );
        out.characterId = pkt->characterId;
        out.count = ok ? MAX_WEAPON_SKIN_LOADOUT_COUNT : 0;

        for (int weaponId = 0; weaponId < MAX_WEAPON_SKIN_LOADOUT_COUNT; ++weaponId)
        {
            out.slots[weaponId].weaponId = weaponId;
            out.slots[weaponId].skinId = equippedSkins[weaponId];
        }

        session->Send(&out);
        break;
    }

    case PacketType::PKT_CS_WEAPON_SKIN_SELECT:
    {
        if (header->size != sizeof(CS_WeaponSkinSelect))
            break;

        auto* pkt = reinterpret_cast<CS_WeaponSkinSelect*>(header);
        SessionState state = session->GetState();
        bool stateAllowsLoadoutChange = state != SessionState::IN_GAME;
        bool ok = stateAllowsLoadoutChange &&
            WeaponSkinManager::Instance().SetEquippedSkin(
            session->GetUserId(),
            pkt->characterId,
            pkt->weaponId,
            pkt->skinId
        );

        SC_WeaponSkinSelectResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_WEAPON_SKIN_SELECT_RESULT
        );
        out.result = ok ? 1 : 0;
        out.characterId = pkt->characterId;
        out.weaponId = pkt->weaponId;
        out.skinId = pkt->skinId;

        session->Send(&out);

        std::cout << "[WEAPON SKIN SELECT] userId=" << session->GetUserId()
            << " characterId=" << pkt->characterId
            << " state=" << static_cast<int>(state)
            << " weaponId=" << pkt->weaponId
            << " skinId=" << pkt->skinId
            << " success=" << ok
            << std::endl;
        break;
    }

    case PacketType::PKT_CS_CAREER_DETAIL_REQUEST:
    {
        if (header->size != sizeof(CS_CareerDetailRequest))
            break;

        const auto* request =
            reinterpret_cast<const CS_CareerDetailRequest*>(header);
        size_t targetLength = 0;
        while (targetLength < USER_ID_LEN &&
            request->targetUserId[targetLength] != '\0')
        {
            ++targetLength;
        }
        std::string targetUserId(request->targetUserId, targetLength);
        if (targetUserId.empty())
            targetUserId = session->GetUserId();

        CareerDetail detail{};
        bool ok = CareerManager::Instance().GetCareerDetail(
            targetUserId,
            request->seasonId,
            request->modeType,
            request->characterId,
            detail);

        SC_CareerDetail out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_CAREER_DETAIL);
        out.result = ok ? 0 : 1;

        if (ok)
        {
            out.hasData = detail.hasData ? 1 : 0;
            out.seasonId = detail.seasonId;
            out.modeType = static_cast<uint8_t>(detail.modeType);
            out.selectedCharacterId = detail.selectedCharacterId;
            size_t seasonCount = std::min(
                detail.seasons.size(),
                static_cast<size_t>(CAREER_DETAIL_SEASON_COUNT));
            out.seasonCount = static_cast<uint8_t>(seasonCount);
            for (size_t i = 0; i < seasonCount; ++i)
            {
                out.seasons[i].seasonId = detail.seasons[i].seasonId;
                strncpy_s(
                    out.seasons[i].seasonName,
                    sizeof(out.seasons[i].seasonName),
                    detail.seasons[i].seasonName.c_str(),
                    _TRUNCATE);
                out.seasons[i].isActive = detail.seasons[i].isActive ? 1 : 0;
            }

            size_t characterCount = std::min(
                detail.characters.size(),
                static_cast<size_t>(CAREER_DETAIL_CHARACTER_COUNT));
            for (size_t i = 0; i < characterCount; ++i)
            {
                const CareerDetailCharacterStats& source = detail.characters[i];
                CareerDetailCharacterPacketEntry& destination = out.characters[i];
                destination.characterId = source.characterId;
                destination.performanceScore = source.performanceScore;
                destination.matches = source.matches;
                destination.wins = source.wins;
                destination.kills = source.kills;
                destination.deaths = source.deaths;
                destination.maxKills = source.maxKills;
                destination.maxDamage = source.maxDamage;
            }

            out.playCount = detail.game.playCount;
            out.wins = detail.game.wins;
            out.roundsPlayed = detail.game.roundsPlayed;
            out.totalDamage = detail.game.totalDamage;
            out.kills = detail.game.kills;
            out.deaths = detail.game.deaths;
            out.assists = detail.game.assists;
            out.headshotKills = detail.game.headshotKills;
            out.meleeKills = detail.game.meleeKills;
            out.spikePlants = detail.game.spikePlants;
            out.spikeDefuses = detail.game.spikeDefuses;
            out.maxAssists = detail.game.maxAssists;
            out.maxRoundDamageHundredths =
                detail.game.maxRoundDamageHundredths;
            out.maxRoundKillsHundredths =
                detail.game.maxRoundKillsHundredths;
        }

        session->Send(&out);
        break;
    }
    case PacketType::PKT_CS_CAREER_OVERVIEW_REQUEST:
    {
        if (header->size != sizeof(CS_CareerOverviewRequest))
            break;

        CareerOverview overview{};
        bool ok = CareerManager::Instance().GetOverview(
            session->GetUserId(),
            overview
        );

        SC_CareerOverview out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_CAREER_OVERVIEW
        );

        if (ok)
        {
            out.seasonId = overview.season.seasonId;
            strncpy_s(
                out.seasonName,
                sizeof(out.seasonName),
                overview.season.seasonName.c_str(),
                _TRUNCATE
            );
            out.seasonEndUnix = overview.season.endUnix;
            out.remainingSeasonDays = overview.remainingSeasonDays;
            out.tierId = overview.tier.tierId;
            strncpy_s(
                out.tierName,
                sizeof(out.tierName),
                overview.tier.tierName.c_str(),
                _TRUNCATE
            );
            out.rankImageId = overview.tier.rankImageId;
            out.tierMinPoints = overview.tier.minPoints;
            out.tierMaxPoints = overview.tier.maxPoints;
            out.rankPoints = overview.stats.rankPoints;
            out.rankedMatches = overview.stats.rankedMatches;
            out.wins = overview.stats.wins;
            out.roundsPlayed = overview.stats.roundsPlayed;
            out.kills = overview.stats.kills;
            out.totalDamage = overview.stats.totalDamage;
            out.mvpCount = overview.stats.mvpCount;
        }

        session->Send(&out);
        break;
    }

    case PacketType::PKT_CS_CAREER_RANKING_REQUEST:
    {
        if (header->size != sizeof(CS_CareerRankingRequest))
            break;

        const CS_CareerRankingRequest* request =
            reinterpret_cast<const CS_CareerRankingRequest*>(header);
        size_t queryLength = 0;
        while (queryLength < CAREER_RANKING_QUERY_LEN &&
            request->searchQuery[queryLength] != '\0')
        {
            ++queryLength;
        }

        CareerRankingPage rankingPage =
            CareerManager::Instance().GetRankingPage(
                request->rankingType,
                request->characterId,
                request->page,
                CAREER_RANKING_COUNT,
                std::string(request->searchQuery, queryLength)
            );

        SC_CareerRanking out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_CAREER_RANKING
        );
        out.rankingType = static_cast<uint8_t>(rankingPage.rankingType);
        out.characterId = rankingPage.characterId;
        out.page = static_cast<uint8_t>(rankingPage.page);
        out.totalPages = static_cast<uint8_t>(std::max(
            1,
            std::min(rankingPage.totalPages, 255)));
        out.totalEntries = rankingPage.totalEntries;
        out.count = static_cast<uint8_t>(rankingPage.entries.size());

        for (size_t i = 0; i < rankingPage.entries.size(); ++i)
        {
            const CareerRankingEntry& source = rankingPage.entries[i];
            CareerRankingPacketEntry& destination = out.entries[i];
            destination.position = source.position;
            strncpy_s(
                destination.userId,
                sizeof(destination.userId),
                source.userId.c_str(),
                _TRUNCATE
            );
            strncpy_s(
                destination.nickname,
                sizeof(destination.nickname),
                source.nickname.c_str(),
                _TRUNCATE
            );
            PlayerData playerData{};
            destination.playerIconId =
                UserManager::Instance().GetPlayerData(
                    source.userId,
                    playerData
                )
                ? playerData.playerIconId
                : 0;
            destination.tierId = source.tier.tierId;
            strncpy_s(
                destination.tierName,
                sizeof(destination.tierName),
                source.tier.tierName.c_str(),
                _TRUNCATE
            );
            destination.rankImageId = source.tier.rankImageId;
            destination.rankPoints = source.score;
            destination.rankedMatches = source.matches;
            destination.wins = source.wins;
        }

        session->Send(&out);
        break;
    }

    case PacketType::PKT_CS_CAREER_MATCH_HISTORY_REQUEST:
    {
        if (header->size != sizeof(CS_CareerMatchHistoryRequest))
            break;

        const auto* request =
            reinterpret_cast<const CS_CareerMatchHistoryRequest*>(header);
        int filterType = std::max(
            0,
            std::min(static_cast<int>(request->filterType), 2));

        std::vector<CareerMatchHistoryEntry> history =
            CareerManager::Instance().GetMatchHistory(
                session->GetUserId(),
                filterType,
                CAREER_MATCH_HISTORY_COUNT
            );

        SC_CareerMatchHistory out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_CAREER_MATCH_HISTORY
        );
        out.filterType = static_cast<uint8_t>(filterType);
        out.count = static_cast<uint8_t>(history.size());

        for (size_t i = 0; i < history.size(); ++i)
        {
            const CareerMatchHistoryEntry& source = history[i];
            CareerMatchPacketEntry& destination = out.matches[i];
            destination.matchId = source.match.matchId;
            strncpy_s(
                destination.mapName,
                sizeof(destination.mapName),
                source.match.mapName.c_str(),
                _TRUNCATE
            );
            destination.startedAtUnix = source.match.startedAtUnix;
            destination.durationSeconds = source.match.durationSeconds;
            destination.scoreA = static_cast<uint8_t>(source.match.scoreA);
            destination.scoreB = static_cast<uint8_t>(source.match.scoreB);
            destination.winnerTeam = static_cast<uint8_t>(source.match.winnerTeam);
            destination.myTeam = static_cast<uint8_t>(source.myTeam);

            size_t playerCount = std::min(
                source.players.size(),
                static_cast<size_t>(CAREER_MATCH_PLAYER_COUNT)
            );
            destination.playerCount = static_cast<uint8_t>(playerCount);

            for (size_t playerIndex = 0; playerIndex < playerCount; ++playerIndex)
            {
                const CareerMatchPlayer& playerSource = source.players[playerIndex];
                CareerMatchPlayerPacketEntry& playerDestination =
                    destination.players[playerIndex];

                strncpy_s(
                    playerDestination.userId,
                    sizeof(playerDestination.userId),
                    playerSource.userId.c_str(),
                    _TRUNCATE
                );
                strncpy_s(
                    playerDestination.nickname,
                    sizeof(playerDestination.nickname),
                    playerSource.nickname.c_str(),
                    _TRUNCATE
                );
                playerDestination.team = static_cast<uint8_t>(playerSource.team);
                playerDestination.characterId = playerSource.characterId;
                playerDestination.combatScore = playerSource.combatScore;
                playerDestination.attackScore = playerSource.attackScore;
                playerDestination.skillScore = playerSource.skillScore;
                playerDestination.teamworkScore = playerSource.teamworkScore;
                playerDestination.kills = playerSource.kills;
                playerDestination.deaths = playerSource.deaths;
                playerDestination.assists = playerSource.assists;
                playerDestination.damage = playerSource.damage;
                playerDestination.isMvp = playerSource.isMvp ? 1 : 0;
            }
        }

        session->Send(&out);
        break;
    }

    case PacketType::PKT_CS_CAREER_INVENTORY_REQUEST:
    {
        if (header->size != sizeof(CS_CareerInventoryRequest))
            break;

        std::vector<CareerInventoryEntry> inventory =
            CareerManager::Instance().GetInventory(
                session->GetUserId(),
                CAREER_INVENTORY_COUNT
            );

        SC_CareerInventory out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_CAREER_INVENTORY
        );
        out.count = static_cast<uint8_t>(inventory.size());

        for (size_t i = 0; i < inventory.size(); ++i)
        {
            const CareerInventoryEntry& source = inventory[i];
            CareerInventoryPacketEntry& destination = out.entries[i];
            destination.itemId = source.item.itemId;
            strncpy_s(
                destination.itemName,
                sizeof(destination.itemName),
                source.item.itemName.c_str(),
                _TRUNCATE
            );
            strncpy_s(
                destination.description,
                sizeof(destination.description),
                source.item.description.c_str(),
                _TRUNCATE
            );
            destination.iconId = source.item.iconId;
            destination.quantity = source.quantity;
            destination.acquiredAtUnix = source.acquiredAtUnix;
        }

        session->Send(&out);
        break;
    }

    case PacketType::PKT_CS_LOBBY_CHAT:
    {
        SessionState lobbyChatState = session->GetState();

        if (lobbyChatState != SessionState::IN_LOBBY &&
            lobbyChatState != SessionState::IN_ROOM)
        {
            std::cout << "[LOBBY CHAT BLOCK] not in lobby playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (lobbyChatState == SessionState::IN_ROOM)
        {
            Room waitingRoom{};
            if (!RoomManager::Instance().GetRoom(
                    session->GetCurrentRoomId(),
                    waitingRoom) ||
                waitingRoom.state != RoomState::WAITING)
            {
                std::cout << "[LOBBY CHAT BLOCK] room is not waiting playerId="
                    << session->GetPlayerId()
                    << std::endl;
                break;
            }
        }

        auto* pkt = reinterpret_cast<CS_LobbyChat*>(header);

        PlayerData data{};
        bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);
        if (!ok)
            break;

        SC_LobbyChat out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_CHAT;
        out.playerId = session->GetPlayerId();

        strncpy_s(out.nickname, data.nickname.c_str(), _TRUNCATE);
        strncpy_s(out.message, pkt->message, _TRUNCATE);

        std::vector<std::shared_ptr<Session>> lobbyTargets =
            SessionManager::Instance().GetAll();

        for (const std::shared_ptr<Session>& target : lobbyTargets)
        {
            if (target == nullptr)
                continue;

            bool canReceive = target->GetState() == SessionState::IN_LOBBY;

            if (target->GetState() == SessionState::IN_ROOM)
            {
                Room targetRoom{};
                canReceive = RoomManager::Instance().GetRoom(
                    target->GetCurrentRoomId(),
                    targetRoom
                ) && targetRoom.state == RoomState::WAITING;
            }

            if (canReceive)
                target->Send(&out);
        }

        break;
    }

    case PacketType::PKT_CS_ROOM_CHAT:
    {
        if (header->size != sizeof(CS_RoomChat))
        {
            std::cout << "[ROOM CHAT BLOCK] invalid size="
                << header->size
                << std::endl;
            break;
        }

        SessionState state = session->GetState();
        if (state != SessionState::IN_LOBBY &&
            state != SessionState::IN_ROOM)
        {
            std::cout << "[ROOM CHAT BLOCK] invalid state playerId="
                << session->GetPlayerId()
                << " state=" << static_cast<int>(state)
                << std::endl;
            break;
        }

        auto* pkt = reinterpret_cast<CS_RoomChat*>(header);
        size_t messageLength = strnlen_s(
            pkt->message,
            ROOM_CHAT_MESSAGE_LEN
        );

        if (messageLength == 0 ||
            messageLength >= ROOM_CHAT_MESSAGE_LEN)
        {
            std::cout << "[ROOM CHAT BLOCK] invalid message length="
                << messageLength
                << std::endl;
            break;
        }

        PlayerData data{};
        if (!UserManager::Instance().GetPlayerData(
                session->GetUserId(),
                data))
        {
            break;
        }

        SC_RoomChat out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_ROOM_CHAT
        );
        out.playerId = session->GetPlayerId();

        strncpy_s(
            out.nickname,
            sizeof(out.nickname),
            data.nickname.c_str(),
            _TRUNCATE
        );
        strncpy_s(
            out.message,
            sizeof(out.message),
            pkt->message,
            _TRUNCATE
        );

        // The temporary ready room has no server Room yet. Until party
        // support is added, keep that channel alive as a private self echo.
        if (state == SessionState::IN_LOBBY)
        {
            out.roomId = 0;
            session->Send(&out);
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();
        Room room{};
        RoomMember sender{};

        if (roomId == 0 ||
            !RoomManager::Instance().GetRoom(roomId, room) ||
            !RoomManager::Instance().GetMember(
                roomId,
                session->GetPlayerId(),
                sender))
        {
            std::cout << "[ROOM CHAT BLOCK] room membership missing playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        out.roomId = roomId;

        for (const RoomMember& member : room.members)
        {
            std::shared_ptr<Session> target =
                SessionManager::Instance().Find(member.playerId);

            if (target != nullptr)
                target->Send(&out);
        }

        std::cout << "[ROOM CHAT] roomId=" << roomId
            << " sender=" << session->GetPlayerId()
            << " targetCount=" << room.members.size()
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_FRIEND_CHAT:
    {
        if (header->size != sizeof(CS_FriendChat))
        {
            std::cout << "[FRIEND CHAT BLOCK] invalid size="
                << header->size
                << std::endl;
            break;
        }

        SessionState senderState = session->GetState();
        if (senderState != SessionState::IN_LOBBY &&
            senderState != SessionState::IN_ROOM)
        {
            std::cout << "[FRIEND CHAT BLOCK] invalid sender state playerId="
                << session->GetPlayerId()
                << " state=" << static_cast<int>(senderState)
                << std::endl;
            break;
        }

        auto* pkt = reinterpret_cast<CS_FriendChat*>(header);
        if (pkt->targetPlayerId == 0 ||
            pkt->targetPlayerId == session->GetPlayerId())
        {
            std::cout << "[FRIEND CHAT BLOCK] invalid target sender="
                << session->GetPlayerId()
                << " target=" << pkt->targetPlayerId
                << std::endl;
            break;
        }

        size_t messageLength = strnlen_s(
            pkt->message,
            FRIEND_CHAT_MESSAGE_LEN
        );
        if (messageLength == 0 ||
            messageLength >= FRIEND_CHAT_MESSAGE_LEN)
        {
            std::cout << "[FRIEND CHAT BLOCK] invalid message length="
                << messageLength
                << std::endl;
            break;
        }

        std::shared_ptr<Session> target =
            SessionManager::Instance().Find(pkt->targetPlayerId);
        if (target == nullptr)
        {
            std::cout << "[FRIEND CHAT BLOCK] offline target="
                << pkt->targetPlayerId
                << std::endl;
            break;
        }

        SessionState targetState = target->GetState();
        if (targetState != SessionState::IN_LOBBY &&
            targetState != SessionState::IN_ROOM)
        {
            std::cout << "[FRIEND CHAT BLOCK] unavailable target="
                << pkt->targetPlayerId
                << " state=" << static_cast<int>(targetState)
                << std::endl;
            break;
        }

        if (!FriendManager::Instance().AreFriends(
                session->GetUserId(),
                target->GetUserId()) ||
            FriendManager::Instance().IsBlockedEitherWay(
                session->GetUserId(),
                target->GetUserId()))
        {
            std::cout << "[FRIEND CHAT BLOCK] not friends sender="
                << session->GetUserId()
                << " target=" << target->GetUserId()
                << std::endl;
            break;
        }

        PlayerData senderData{};
        if (!UserManager::Instance().GetPlayerData(
                session->GetUserId(),
                senderData))
        {
            break;
        }

        SC_FriendChat out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_CHAT
        );
        out.senderPlayerId = session->GetPlayerId();
        out.targetPlayerId = pkt->targetPlayerId;

        strncpy_s(
            out.senderNickname,
            sizeof(out.senderNickname),
            senderData.nickname.c_str(),
            _TRUNCATE
        );
        strncpy_s(
            out.message,
            sizeof(out.message),
            pkt->message,
            _TRUNCATE
        );

        target->Send(&out);
        session->Send(&out);

        std::cout << "[FRIEND CHAT] sender="
            << out.senderPlayerId
            << " target=" << out.targetPlayerId
            << std::endl;
        break;
    }
    case PacketType::PKT_CS_FRIEND_OVERVIEW_REQUEST:
    case PacketType::PKT_CS_FRIEND_PRESENCE_SET:
    case PacketType::PKT_CS_FRIEND_SEARCH:
    case PacketType::PKT_CS_FRIEND_RECOMMEND_REQUEST:
    case PacketType::PKT_CS_FRIEND_OPERATION:
    case PacketType::PKT_CS_FRIEND_REQUEST_LIST_REQUEST:
    case PacketType::PKT_CS_FRIEND_RECENT_REQUEST:
    case PacketType::PKT_CS_FRIEND_MEMO_SET:
        FriendPacketService::Dispatch(session, header);
        break;

    case static_cast<PacketType>(340): // Independent cosmetic shop request
        ShopPacketService::Dispatch(session, header);
        break;

    case PacketType::PKT_CS_PROFILE_REQUEST:
    case PacketType::PKT_CS_PROFILE_PRIVACY_SET:
    case PacketType::PKT_CS_PROFILE_LIKE:
    case PacketType::PKT_CS_PROFILE_BATTLE_STATS_SET:
    case PacketType::PKT_CS_CHIPCRAFT_REQUEST:
    case PacketType::PKT_CS_CHIPCRAFT_SAVE:
    case PacketType::PKT_CS_LEVEL_REWARD_REQUEST:
    case PacketType::PKT_CS_LEVEL_REWARD_CLAIM:
    case PacketType::PKT_CS_HONOR_REQUEST:
    case PacketType::PKT_CS_HONOR_CLAIM:
    case static_cast<PacketType>(334): // Event pass request.
        if (ProfilePacketService::Dispatch(session, header))
        {
            std::lock_guard<std::recursive_mutex> lock(g_matchmakingMutex);
            if (session->IsInLobby() && session->GetCurrentRoomId() == 0 &&
                g_lobbyParties.Members(session->GetPlayerId()).size() > 1)
                BroadcastParty(session->GetPlayerId());
        }
        break;

    case PacketType::PKT_CS_LOBBY_CHARACTER_LIST:
    {
        CharacterOwnedData ownedData{};
        PlayerData playerData{};

        bool okOwned = UserManager::Instance().GetCharacterOwnedData(session->GetUserId(), ownedData);
        bool okPlayer = UserManager::Instance().GetPlayerData(session->GetUserId(), playerData);
        if (!okOwned || !okPlayer)
            break;

        SC_LobbyCharacterList out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_LOBBY_CHARACTER_LIST;
        out.count = MAX_LOBBY_CHARACTER_COUNT;
        out.currentCharacterModelId = playerData.lobbyCharacterModelId;

        out.characters[0].characterId = 0;
        out.characters[0].owned = (uint8_t)(ownedData.c0 ? 1 : 0);

        out.characters[1].characterId = 1;
        out.characters[1].owned = (uint8_t)(ownedData.c1 ? 1 : 0);

        out.characters[2].characterId = 2;
        out.characters[2].owned = (uint8_t)(ownedData.c2 ? 1 : 0);

        out.characters[3].characterId = 3;
        out.characters[3].owned = (uint8_t)(ownedData.c3 ? 1 : 0);

        session->Send(&out);

        break;
    }

    case PacketType::PKT_CS_TEMP_GAME_ROOM_ENTER:
    {
        if (header->size == sizeof(CS_TempGameRoomEnter) && session->IsInLobby()) SendParty(session);
        break;
    }

    case PacketType::PKT_CS_TEMP_GAME_START:
    {
        if (header->size != sizeof(CS_TempGameStart)) break;
        const auto mode = reinterpret_cast<CS_TempGameStart*>(header)->mode;
        if (mode > 1) { SocialResult(session, 3); break; }
        QueueSocialParty(session, mode);
        EnsureMatchmakingWorkerStarted();
        break;
    }

    case PacketType::PKT_CS_MATCHMAKING_CANCEL:
    {
        if (header->size != sizeof(CS_MatchmakingCancel)) break;
        auto entry = std::find_if(g_matchmakingQueue.begin(), g_matchmakingQueue.end(),
            [session](const MatchmakingEntry& value) { return value.playerId == session->GetPlayerId(); });
        if (entry == g_matchmakingQueue.end())
        {
            // A ready check owns cancellation once a match has been found.
            SendMatchmakingState(session, 2, 3, 0, 0, GetInitialMatchmakingRange(0), 0);
            break;
        }
        const auto party = entry->partyId;
        g_matchmakingQueue.erase(std::remove_if(g_matchmakingQueue.begin(), g_matchmakingQueue.end(),
            [party](const MatchmakingEntry& value)
            {
                if (value.partyId != party) return false;
                SendMatchmakingState(SessionManager::Instance().Find(value.playerId).get(), 0, 2, value.mode, 0, GetInitialMatchmakingRange(value.mode), 0);
                return true;
            }), g_matchmakingQueue.end());
        break;
    }

    case PacketType::PKT_CS_ROOM_LIST:
    {
        std::cout << "[ROOM LIST REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << std::endl;

        SC_RoomList out{};
        out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_LIST;

        std::vector<Room> roomList = RoomManager::Instance().GetRoomList();

        uint16_t count = static_cast<uint16_t>(roomList.size());
        if (count > MAX_ROOM_LIST_COUNT)
            count = MAX_ROOM_LIST_COUNT;

        out.roomCount = count;

        for (uint16_t i = 0; i < count; ++i)
        {
            const Room& room = roomList[i];
            RoomInfo& info = out.rooms[i];

            info.roomId = room.roomId;

            memset(info.roomName, 0, ROOM_NAME_LEN);
            memset(info.mapName, 0, MAP_NAME_LEN);
            memset(info.hostName, 0, HOST_NAME_LEN);

            strncpy_s(info.roomName, ROOM_NAME_LEN, room.roomName.c_str(), _TRUNCATE);
            strncpy_s(info.mapName, MAP_NAME_LEN, room.mapName.c_str(), _TRUNCATE);
            strncpy_s(info.hostName, HOST_NAME_LEN, room.hostName.c_str(), _TRUNCATE);

            info.currentPlayerCount = room.currentPlayerCount;
            info.maxPlayerCount = room.maxPlayerCount;
            info.state = (uint8_t)room.state;

            std::cout << "[ROOM INFO] index=" << i
                << " roomId=" << info.roomId
                << " roomName=" << info.roomName
                << " mapName=" << info.mapName
                << " hostName=" << info.hostName
                << " players=" << info.currentPlayerCount << "/" << info.maxPlayerCount
                << " state=" << (int)info.state
                << std::endl;
        }

        out.header.size =
            sizeof(PacketHeader)
            + sizeof(uint16_t)
            + sizeof(RoomInfo) * out.roomCount;

        session->Send(&out);

        std::cout << "[ROOM LIST SENT] playerId=" << session->GetPlayerId()
            << " roomCount=" << out.roomCount
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_CREATE:
    {
        std::cout << "[ROOM CREATE REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " state=" << (int)session->GetState()
            << std::endl;

        if (session->GetState() == SessionState::IN_ROOM ||
            session->GetState() == SessionState::IN_GAME)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::ALREADY_IN_ROOM;

            session->Send(&out);
            break;
        }

        if (session->GetState() != SessionState::IN_LOBBY)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::NOT_IN_LOBBY;

            session->Send(&out);
            break;
        }

        PlayerData data{};
        bool ok = UserManager::Instance().GetPlayerData(session->GetUserId(), data);

        if (!ok)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::PLAYER_DATA_NOT_FOUND;

            session->Send(&out);
            break;
        }

        std::string roomName = data.nickname + WideToUTF8(L"의 방");
        std::string mapName = TEST_MAP_NAME;

        RoomMember leader{};
        leader.playerId = session->GetPlayerId();
        leader.userId = session->GetUserId();
        leader.nickname = data.nickname;
        leader.level = data.level;
        leader.playerIconId = data.playerIconId;
        leader.rankImageId = data.rankImageId;
        leader.team = RoomTeam::A;
        leader.slotIndex = 0;
        leader.isLeader = true;

        uint32_t roomId = RoomManager::Instance().CreateRoom(
            roomName,
            mapName,
            data.nickname,
            leader,
            ROOM_MAX_MEMBER_COUNT
        );

        if (roomId == 0)
        {
            SC_RoomCreateResult out{};
            out.header.size = sizeof(out);
            out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_CREATE_RESULT;
            out.result = (uint8_t)RoomCreateResult::CREATE_FAILED;

            session->Send(&out);
            break;
        }

        session->SetState(SessionState::IN_ROOM);
        session->SetCurrentRoomId(roomId);

        Room room{};
        RoomManager::Instance().GetRoom(roomId, room);

        SC_RoomCreateResult out{};
        FillRoomCreateResultPacket(
            out,
            room,
            (uint8_t)RoomCreateResult::SUCCESS,
            session->GetPlayerId()
        );

        session->Send(&out);

        std::cout << "[ROOM CREATE SUCCESS] roomId=" << roomId
            << " roomName=" << room.roomName
            << " leaderPlayerId=" << session->GetPlayerId()
            << " leaderNickname=" << data.nickname
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_ENTER:
    {
        auto* pkt = reinterpret_cast<CS_RoomEnter*>(header);

        std::cout << "[ROOM ENTER REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " roomId=" << pkt->roomId
            << " state=" << (int)session->GetState()
            << std::endl;

        SC_RoomEnterResult resultPkt{};
        resultPkt.header.size = sizeof(resultPkt);
        resultPkt.header.type = (uint16_t)PacketType::PKT_SC_ROOM_ENTER_RESULT;
        resultPkt.roomId = pkt->roomId;

        if (session->GetState() == SessionState::IN_ROOM ||
            session->GetState() == SessionState::IN_GAME)
        {
            resultPkt.result = (uint8_t)RoomEnterResult::ALREADY_IN_ROOM;
            session->Send(&resultPkt);
            break;
        }

        if (session->GetState() != SessionState::IN_LOBBY)
        {
            resultPkt.result = (uint8_t)RoomEnterResult::NOT_IN_LOBBY;
            session->Send(&resultPkt);
            break;
        }

        Room checkRoom{};
        if (!RoomManager::Instance().GetRoom(pkt->roomId, checkRoom))
        {
            resultPkt.result = (uint8_t)RoomEnterResult::ROOM_NOT_FOUND;
            session->Send(&resultPkt);
            break;
        }

        PlayerData data{};
        if (!UserManager::Instance().GetPlayerData(session->GetUserId(), data))
        {
            resultPkt.result = (uint8_t)RoomEnterResult::PLAYER_DATA_NOT_FOUND;
            session->Send(&resultPkt);
            break;
        }

        RoomMember member{};
        member.playerId = session->GetPlayerId();
        member.userId = session->GetUserId();
        member.nickname = data.nickname;
        member.level = data.level;
        member.playerIconId = data.playerIconId;
        member.rankImageId = data.rankImageId;
        member.isLeader = false;

        Room updatedRoom{};
        bool ok = RoomManager::Instance().EnterRoom(pkt->roomId, member, updatedRoom);

        if (!ok)
        {
            resultPkt.result = (uint8_t)RoomEnterResult::ROOM_FULL;
            session->Send(&resultPkt);
            break;
        }

        session->SetState(SessionState::IN_ROOM);
        session->SetCurrentRoomId(pkt->roomId);

        resultPkt.result = (uint8_t)RoomEnterResult::SUCCESS;
        session->Send(&resultPkt);

        BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM ENTER SUCCESS] roomId=" << pkt->roomId
            << " playerId=" << session->GetPlayerId()
            << " nickname=" << data.nickname
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_LEAVE:
    {
        std::cout << "[ROOM LEAVE REQUEST] playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " roomId=" << session->GetCurrentRoomId()
            << " state=" << (int)session->GetState()
            << std::endl;

        SC_RoomLeaveResult out{};
        out.header.size = sizeof(out);
        out.header.type = (uint16_t)PacketType::PKT_SC_ROOM_LEAVE_RESULT;
        out.roomId = session->GetCurrentRoomId();
        out.roomDeleted = 0;

        if (session->GetState() != SessionState::IN_ROOM)
        {
            out.result = (uint8_t)RoomLeaveResult::NOT_IN_ROOM;
            session->Send(&out);
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        bool roomDeleted = false;
        Room updatedRoom{};

        bool ok = RoomManager::Instance().LeaveRoom(
            roomId,
            session->GetPlayerId(),
            roomDeleted,
            updatedRoom
        );

        if (!ok)
        {
            out.result = (uint8_t)RoomLeaveResult::ROOM_NOT_FOUND;
            session->Send(&out);
            break;
        }

        session->SetState(SessionState::IN_LOBBY);
        session->SetCurrentRoomId(0);

        out.result = (uint8_t)RoomLeaveResult::SUCCESS;
        out.roomDeleted = roomDeleted ? 1 : 0;

        session->Send(&out);

        if (!roomDeleted)
        {
            BroadcastRoomState(updatedRoom);
        }

        std::cout << "[ROOM LEAVE SUCCESS] playerId=" << session->GetPlayerId()
            << " roomId=" << roomId
            << " roomDeleted=" << roomDeleted
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_MAP_SET:
    {
        if (header->size != sizeof(CS_RoomMapSet))
        {
            SendRoomMapSetResult(session, RoomMapSetResult::FAILED, 0);
            break;
        }

        auto* pkt = reinterpret_cast<CS_RoomMapSet*>(header);
        const char* mapName = GetMapName(pkt->mapId);
        if (mapName == nullptr)
        {
            SendRoomMapSetResult(session, RoomMapSetResult::INVALID_MAP, pkt->mapId);
            break;
        }

        if (session->GetState() != SessionState::IN_ROOM ||
            session->GetCurrentRoomId() == 0)
        {
            SendRoomMapSetResult(session, RoomMapSetResult::NOT_IN_ROOM, pkt->mapId);
            break;
        }

        const uint32_t roomId = session->GetCurrentRoomId();
        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomMapSetResult(session, RoomMapSetResult::ROOM_NOT_FOUND, pkt->mapId);
            break;
        }

        if (room.isMatchmade)
        {
            SendRoomMapSetResult(session, RoomMapSetResult::PRIVATE_ROOM_ONLY, pkt->mapId);
            break;
        }

        if (room.state != RoomState::WAITING)
        {
            SendRoomMapSetResult(session, RoomMapSetResult::ROOM_PLAYING, pkt->mapId);
            break;
        }

        if (room.leaderPlayerId != session->GetPlayerId())
        {
            SendRoomMapSetResult(session, RoomMapSetResult::NOT_LEADER, pkt->mapId);
            break;
        }

        Room updatedRoom{};
        if (!RoomManager::Instance().SetRoomMap(
                roomId,
                session->GetPlayerId(),
                mapName,
                updatedRoom))
        {
            SendRoomMapSetResult(session, RoomMapSetResult::FAILED, pkt->mapId);
            break;
        }

        SendRoomMapSetResult(session, RoomMapSetResult::SUCCESS, pkt->mapId);
        BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM MAP SET] roomId=" << roomId
            << " leaderPlayerId=" << session->GetPlayerId()
            << " map=" << updatedRoom.mapName
            << std::endl;
        break;
    }

    case PacketType::PKT_CS_ROOM_GAME_START:
    {
        std::cout << "[ROOM GAME START REQUEST] playerId="
            << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " roomId=" << session->GetCurrentRoomId()
            << " state=" << static_cast<int>(session->GetState())
            << std::endl;

        if (session->GetState() != SessionState::IN_ROOM ||
            session->GetCurrentRoomId() == 0)
        {
            SendRoomGameStartResult(session, RoomGameStartResult::NOT_IN_ROOM);
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomGameStartResult(session, RoomGameStartResult::ROOM_NOT_FOUND);
            break;
        }

        if (room.state == RoomState::PLAYING)
        {
            SendRoomGameStartResult(session, RoomGameStartResult::ALREADY_PLAYING);
            break;
        }

        if (room.leaderPlayerId != session->GetPlayerId())
        {
            SendRoomGameStartResult(session, RoomGameStartResult::NOT_LEADER);
            break;
        }

        if (!AreAllPlayersReturnedFromResult(roomId, room))
        {
            std::cout << "[ROOM GAME START BLOCK] not all players returned from result. "
                << "roomId=" << roomId
                << " leaderPlayerId=" << session->GetPlayerId()
                << std::endl;

            SendRoomGameStartResult(
                session,
                RoomGameStartResult::NOT_ALL_RETURNED_FROM_RESULT
            );

            break;
        }

        if (!RoomManager::Instance().CanStartGame(roomId))
        {
            SendRoomGameStartResult(session, RoomGameStartResult::NOT_ENOUGH_PLAYERS);
            break;
        }

        Room updatedRoom{};
        bool ok = RoomManager::Instance().StartGame(
            roomId,
            session->GetPlayerId(),
            updatedRoom
        );

        if (!ok)
        {
            SendRoomGameStartResult(session, RoomGameStartResult::FAILED);
            break;
        }

        ResetGameLoadRuntimeState(roomId);
        {
            std::lock_guard<std::mutex> lock(g_resultLeaveMutex);
            g_resultLeavePlayers.erase(roomId);
        }

        BroadcastRoomGameStartResult(updatedRoom, RoomGameStartResult::SUCCESS);

        std::cout << "[ROOM GAME START SUCCESS] roomId="
            << roomId
            << " leaderPlayerId=" << session->GetPlayerId()
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_SLOT_CHANGE:
    {
        auto* pkt = reinterpret_cast<CS_RoomSlotChange*>(header);

        RoomTeam targetTeam = static_cast<RoomTeam>(pkt->targetTeam);
        uint8_t targetSlotIndex = pkt->targetSlotIndex;

        std::cout << "[ROOM SLOT CHANGE REQUEST] playerId=" << session->GetPlayerId()
            << " roomId=" << session->GetCurrentRoomId()
            << " targetTeam=" << static_cast<int>(pkt->targetTeam)
            << " targetSlot=" << static_cast<int>(targetSlotIndex)
            << std::endl;

        if (session->GetState() != SessionState::IN_ROOM || session->GetCurrentRoomId() == 0)
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::NOT_IN_ROOM,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::ROOM_NOT_FOUND,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        RoomMember requesterMember{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), requesterMember))
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::NOT_IN_ROOM,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        if (requesterMember.team == targetTeam && requesterMember.slotIndex == targetSlotIndex)
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::SAME_SLOT,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        RoomMember targetMember{};
        bool occupied = RoomManager::Instance().GetMemberBySlot(
            roomId,
            targetTeam,
            targetSlotIndex,
            targetMember
        );

        if (!occupied)
        {
            Room updatedRoom{};
            bool moved = RoomManager::Instance().MoveMemberToSlot(
                roomId,
                session->GetPlayerId(),
                targetTeam,
                targetSlotIndex,
                updatedRoom
            );

            if (!moved)
            {
                SendRoomSlotChangeResult(
                    session,
                    RoomSlotChangeResult::INVALID_SLOT,
                    targetTeam,
                    targetSlotIndex
                );
                break;
            }

            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::SUCCESS,
                targetTeam,
                targetSlotIndex
            );

            BroadcastRoomState(updatedRoom);

            std::cout << "[ROOM SLOT MOVE SUCCESS] playerId=" << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;

            break;
        }

        if (targetMember.playerId == session->GetPlayerId())
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::SAME_SLOT,
                targetTeam,
                targetSlotIndex
            );
            break;
        }

        auto targetSession = SessionManager::Instance().Find(targetMember.playerId);
        if (targetSession == nullptr)
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::TARGET_NOT_FOUND,
                targetTeam,
                targetSlotIndex,
                targetMember.nickname
            );
            break;
        }

        PendingSlotSwap pending{};
        pending.roomId = roomId;
        pending.requesterPlayerId = session->GetPlayerId();
        pending.targetPlayerId = targetMember.playerId;
        pending.requesterTeam = requesterMember.team;
        pending.requesterSlotIndex = requesterMember.slotIndex;
        pending.targetTeam = targetMember.team;
        pending.targetSlotIndex = targetMember.slotIndex;

        if (!AddPendingSlotSwap(pending))
        {
            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::REQUEST_ALREADY_EXISTS,
                targetTeam,
                targetSlotIndex,
                targetMember.nickname
            );
            break;
        }

        SC_RoomSlotSwapRequest requestOut{};
        requestOut.header.size = sizeof(requestOut);
        requestOut.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ROOM_SLOT_SWAP_REQUEST);

        requestOut.requesterPlayerId = session->GetPlayerId();

        memset(requestOut.requesterNickname, 0, sizeof(requestOut.requesterNickname));
        strncpy_s(
            requestOut.requesterNickname,
            sizeof(requestOut.requesterNickname),
            requesterMember.nickname.c_str(),
            _TRUNCATE
        );

        requestOut.requesterTeam = static_cast<uint8_t>(requesterMember.team);
        requestOut.requesterSlotIndex = requesterMember.slotIndex;

        requestOut.targetTeam = static_cast<uint8_t>(targetMember.team);
        requestOut.targetSlotIndex = targetMember.slotIndex;

        targetSession->Send(&requestOut);

        SendRoomSlotChangeResult(
            session,
            RoomSlotChangeResult::REQUEST_SENT,
            targetTeam,
            targetSlotIndex,
            targetMember.nickname
        );

        std::cout << "[ROOM SLOT SWAP REQUEST SENT] requester=" << session->GetPlayerId()
            << " target=" << targetMember.playerId
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_SLOT_SWAP_RESPONSE:
    {
        auto* pkt = reinterpret_cast<CS_RoomSlotSwapResponse*>(header);

        uint32_t targetPlayerId = session->GetPlayerId();
        uint32_t requesterPlayerId = pkt->requesterPlayerId;
        bool accept = pkt->accept != 0;

        std::cout << "[ROOM SLOT SWAP RESPONSE] target=" << targetPlayerId
            << " requester=" << requesterPlayerId
            << " accept=" << accept
            << std::endl;

        PendingSlotSwap pending{};
        bool found = TakePendingSlotSwap(
            targetPlayerId,
            requesterPlayerId,
            pending
        );

        if (!found)
        {
            std::cout << "[ROOM SLOT SWAP RESPONSE FAIL] pending not found"
                << std::endl;
            break;
        }

        auto requesterSession = SessionManager::Instance().Find(requesterPlayerId);

        RoomMember targetMember{};
        RoomManager::Instance().GetMember(
            pending.roomId,
            targetPlayerId,
            targetMember
        );

        if (!accept)
        {
            if (requesterSession != nullptr)
            {
                SendRoomSlotChangeResult(
                    requesterSession.get(),
                    RoomSlotChangeResult::REJECTED,
                    pending.targetTeam,
                    pending.targetSlotIndex,
                    targetMember.nickname
                );
            }

            std::cout << "[ROOM SLOT SWAP REJECTED] requester=" << requesterPlayerId
                << " target=" << targetPlayerId
                << std::endl;

            break;
        }

        Room updatedRoom{};
        bool swapped = RoomManager::Instance().SwapMembers(
            pending.roomId,
            requesterPlayerId,
            targetPlayerId,
            updatedRoom
        );

        if (!swapped)
        {
            if (requesterSession != nullptr)
            {
                SendRoomSlotChangeResult(
                    requesterSession.get(),
                    RoomSlotChangeResult::FAILED,
                    pending.targetTeam,
                    pending.targetSlotIndex,
                    targetMember.nickname
                );
            }

            SendRoomSlotChangeResult(
                session,
                RoomSlotChangeResult::FAILED,
                pending.requesterTeam,
                pending.requesterSlotIndex
            );

            std::cout << "[ROOM SLOT SWAP FAIL] roomId=" << pending.roomId
                << std::endl;

            break;
        }

        if (requesterSession != nullptr)
        {
            SendRoomSlotChangeResult(
                requesterSession.get(),
                RoomSlotChangeResult::SUCCESS,
                pending.targetTeam,
                pending.targetSlotIndex,
                targetMember.nickname
            );
        }

        SendRoomSlotChangeResult(
            session,
            RoomSlotChangeResult::SUCCESS,
            pending.requesterTeam,
            pending.requesterSlotIndex
        );

        BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM SLOT SWAP SUCCESS] roomId=" << pending.roomId
            << " requester=" << requesterPlayerId
            << " target=" << targetPlayerId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_TRANSFER_LEADER:
    {
        auto* pkt = reinterpret_cast<CS_RoomTransferLeader*>(header);

        uint32_t roomId = session->GetCurrentRoomId();
        uint32_t targetPlayerId = pkt->targetPlayerId;

        std::cout << "[ROOM TRANSFER LEADER REQUEST] leader="
            << session->GetPlayerId()
            << " target=" << targetPlayerId
            << " roomId=" << roomId
            << std::endl;

        if (session->GetState() != SessionState::IN_ROOM || roomId == 0)
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::NOT_IN_ROOM,
                targetPlayerId
            );
            break;
        }

        if (session->GetPlayerId() == targetPlayerId)
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::CANNOT_TARGET_SELF,
                targetPlayerId
            );
            break;
        }

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::ROOM_NOT_FOUND,
                targetPlayerId
            );
            break;
        }

        if (room.leaderPlayerId != session->GetPlayerId())
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::NOT_LEADER,
                targetPlayerId
            );
            break;
        }

        RoomMember targetMember{};
        if (!RoomManager::Instance().GetMember(roomId, targetPlayerId, targetMember))
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::TARGET_NOT_FOUND,
                targetPlayerId
            );
            break;
        }

        Room updatedRoom{};
        bool ok = RoomManager::Instance().TransferLeader(
            roomId,
            session->GetPlayerId(),
            targetPlayerId,
            updatedRoom
        );

        if (!ok)
        {
            SendRoomTransferLeaderResult(
                session,
                RoomAdminResult::FAILED,
                targetPlayerId,
                targetMember.nickname
            );
            break;
        }

        SendRoomTransferLeaderResult(
            session,
            RoomAdminResult::SUCCESS,
            targetPlayerId,
            targetMember.nickname
        );

        BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM TRANSFER LEADER SUCCESS] oldLeader="
            << session->GetPlayerId()
            << " newLeader=" << targetPlayerId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_KICK:
    {
        auto* pkt = reinterpret_cast<CS_RoomKick*>(header);

        uint32_t roomId = session->GetCurrentRoomId();
        uint32_t targetPlayerId = pkt->targetPlayerId;

        std::cout << "[ROOM KICK REQUEST] leader="
            << session->GetPlayerId()
            << " target=" << targetPlayerId
            << " roomId=" << roomId
            << std::endl;

        if (session->GetState() != SessionState::IN_ROOM || roomId == 0)
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::NOT_IN_ROOM,
                targetPlayerId
            );
            break;
        }

        if (session->GetPlayerId() == targetPlayerId)
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::CANNOT_TARGET_SELF,
                targetPlayerId
            );
            break;
        }

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::ROOM_NOT_FOUND,
                targetPlayerId
            );
            break;
        }

        if (room.leaderPlayerId != session->GetPlayerId())
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::NOT_LEADER,
                targetPlayerId
            );
            break;
        }

        RoomMember targetMember{};
        if (!RoomManager::Instance().GetMember(roomId, targetPlayerId, targetMember))
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::TARGET_NOT_FOUND,
                targetPlayerId
            );
            break;
        }

        PlayerData leaderData{};
        std::string leaderNickname = "방장";

        if (UserManager::Instance().GetPlayerData(session->GetUserId(), leaderData))
            leaderNickname = leaderData.nickname;

        bool roomDeleted = false;
        Room updatedRoom{};

        bool ok = RoomManager::Instance().KickMember(
            roomId,
            session->GetPlayerId(),
            targetPlayerId,
            roomDeleted,
            updatedRoom
        );

        if (!ok)
        {
            SendRoomKickResult(
                session,
                RoomAdminResult::FAILED,
                targetPlayerId,
                targetMember.nickname
            );
            break;
        }

        auto targetSession = SessionManager::Instance().Find(targetPlayerId);
        if (targetSession != nullptr)
        {
            targetSession->SetState(SessionState::IN_LOBBY);
            targetSession->SetCurrentRoomId(0);

            SendRoomKicked(
                targetSession.get(),
                roomId,
                leaderNickname
            );
        }

        SendRoomKickResult(
            session,
            RoomAdminResult::SUCCESS,
            targetPlayerId,
            targetMember.nickname
        );

        if (!roomDeleted)
            BroadcastRoomState(updatedRoom);

        std::cout << "[ROOM KICK SUCCESS] leader="
            << session->GetPlayerId()
            << " target=" << targetPlayerId
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_PICK_ENTER:
    {
        std::cout << "[PICK ENTER REQUEST] playerId="
            << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " roomId=" << session->GetCurrentRoomId()
            << " state=" << static_cast<int>(session->GetState())
            << std::endl;

        if (session->GetCurrentRoomId() == 0)
        {
            std::cout << "[PICK ENTER BLOCK] currentRoomId is 0" << std::endl;
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        Room readyRoom{};
        bool readyOk = RoomManager::Instance().SetPickReady(
            roomId,
            session->GetPlayerId(),
            readyRoom
        );

        if (!readyOk)
        {
            std::cout << "[PICK ENTER READY FAIL] playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        SendPickState(session);

        Room timerRoom{};
        bool timerStartOk = RoomManager::Instance().TryStartPickTimer(
            roomId,
            timerRoom
        );

        if (timerStartOk)
        {
            std::cout << "[PICK TIMER START] roomId="
                << roomId
                << std::endl;

            BroadcastPickStateToRoom(timerRoom);
            StartPickTimerThread(roomId);
        }

        break;
    }

    case PacketType::PKT_CS_PICK_HOVER:
    {
        auto* pkt = reinterpret_cast<CS_PickHover*>(header);

        std::cout << "[PICK HOVER REQUEST] playerId="
            << session->GetPlayerId()
            << " roomId=" << session->GetCurrentRoomId()
            << " characterId=" << pkt->characterId
            << std::endl;

        if (session->GetCurrentRoomId() == 0)
        {
            std::cout << "[PICK HOVER BLOCK] currentRoomId is 0" << std::endl;
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        RoomMember requester{};
        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            requester))
        {
            std::cout << "[PICK HOVER BLOCK] requester not found" << std::endl;
            break;
        }

        Room updatedRoom{};
        bool ok = RoomManager::Instance().SetPickHover(
            roomId,
            session->GetPlayerId(),
            pkt->characterId,
            updatedRoom
        );

        if (!ok)
        {
            std::cout << "[PICK HOVER FAIL] playerId="
                << session->GetPlayerId()
                << " characterId=" << pkt->characterId
                << std::endl;

            SendPickState(session);
            break;
        }

        BroadcastPickStateToTeam(updatedRoom, requester.team);

        std::cout << "[PICK HOVER SUCCESS] playerId="
            << session->GetPlayerId()
            << " characterId=" << pkt->characterId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_PICK_LOCK:
    {
        auto* pkt = reinterpret_cast<CS_PickLock*>(header);

        std::cout << "[PICK LOCK REQUEST] playerId="
            << session->GetPlayerId()
            << " roomId=" << session->GetCurrentRoomId()
            << " characterId=" << pkt->characterId
            << std::endl;

        if (session->GetCurrentRoomId() == 0)
        {
            SendPickLockResult(
                session,
                PickLockResult::NOT_IN_GAME,
                pkt->characterId
            );
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        RoomMember requester{};
        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            requester))
        {
            SendPickLockResult(
                session,
                PickLockResult::NOT_IN_GAME,
                pkt->characterId
            );
            break;
        }

        Room updatedRoom{};
        uint8_t result = static_cast<uint8_t>(PickLockResult::FAILED);

        bool ok = RoomManager::Instance().LockPick(
            roomId,
            session->GetPlayerId(),
            pkt->characterId,
            updatedRoom,
            result
        );

        SendPickLockResult(
            session,
            static_cast<PickLockResult>(result),
            pkt->characterId
        );

        if (!ok)
        {
            std::cout << "[PICK LOCK FAIL] playerId="
                << session->GetPlayerId()
                << " characterId=" << pkt->characterId
                << " result=" << static_cast<int>(result)
                << std::endl;

            SendPickState(session);
            break;
        }

        BroadcastPickStateToTeam(updatedRoom, requester.team);

        std::cout << "[PICK LOCK SUCCESS] playerId="
            << session->GetPlayerId()
            << " characterId=" << pkt->characterId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_GAME_LOAD_READY:
    {
        std::cout << "[GAME LOAD READY REQUEST] playerId="
            << session->GetPlayerId()
            << " roomId=" << session->GetCurrentRoomId()
            << " state=" << static_cast<int>(session->GetState())
            << std::endl;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[GAME LOAD READY BLOCK] roomId is 0" << std::endl;
            break;
        }

        Room readyRoom{};
        bool allReady = MarkGameLoadReadyAndCheckAll(
            roomId,
            session->GetPlayerId(),
            readyRoom
        );

        if (!allReady)
        {
            std::cout << "[GAME LOAD READY WAIT] playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        std::cout << "[GAME ALL CLIENT READY] roomId="
            << roomId
            << std::endl;

        BroadcastGameAllReady(readyRoom);
        StartGamePrepareTimerThread(roomId);

        break;
    }

    case PacketType::PKT_CS_MOVE_STATE:
    {
        auto* pkt = reinterpret_cast<CS_MoveState*>(header);

        if (header->size != sizeof(CS_MoveState))
        {
            std::cout << "[MOVE BLOCK] invalid packet size playerId="
                << session->GetPlayerId()
                << " receivedSize=" << header->size
                << " expectedSize=" << sizeof(CS_MoveState)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME &&
            session->GetState() != SessionState::IN_ROOM)
        {
            std::cout << "[MOVE BLOCK] invalid state playerId="
                << session->GetPlayerId()
                << " state=" << static_cast<int>(session->GetState())
                << std::endl;

            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[MOVE BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneActiveForPlayer(session->GetPlayerId()))
        {
            std::cout << "[MOVE BLOCK] player is controlling drone. playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        Vector3 oldPos = session->GetPosition();

        float dx = pkt->x - oldPos.x;
        float dy = pkt->y - oldPos.y;
        float dz = pkt->z - oldPos.z;

        float distSq =
            dx * dx +
            dy * dy +
            dz * dz;

        bool moved =
            distSq > SCANNER_MOVE_EPSILON_SQ;

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout
                << "[MOVE BLOCK] stunned playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        session->SetPosition(
            pkt->x,
            pkt->y,
            pkt->z
        );

        session->SetYaw(pkt->yaw);
        session->SetPitch(pkt->pitch);

        if (moved)
        {
            TryTriggerScannersByMovement(session);
        }

        uint8_t animIndex = pkt->animIndex;

        // 클라이언트 애니메이션 인덱스
        //
        // 0 ~ 5   : 권총
        // 6 ~ 11  : 긴 총
        // 12 ~ 17 : 칼
        constexpr uint8_t MAX_ANIM_INDEX = 17;

        if (animIndex > MAX_ANIM_INDEX)
        {
            std::cout << "[MOVE ANIM INVALID] playerId="
                << session->GetPlayerId()
                << " animIndex="
                << static_cast<int>(animIndex)
                << std::endl;

            animIndex = 0;
        }

        session->SetAnimIndex(animIndex);

        SC_MoveState out{};

        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_MOVE_STATE
            );

        out.playerId = session->GetPlayerId();

        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;

        out.yaw = pkt->yaw;
        out.pitch = pkt->pitch;

        out.animIndex = animIndex;
        out.isCrouching = pkt->isCrouching != 0 ? 1 : 0;
        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        break;
    }

    case PacketType::PKT_CS_HIT:
    {
        auto* pkt = reinterpret_cast<CS_Hit*>(header);

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[HIT BLOCK] attacker not in game. playerId="
                << session->GetPlayerId()
                << " state=" << static_cast<int>(session->GetState())
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "HIT"))
            break;

        if (IsEntryBladeActive(session->GetPlayerId()))
        {
            std::cout
                << "[HIT BLOCK] entry blade active attacker="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[HIT BLOCK] roomId is 0. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        auto target = SessionManager::Instance().Find(pkt->targetId);
        if (target == nullptr)
            break;

        if (target->GetCurrentRoomId() != roomId)
        {
            std::cout << "[HIT BLOCK] different room. attacker="
                << session->GetPlayerId()
                << " target=" << target->GetPlayerId()
                << std::endl;
            break;
        }

        RoomMember attackerMember{};
        RoomMember targetMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            attackerMember))
        {
            std::cout << "[HIT BLOCK] attacker member not found. attacker="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        if (!RoomManager::Instance().GetMember(
            roomId,
            target->GetPlayerId(),
            targetMember))
        {
            std::cout << "[HIT BLOCK] target member not found. target="
                << target->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        if (attackerMember.team == targetMember.team)
        {
            std::cout << "[HIT BLOCK] friendly fire blocked. attacker="
                << session->GetPlayerId()
                << " target=" << target->GetPlayerId()
                << " team=" << static_cast<int>(attackerMember.team)
                << std::endl;
            break;
        }

        if (target->IsDead())
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
        {
            std::cout << "[HIT BLOCK] weapon not owned. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        PlayerWeaponState* weaponState = session->GetWeaponState(weaponId);

        if (weaponState == nullptr)
            break;

        const ServerWeaponData* weaponData = ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
        {
            std::cout << "[HIT BLOCK] weapon data not found. weaponId="
                << weaponId
                << std::endl;
            break;
        }

        if (weaponData->weaponType != ServerWeaponType::Knife)
        {
            if (weaponState->magAmmo < 0)
            {
                SendAmmoUpdate(session, *weaponState);
                break;
            }
        }
        target->SetLastDamager(
            session->GetPlayerId(),
            pkt->hitZone == 1,
            weaponId);

        target->AddAssistDamageRecord(
            session->GetPlayerId(),
            weaponData->damage
        );

        int appliedDamage = target->Damage(weaponData->damage);
        session->GetMatchStats().damageDealt += appliedDamage;

        SC_HPUpdate hpPkt{};
        FillHPUpdatePacket(hpPkt, target.get());

        BroadcastPacketToRoomInternal(roomId, &hpPkt);

        std::cout << "[HIT APPLY] attacker="
            << session->GetPlayerId()
            << " target=" << target->GetPlayerId()
            << " weaponId=" << weaponId
            << " damage=" << weaponData->damage
            << " ammo=" << weaponState->magAmmo
            << "/" << weaponState->reserveAmmo
            << " targetHp=" << target->GetHP()
            << " targetBarrier=" << target->GetBarrier()
            << std::endl;

        if (target->IsDead())
        {
            HandleDeath(target.get());
        }

        break;
    }

    case PacketType::PKT_CS_WEAPON_SWITCH:
    {
        auto* pkt = reinterpret_cast<CS_WeaponSwitch*>(header);

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[WEAPON SWITCH BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "WEAPON_SWITCH"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[WEAPON SWITCH BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (!session->HasWeapon(pkt->weaponId))
        {
            std::cout << "[WEAPON SWITCH BLOCK] not owned playerId="
                << session->GetPlayerId()
                << " weaponId=" << pkt->weaponId
                << std::endl;
            break;
        }

        int previousWeaponId = session->GetCurrentWeaponId();

        // 장전 중 다른 무기로 바꾸면 이전 무기의 장전을 서버에서도 취소한다.
        if (previousWeaponId != pkt->weaponId)
        {
            PlayerWeaponState* previousState =
                session->GetWeaponState(previousWeaponId);

            if (previousState != nullptr && previousState->isReloading)
            {
                previousState->isReloading = false;

                BroadcastReloadVisualInternal(
                    roomId,
                    session->GetPlayerId(),
                    previousWeaponId,
                    false
                );

                std::cout << "[RELOAD CANCEL BY SWITCH] playerId="
                    << session->GetPlayerId()
                    << " weaponId=" << previousWeaponId
                    << " nextWeaponId=" << pkt->weaponId
                    << std::endl;
            }
        }

        session->SetCurrentWeaponId(pkt->weaponId);

        PlayerWeaponState* weaponState = session->GetWeaponState(pkt->weaponId);
        if (weaponState != nullptr)
            SendAmmoUpdate(session, *weaponState);

        BroadcastWeaponSwitchNotify(
            roomId,
            session->GetPlayerId(),
            pkt->weaponId
        );

        std::cout << "[WEAPON SWITCH BROADCAST] playerId="
            << session->GetPlayerId()
            << " weaponId=" << pkt->weaponId
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ADS_STATE:
    {
        auto* pkt = reinterpret_cast<CS_ADSState*>(header);

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[ADS BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "ADS"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[ADS BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        SC_ADSState out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_ADS_STATE);

        out.playerId = session->GetPlayerId();
        out.isADS = pkt->isADS != 0 ? 1 : 0;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[ADS STATE BROADCAST] playerId="
            << session->GetPlayerId()
            << " isADS=" << static_cast<int>(out.isADS)
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_RELOAD:
    {
        if (header->size != sizeof(CS_Reload))
        {
            std::cout << "[RELOAD BLOCK] invalid packet size. playerId="
                << session->GetPlayerId()
                << " receivedSize=" << header->size
                << " expectedSize=" << sizeof(CS_Reload)
                << std::endl;
            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[RELOAD BLOCK] not in game. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "RELOAD"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();
        if (roomId == 0)
            break;

        int weaponId = session->GetCurrentWeaponId();

        PlayerWeaponState* weaponState =
            session->GetWeaponState(weaponId);

        if (weaponState == nullptr)
        {
            std::cout << "[RELOAD BLOCK] weapon state null. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        // 장전 시작 상태가 없거나 무기 변경으로 취소된 장전은 적용하지 않는다.
        if (!weaponState->isReloading)
        {
            std::cout << "[RELOAD BLOCK] reload is not active. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;

            SendAmmoUpdate(session, *weaponState);
            break;
        }

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
        {
            weaponState->isReloading = false;
            break;
        }

        // 이 시점부터 완료 또는 실패 모두 장전 상태를 종료한다.
        weaponState->isReloading = false;

        if (IsBuyPhase(roomId) &&
            weaponData->weaponType != ServerWeaponType::Knife)
        {
            weaponState->magAmmo = weaponData->magSize;
            weaponState->reserveAmmo = weaponData->maxReserveAmmo;

            SendAmmoUpdate(session, *weaponState);

            BroadcastReloadVisualInternal(
                roomId,
                session->GetPlayerId(),
                weaponId,
                false
            );

            std::cout << "[BUY PHASE RELOAD REFILL] playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << " ammo=" << weaponState->magAmmo
                << "/" << weaponState->reserveAmmo
                << std::endl;

            break;
        }

        if (weaponData->weaponType == ServerWeaponType::Knife ||
            weaponState->magAmmo >= weaponData->magSize ||
            weaponState->reserveAmmo <= 0)
        {
            BroadcastReloadVisualInternal(
                roomId,
                session->GetPlayerId(),
                weaponId,
                false
            );

            SendAmmoUpdate(session, *weaponState);
            break;
        }

        int needAmmo = weaponData->magSize - weaponState->magAmmo;
        int reloadAmount = std::min(needAmmo, weaponState->reserveAmmo);

        weaponState->magAmmo += reloadAmount;
        weaponState->reserveAmmo -= reloadAmount;

        SendAmmoUpdate(session, *weaponState);

        BroadcastReloadVisualInternal(
            roomId,
            session->GetPlayerId(),
            weaponId,
            false
        );

        std::cout << "[RELOAD APPLY] playerId="
            << session->GetPlayerId()
            << " weaponId=" << weaponId
            << " ammo=" << weaponState->magAmmo
            << "/" << weaponState->reserveAmmo
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_FIRE:
    {
        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[FIRE BLOCK] not in game. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (session->IsDead())
        {
            std::cout << "[FIRE BLOCK] player dead. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout << "[FIRE BLOCK] stunned playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsEntryBladeActive(session->GetPlayerId()))
        {
            std::cout
                << "[FIRE BLOCK] entry blade active playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneBlockedAction(session, "FIRE"))
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
        {
            std::cout << "[FIRE BLOCK] weapon not owned. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        PlayerWeaponState* weaponState = session->GetWeaponState(weaponId);

        if (weaponState == nullptr)
        {
            std::cout << "[FIRE BLOCK] weapon state null. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        if (weaponState->isReloading)
        {
            std::cout << "[FIRE BLOCK] weapon is reloading. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        const ServerWeaponData* weaponData = ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
        {
            std::cout << "[FIRE BLOCK] weapon data null. weaponId="
                << weaponId
                << std::endl;
            break;
        }

        if (weaponData->weaponType == ServerWeaponType::Knife)
        {
            SendAmmoUpdate(session, *weaponState);
            break;
        }

        if (weaponState->magAmmo <= 0)
        {
            std::cout << "[FIRE BLOCK] no ammo. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;

            SendAmmoUpdate(session, *weaponState);
            break;
        }

        // 여기서부터 실제 총 발사가 성립됨.
        // 야수화 중이면 공격으로 판정하고 해제한다.
        CancelBeastStealth(
            session->GetPlayerId(),
            "fire"
        );

        weaponState->magAmmo -= 1;

        SendAmmoUpdate(session, *weaponState);

        uint32_t roomId =
            session->GetCurrentRoomId();

        SC_FireNotify fireOut{};

        fireOut.header.size =
            sizeof(fireOut);

        fireOut.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_FIRE_NOTIFY
                );

        fireOut.playerId =
            session->GetPlayerId();

        fireOut.weaponId =
            weaponId;

        BroadcastPacketToRoomInternal(
            roomId,
            &fireOut
        );

        std::cout << "[FIRE APPLY] playerId="
            << session->GetPlayerId()
            << " weaponId=" << weaponId
            << " ammo=" << weaponState->magAmmo
            << "/" << weaponState->reserveAmmo
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_KNIFE_ATTACK:
    {
        if (header->size != sizeof(CS_KnifeAttack))
        {
            std::cout << "[KNIFE ATTACK BLOCK] invalid packet size playerId="
                << session->GetPlayerId()
                << " receivedSize=" << header->size
                << " expectedSize=" << sizeof(CS_KnifeAttack)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[KNIFE ATTACK BLOCK] not in game playerId="
                << session->GetPlayerId()
                << " state="
                << static_cast<int>(session->GetState())
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout << "[KNIFE ATTACK BLOCK] player dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout << "[KNIFE ATTACK BLOCK] stunned playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsEntryBladeActive(session->GetPlayerId()))
        {
            std::cout
                << "[KNIFE ATTACK BLOCK] entry blade active playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneBlockedAction(session, "KNIFE_ATTACK"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[KNIFE ATTACK BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        int currentWeaponId =
            session->GetCurrentWeaponId();

        if (!session->HasWeapon(currentWeaponId))
        {
            std::cout << "[KNIFE ATTACK BLOCK] weapon not owned playerId="
                << session->GetPlayerId()
                << " weaponId=" << currentWeaponId
                << std::endl;

            break;
        }

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(currentWeaponId);

        if (weaponData == nullptr)
        {
            std::cout << "[KNIFE ATTACK BLOCK] weapon data not found playerId="
                << session->GetPlayerId()
                << " weaponId=" << currentWeaponId
                << std::endl;

            break;
        }

        if (weaponData->weaponType != ServerWeaponType::Knife)
        {
            std::cout << "[KNIFE ATTACK BLOCK] current weapon is not knife playerId="
                << session->GetPlayerId()
                << " weaponId=" << currentWeaponId
                << std::endl;

            break;
        }

        // 여기서부터 실제 칼 공격이 성립됨.
        // 야수화 중이면 공격으로 판정하고 해제한다.
        CancelBeastStealth(
            session->GetPlayerId(),
            "knife_attack"
        );

        SC_KnifeAttackNotify out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_KNIFE_ATTACK_NOTIFY
                );

        out.playerId =
            session->GetPlayerId();

        BroadcastPacketToRoomInternal(
            roomId,
            &out
        );

        std::cout << "[KNIFE ATTACK BROADCAST] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << " weaponId=" << currentWeaponId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_RESULT_LEAVE:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[RESULT LEAVE BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        Room currentRoom{};
        if (!RoomManager::Instance().GetRoom(roomId, currentRoom))
            break;

        if (currentRoom.isMatchmade)
        {
            // A forged result-leave must not turn an in-progress player into a
            // lobby-ready party member and allow that party to queue again.
            if (!currentRoom.matchResultReady) { SocialResult(session, 4); break; }
            bool roomDeleted = false;
            Room remainingRoom{};
            RoomManager::Instance().LeaveRoom(
                roomId,
                session->GetPlayerId(),
                roomDeleted,
                remainingRoom);

            session->SetState(SessionState::IN_LOBBY);
            session->SetCurrentRoomId(0);

            SC_ResultLeaveDone out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(
                PacketType::PKT_SC_RESULT_LEAVE_DONE);
            out.roomId = 0;
            session->Send(&out);

            BroadcastParty(session->GetPlayerId());

            std::cout << "[MATCHMAKING RESULT LEAVE] playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << " roomDeleted=" << roomDeleted
                << std::endl;
            break;
        }

        Room updatedRoom{};

        bool ok = RoomManager::Instance().FinishGameAndReturnRoom(
            roomId,
            updatedRoom
        );

        if (!ok)
        {
            std::cout << "[RESULT LEAVE BLOCK] room not found roomId="
                << roomId
                << std::endl;
            break;
        }

        session->SetState(SessionState::IN_ROOM);

        {
            std::lock_guard<std::mutex> lock(g_resultLeaveMutex);
            g_resultLeavePlayers[roomId].insert(session->GetPlayerId());
        }

        SC_ResultLeaveDone out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_RESULT_LEAVE_DONE);
        out.roomId = roomId;

        session->Send(&out);

        BroadcastRoomState(updatedRoom);

        std::cout << "[RESULT LEAVE] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << " returnRoomState=WAITING"
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ROOM_STATE_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_RoomStateRequest*>(header);

        uint32_t roomId = pkt->roomId;

        Room room{};
        if (!RoomManager::Instance().GetRoom(roomId, room))
        {
            std::cout << "[ROOM STATE REQUEST FAIL] room not found roomId="
                << roomId
                << std::endl;
            break;
        }

        bool foundMember = false;

        for (const RoomMember& member : room.members)
        {
            if (member.playerId == session->GetPlayerId())
            {
                foundMember = true;
                break;
            }
        }

        if (!foundMember)
        {
            std::cout << "[ROOM STATE REQUEST FAIL] player not in room playerId="
                << session->GetPlayerId()
                << " roomId=" << roomId
                << std::endl;
            break;
        }

        session->SetState(SessionState::IN_ROOM);
        session->SetCurrentRoomId(roomId);

        SC_RoomState out{};
        FillRoomStatePacket(out, room);

        session->Send(&out);

        std::cout << "[ROOM STATE REQUEST OK] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SPIKE_PICKUP_REQUEST:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        std::cout << "[SPIKE PICKUP REQUEST] playerId="
            << session->GetPlayerId()
            << " roomId=" << roomId
            << " state=" << static_cast<int>(session->GetState())
            << std::endl;

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        // 추가
        if (session->IsDead())
        {
            std::cout << "[SPIKE PICKUP BLOCK] player is dead. playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "SPIKE_PICKUP"))
            break;

        RoomMember member{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        bool pickupOk = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            RoomTeam attackTeam = GetAttackTeam(state.currentRound);

            std::cout << "[SPIKE PICKUP CHECK] "
                << "playerId=" << session->GetPlayerId()
                << " memberTeam=" << static_cast<int>(member.team)
                << " attackTeam=" << static_cast<int>(attackTeam)
                << " planted=" << state.spikePlanted
                << " carrier=" << state.spikeCarrierPlayerId
                << " resolving=" << state.roundResolving
                << " matchEnded=" << state.matchEnded
                << std::endl;

            if (state.roundResolving || state.matchEnded)
                break;

            if (state.spikePlanted)
                break;

            if (member.team != attackTeam)
                break;

            if (state.spikeCarrierPlayerId != 0)
                break;

            state.spikeCarrierPlayerId = session->GetPlayerId();
            pickupOk = true;
        }

        if (pickupOk)
        {
            std::cout << "[SPIKE PICKUP SUCCESS] carrier="
                << session->GetPlayerId()
                << std::endl;

            BroadcastSpikePickupNotify(roomId, session->GetPlayerId());
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DROP_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_SpikeDropRequest*>(header);

        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        if (session->IsDead())
            break;

        if (IsDroneBlockedAction(session, "SPIKE_DROP"))
            break;

        bool dropOk = false;

        Vector3 dropPos{};
        dropPos.x = pkt->x;
        dropPos.y = pkt->y;
        dropPos.z = pkt->z;

        Vector3 dropDir{};
        dropDir.x = pkt->dirX;
        dropDir.y = pkt->dirY;
        dropDir.z = pkt->dirZ;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                break;

            if (state.spikePlanted)
                break;

            if (state.spikeCarrierPlayerId != session->GetPlayerId())
                break;

            state.spikeCarrierPlayerId = 0;
            dropOk = true;
        }

        if (dropOk)
        {
            BroadcastSpikeDropped(
                roomId,
                dropPos.x,
                dropPos.y,
                dropPos.z,
                dropDir.x,
                dropDir.y,
                dropDir.z,
                session->GetPlayerId()
            );

            std::cout << "[SPIKE DROP REQUEST OK] playerId="
                << session->GetPlayerId()
                << " pos=(" << dropPos.x << ", " << dropPos.y << ", " << dropPos.z << ")"
                << " dir=(" << dropDir.x << ", " << dropDir.y << ", " << dropDir.z << ")"
                << std::endl;
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_PLANT_COMPLETE:
    {
        auto* pkt = reinterpret_cast<CS_SpikePlantComplete*>(header);

        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        if (IsDroneBlockedAction(session, "SPIKE_PLANT"))
            break;

        RoomMember member{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        bool plantOk = false;
        uint64_t spikeGeneration = 0;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                break;

            if (state.spikePlanted)
                break;

            RoomTeam attackTeam = GetAttackTeam(state.currentRound);

            if (member.team != attackTeam)
                break;

            if (state.spikeCarrierPlayerId != session->GetPlayerId())
                break;

            state.spikeCarrierPlayerId = 0;
            state.spikePlanted = true;

            state.spikeDefuserPlayerId = 0;
            state.spikeHalfDefused = false;

            // 이번 설치만의 고유 번호를 발급한다.
            state.spikePlantGeneration++;
            spikeGeneration = state.spikePlantGeneration;

            plantOk = true;
        }

        if (plantOk)
        {
            BroadcastSpikePlanted(
                roomId,
                pkt->x,
                pkt->y,
                pkt->z
            );

            int plantCredit = GetCreditReward("SPIKE_PLANT");

            session->AddCredits(plantCredit);
            session->GetMatchStats().spikePlants++;

            std::cout << "[CREDIT SPIKE PLANT] playerId="
                << session->GetPlayerId()
                << " add=" << plantCredit
                << " total=" << session->GetCredits()
                << std::endl;

            BroadcastScoreboardUpdate(roomId);

            StartSpikeTimerThread(
                roomId,
                spikeGeneration
            );
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DEFUSE_COMPLETE:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        if (IsDroneBlockedAction(session, "SPIKE_DEFUSE_COMPLETE"))
            break;

        RoomMember member{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        bool defuseOk = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (state.roundResolving || state.matchEnded)
                break;

            if (!state.spikePlanted)
                break;

            RoomTeam defenseTeam = GetDefenseTeam(state.currentRound);

            if (member.team != defenseTeam)
                break;

            if (state.spikeDefuserPlayerId != session->GetPlayerId())
                break;

            defuseOk = true;
        }

        if (defuseOk)
        {
            session->GetMatchStats().spikeDefuses++;
            ResolveRoundBySpikeDefuse(
                roomId,
                session->GetPlayerId()
            );
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DEFUSE_START:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        if (IsDroneBlockedAction(session, "SPIKE_DEFUSE_START"))
            break;

        RoomMember member{};
        if (!RoomManager::Instance().GetMember(roomId, session->GetPlayerId(), member))
            break;

        bool startOk = false;
        bool halfDefused = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (!state.spikePlanted || state.roundResolving || state.matchEnded)
                break;

            RoomTeam defenseTeam = GetDefenseTeam(state.currentRound);

            if (member.team != defenseTeam)
                break;

            if (state.spikeDefuserPlayerId != 0)
                break;

            state.spikeDefuserPlayerId = session->GetPlayerId();
            halfDefused = state.spikeHalfDefused;
            startOk = true;
        }

        if (startOk)
        {
            BroadcastSpikeDefuseState(
                roomId,
                1,
                halfDefused,
                session->GetPlayerId()
            );
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DEFUSE_CANCEL:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        bool cancelOk = false;
        bool halfDefused = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (state.spikeDefuserPlayerId != session->GetPlayerId())
                break;

            state.spikeDefuserPlayerId = 0;
            halfDefused = state.spikeHalfDefused;
            cancelOk = true;
        }

        if (cancelOk)
        {
            BroadcastSpikeDefuseState(
                roomId,
                0,
                halfDefused,
                0
            );
        }

        break;
    }

    case PacketType::PKT_CS_SPIKE_DEFUSE_HALF:
    {
        uint32_t roomId = session->GetCurrentRoomId();

        if (session->GetState() != SessionState::IN_GAME || roomId == 0)
            break;

        bool halfOk = false;

        {
            std::lock_guard<std::mutex> lock(g_matchStateMutex);

            auto it = g_matchStates.find(roomId);
            if (it == g_matchStates.end())
                break;

            MatchRuntimeState& state = it->second;

            if (!state.spikePlanted || state.roundResolving || state.matchEnded)
                break;

            if (state.spikeDefuserPlayerId != session->GetPlayerId())
                break;

            if (!state.spikeHalfDefused)
            {
                state.spikeHalfDefused = true;
                halfOk = true;
            }
        }

        if (halfOk)
        {
            BroadcastSpikeDefuseState(
                roomId,
                2,
                true,
                session->GetPlayerId()
            );
        }

        break;
    }

    case PacketType::PKT_CS_WEAPON_DROP_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_WeaponDropRequest*>(header);

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (IsDroneBlockedAction(session, "WEAPON_DROP"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        int weaponId = pkt->weaponId;

        if (!session->HasWeapon(weaponId))
        {
            std::cout << "[WEAPON DROP BLOCK] not owned. playerId="
                << session->GetPlayerId()
                << " weaponId=" << weaponId
                << std::endl;
            break;
        }

        PlayerWeaponState* weaponState = session->GetWeaponState(weaponId);

        if (weaponState == nullptr)
            break;

        int magAmmo = weaponState->magAmmo;
        int reserveAmmo = weaponState->reserveAmmo;
        int skinId = weaponState->skinId;

        session->RemoveWeapon(weaponId);

        CreateDroppedWeapon(
            roomId,
            session->GetPlayerId(),
            weaponId,
            magAmmo,
            reserveAmmo,
            skinId,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->dirX,
            pkt->dirY,
            pkt->dirZ
        );

        int currentWeaponId = session->GetCurrentWeaponId();

        PlayerWeaponState* currentState = session->GetWeaponState(currentWeaponId);

        if (currentState != nullptr)
            SendAmmoUpdate(session, *currentState);

        if (currentWeaponId >= 0 && session->HasWeapon(currentWeaponId))
        {
            BroadcastWeaponSwitchNotify(
                roomId,
                session->GetPlayerId(),
                currentWeaponId
            );
        }

        std::cout << "[WEAPON DROP APPLY] playerId="
            << session->GetPlayerId()
            << " droppedWeaponId=" << weaponId
            << " currentWeaponId=" << currentWeaponId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_WEAPON_PICKUP_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_WeaponPickupRequest*>(header);

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (IsDroneBlockedAction(session, "WEAPON_PICKUP"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        DroppedWeaponState dropped{};

        {
            std::lock_guard<std::mutex> lock(g_droppedWeaponMutex);

            auto it = g_droppedWeapons.find(pkt->dropId);

            if (it == g_droppedWeapons.end())
            {
                std::cout << "[WEAPON PICKUP BLOCK] drop not found. dropId="
                    << pkt->dropId
                    << std::endl;
                break;
            }

            if (it->second.roomId != roomId)
            {
                std::cout << "[WEAPON PICKUP BLOCK] different room. dropId="
                    << pkt->dropId
                    << std::endl;
                break;
            }

            dropped = it->second;
            g_droppedWeapons.erase(it);
        }

        int replaceWeaponId =
            FindReplaceWeaponIdForPickup(session, dropped.weaponId);

        if (replaceWeaponId >= 0)
        {
            PlayerWeaponState* replaceState =
                session->GetWeaponState(replaceWeaponId);

            if (replaceState != nullptr)
            {
                int oldMag = replaceState->magAmmo;
                int oldReserve = replaceState->reserveAmmo;
                int oldSkinId = replaceState->skinId;

                session->RemoveWeapon(replaceWeaponId);

                Vector3 pos = session->GetPosition();

                float yawRad = session->GetYaw() * 3.14159265f / 180.0f;

                float dirX = sinf(yawRad);
                float dirZ = cosf(yawRad);

                float dropX = pos.x + dirX * 1.0f;
                float dropY = pos.y + 0.6f;
                float dropZ = pos.z + dirZ * 1.0f;

                CreateDroppedWeapon(
                    roomId,
                    session->GetPlayerId(),
                    replaceWeaponId,
                    oldMag,
                    oldReserve,
                    oldSkinId,
                    dropX,
                    dropY,
                    dropZ,
                    dirX,
                    0.f,
                    dirZ
                );
            }
        }

        session->GiveWeapon(
            dropped.weaponId,
            dropped.magAmmo,
            dropped.reserveAmmo,
            dropped.skinId
        );

        session->SetCurrentWeaponId(dropped.weaponId);

        PlayerWeaponState* newWeaponState =
            session->GetWeaponState(dropped.weaponId);

        if (newWeaponState != nullptr)
            SendAmmoUpdate(session, *newWeaponState);

        SC_WeaponPickupNotify out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_WEAPON_PICKUP_NOTIFY);

        out.dropId = dropped.dropId;
        out.pickerPlayerId = session->GetPlayerId();
        out.weaponId = dropped.weaponId;
        out.magAmmo = dropped.magAmmo;
        out.reserveAmmo = dropped.reserveAmmo;
        out.skinId = dropped.skinId;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[WEAPON PICKUP APPLY] playerId="
            << session->GetPlayerId()
            << " weaponId=" << dropped.weaponId
            << " ammo=" << dropped.magAmmo << "/" << dropped.reserveAmmo
            << " skinId=" << dropped.skinId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_STORE_BUY_REQUEST:
    {
        auto* pkt = reinterpret_cast<CS_StoreBuyRequest*>(header);

        StoreItemType itemType =
            static_cast<StoreItemType>(pkt->itemType);

        if (session->GetState() != SessionState::IN_GAME)
        {
            SendStoreBuyResult(
                session,
                StoreBuyResult::NOT_IN_GAME,
                itemType,
                pkt->itemId,
                0,
                0,
                0,
                0
            );
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            SendStoreBuyResult(
                session,
                StoreBuyResult::NOT_IN_GAME,
                itemType,
                pkt->itemId,
                0,
                0,
                0,
                0
            );
            break;
        }

        if (!IsBuyPhase(roomId))
        {
            SendStoreBuyResult(
                session,
                StoreBuyResult::NOT_BUY_PHASE,
                itemType,
                pkt->itemId,
                0,
                0,
                0,
                0
            );
            break;
        }

        if (itemType == StoreItemType::WEAPON)
        {
            auto it = g_storeWeapons.find(pkt->itemId);

            if (it == g_storeWeapons.end() || !it->second.purchasable)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::INVALID_ITEM,
                    itemType,
                    pkt->itemId,
                    0,
                    0,
                    0,
                    0
                );
                break;
            }

            StoreWeaponData storeData = it->second;

            const ServerWeaponData* weaponData =
                ServerWeaponTable::Find(storeData.weaponId);

            if (weaponData == nullptr)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::INVALID_ITEM,
                    itemType,
                    pkt->itemId,
                    storeData.price,
                    storeData.weaponId,
                    0,
                    0
                );
                break;
            }

            if (session->GetCredits() < storeData.price)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::NOT_ENOUGH_CREDIT,
                    itemType,
                    pkt->itemId,
                    storeData.price,
                    storeData.weaponId,
                    0,
                    0
                );
                break;
            }

            if (session->HasWeapon(storeData.weaponId))
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::ALREADY_OWNED,
                    itemType,
                    pkt->itemId,
                    storeData.price,
                    storeData.weaponId,
                    0,
                    0
                );
                break;
            }

            int replaceWeaponId =
                FindReplaceWeaponIdForPickup(session, storeData.weaponId);

            if (replaceWeaponId >= 0)
            {
                PlayerWeaponState* oldWeaponState =
                    session->GetWeaponState(replaceWeaponId);

                int oldMagAmmo = 0;
                int oldReserveAmmo = 0;
                int oldSkinId = 0;

                if (oldWeaponState != nullptr)
                {
                    oldMagAmmo = oldWeaponState->magAmmo;
                    oldReserveAmmo = oldWeaponState->reserveAmmo;
                    oldSkinId = oldWeaponState->skinId;
                }

                Vector3 pos = session->GetPosition();

                float yawRad = session->GetYaw() * 3.14159265f / 180.0f;

                float dirX = sinf(yawRad);
                float dirZ = cosf(yawRad);

                float dropX = pos.x + dirX * 1.0f;
                float dropY = pos.y + 0.6f;
                float dropZ = pos.z + dirZ * 1.0f;

                CreateDroppedWeapon(
                    roomId,
                    session->GetPlayerId(),
                    replaceWeaponId,
                    oldMagAmmo,
                    oldReserveAmmo,
                    oldSkinId,
                    dropX,
                    dropY,
                    dropZ,
                    dirX,
                    0.0f,
                    dirZ
                );

                session->RemoveWeapon(replaceWeaponId);
            }

            session->AddCredits(-storeData.price);

            int purchasedSkinId = GetEquippedSkinIdForUser(
                session->GetUserId(),
                session->GetActiveCharacterId(),
                storeData.weaponId
            );

            session->GiveWeapon(
                storeData.weaponId,
                weaponData->magSize,
                weaponData->maxReserveAmmo,
                purchasedSkinId
            );

            session->SetCurrentWeaponId(storeData.weaponId);

            PlayerWeaponState* newWeaponState =
                session->GetWeaponState(storeData.weaponId);

            if (newWeaponState != nullptr)
                SendAmmoUpdate(session, *newWeaponState);

            SendStoreBuyResult(
                session,
                StoreBuyResult::SUCCESS,
                itemType,
                pkt->itemId,
                storeData.price,
                storeData.weaponId,
                weaponData->magSize,
                weaponData->maxReserveAmmo
            );

            // dropId = 0 이면 상점 구매다.
            // 클라는 바닥 아이템 삭제하지 말고 무기만 적용하면 된다.
            BroadcastWeaponPickupNotify(
                roomId,
                0,
                session->GetPlayerId(),
                storeData.weaponId,
                weaponData->magSize,
                weaponData->maxReserveAmmo,
                purchasedSkinId
            );

            BroadcastWeaponSwitchNotify(
                roomId,
                session->GetPlayerId(),
                storeData.weaponId
            );

            BroadcastScoreboardUpdate(roomId);

            std::cout << "[STORE BUY WEAPON SUCCESS] playerId="
                << session->GetPlayerId()
                << " weaponId=" << storeData.weaponId
                << " price=" << storeData.price
                << " credits=" << session->GetCredits()
                << std::endl;

            break;
        }

        if (itemType == StoreItemType::ARMOR)
        {
            auto it = g_storeArmors.find(pkt->itemId);

            if (it == g_storeArmors.end())
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::INVALID_ITEM,
                    itemType,
                    pkt->itemId,
                    0,
                    0,
                    0,
                    0
                );
                break;
            }

            StoreArmorData armorData = it->second;

            // 남아 있는 보호막이 있고, 현재 등급이 구매하려는 등급 이상일 때만 재구매 차단.
            // 예: 50 보호막 보유 중 -> 50 재구매 불가, 100 업그레이드 가능.
            // 보호막이 0이면 파괴된 상태이므로 같은 등급도 다시 구매 가능.
            bool hasActiveArmor = session->GetBarrier() > 0;
            bool alreadyOwnsSameOrBetterArmor =
                hasActiveArmor &&
                session->GetMaxBarrier() >= armorData.barrier;

            if (alreadyOwnsSameOrBetterArmor)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::ALREADY_OWNED,
                    itemType,
                    pkt->itemId,
                    armorData.price,
                    0,
                    0,
                    0
                );
                break;
            }

            if (session->GetCredits() < armorData.price)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::NOT_ENOUGH_CREDIT,
                    itemType,
                    pkt->itemId,
                    armorData.price,
                    0,
                    0,
                    0
                );
                break;
            }

            session->AddCredits(-armorData.price);
            session->SetArmor(armorData.barrier);

            SC_HPUpdate hpOut{};
            FillHPUpdatePacket(hpOut, session);
            BroadcastPacketToRoomInternal(roomId, &hpOut);

            SendStoreBuyResult(
                session,
                StoreBuyResult::SUCCESS,
                itemType,
                pkt->itemId,
                armorData.price,
                0,
                0,
                0
            );

            BroadcastScoreboardUpdate(roomId);

            std::cout << "[STORE BUY ARMOR SUCCESS] playerId="
                << session->GetPlayerId()
                << " armor=" << armorData.barrier
                << " price=" << armorData.price
                << " credits=" << session->GetCredits()
                << std::endl;

            break;
        }

        if (itemType == StoreItemType::SKILL)
        {
            int price = GetSkillPrice(pkt->itemId);
            bool validSlot =
                pkt->itemId >= static_cast<int>(PlayerSkillSlot::C) &&
                pkt->itemId <= static_cast<int>(PlayerSkillSlot::X);

            if (!validSlot || price < 0)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::INVALID_ITEM,
                    itemType,
                    pkt->itemId,
                    0,
                    0,
                    0,
                    0
                );
                break;
            }

            PlayerSkillSlot slot =
                static_cast<PlayerSkillSlot>(pkt->itemId);

            if (session->HasPurchasedSkill(slot))
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::ALREADY_OWNED,
                    itemType,
                    pkt->itemId,
                    price,
                    0,
                    0,
                    0
                );
                break;
            }

            if (session->GetCredits() < price)
            {
                SendStoreBuyResult(
                    session,
                    StoreBuyResult::NOT_ENOUGH_CREDIT,
                    itemType,
                    pkt->itemId,
                    price,
                    0,
                    0,
                    0
                );
                break;
            }

            session->AddCredits(-price);
            session->PurchaseSkill(slot);

            SendStoreBuyResult(
                session,
                StoreBuyResult::SUCCESS,
                itemType,
                pkt->itemId,
                price,
                0,
                0,
                0
            );

            BroadcastScoreboardUpdate(roomId);

            std::cout << "[STORE BUY SKILL SUCCESS] playerId="
                << session->GetPlayerId()
                << " slot=" << pkt->itemId
                << " price=" << price
                << " credits=" << session->GetCredits()
                << std::endl;
            break;
        }

        SendStoreBuyResult(
            session,
            StoreBuyResult::INVALID_ITEM,
            itemType,
            pkt->itemId,
            0,
            0,
            0,
            0
        );

        break;
    }

    case PacketType::PKT_CS_RELOAD_VISUAL:
    {
        auto* pkt =
            reinterpret_cast<CS_ReloadVisual*>(
                header
                );

        if (header->size != sizeof(CS_ReloadVisual))
            break;

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (IsDroneBlockedAction(session, "RELOAD_VISUAL"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();
        if (roomId == 0)
            break;

        PlayerWeaponState* weaponState =
            session->GetWeaponState(pkt->weaponId);

        if (weaponState == nullptr || !weaponState->owned)
            break;

        bool wantsReload = pkt->isReloading != 0;

        if (wantsReload)
        {
            // 장전 시작은 현재 들고 있는 무기에 대해서만 허용한다.
            if (pkt->weaponId != session->GetCurrentWeaponId())
            {
                std::cout << "[RELOAD START BLOCK] not current weapon. playerId="
                    << session->GetPlayerId()
                    << " weaponId=" << pkt->weaponId
                    << " currentWeaponId=" << session->GetCurrentWeaponId()
                    << std::endl;
                break;
            }

            const ServerWeaponData* weaponData =
                ServerWeaponTable::Find(pkt->weaponId);

            bool isBuyPhase = IsBuyPhase(roomId);
            bool cannotReload =
                weaponData == nullptr ||
                weaponData->weaponType == ServerWeaponType::Knife;

            if (!cannotReload)
            {
                if (isBuyPhase)
                {
                    cannotReload =
                        weaponState->magAmmo >= weaponData->magSize &&
                        weaponState->reserveAmmo >= weaponData->maxReserveAmmo;
                }
                else
                {
                    cannotReload =
                        weaponState->magAmmo >= weaponData->magSize ||
                        weaponState->reserveAmmo <= 0;
                }
            }

            if (cannotReload)
            {
                weaponState->isReloading = false;

                SendAmmoUpdate(session, *weaponState);
                break;
            }

            weaponState->isReloading = true;

            BroadcastReloadVisualInternal(
                roomId,
                session->GetPlayerId(),
                pkt->weaponId,
                true
            );

            std::cout << "[RELOAD START ACCEPT] playerId="
                << session->GetPlayerId()
                << " weaponId=" << pkt->weaponId
                << std::endl;
        }
        else
        {
            // 취소는 이미 다른 무기로 바뀐 뒤 도착해도 허용한다.
            // 해당 무기의 남아 있는 장전 상태만 제거한다.
            bool wasReloading = weaponState->isReloading;

            weaponState->isReloading = false;

            BroadcastReloadVisualInternal(
                roomId,
                session->GetPlayerId(),
                pkt->weaponId,
                false
            );

            if (wasReloading)
            {
                std::cout << "[RELOAD CANCEL ACCEPT] playerId="
                    << session->GetPlayerId()
                    << " weaponId=" << pkt->weaponId
                    << std::endl;
            }
        }

        break;
    }

    case PacketType::PKT_CS_IN_GAME_CHAT:
    {
        if (header->size !=
            sizeof(CS_InGameChat))
        {
            std::cout
                << "[IN GAME CHAT BLOCK] invalid size="
                << header->size
                << std::endl;

            break;
        }

        if (session->GetState() !=
            SessionState::IN_GAME)
        {
            std::cout
                << "[IN GAME CHAT BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        auto* pkt =
            reinterpret_cast<CS_InGameChat*>(
                header
                );

        bool isTeamChat =
            pkt->channel ==
            static_cast<uint8_t>(
                InGameChatChannel::TEAM
                );

        bool isMatchChat =
            pkt->channel ==
            static_cast<uint8_t>(
                InGameChatChannel::MATCH
                );

        if (!isTeamChat &&
            !isMatchChat)
        {
            std::cout
                << "[IN GAME CHAT BLOCK] invalid channel="
                << static_cast<int>(pkt->channel)
                << std::endl;

            break;
        }

        size_t messageLength =
            strnlen_s(
                pkt->message,
                IN_GAME_CHAT_MESSAGE_LEN
            );

        if (messageLength == 0 ||
            messageLength >=
            IN_GAME_CHAT_MESSAGE_LEN)
        {
            std::cout
                << "[IN GAME CHAT BLOCK] invalid message length="
                << messageLength
                << std::endl;

            break;
        }

        Room room{};

        if (!RoomManager::Instance().GetRoom(
            roomId,
            room))
        {
            break;
        }

        RoomMember senderMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            senderMember))
        {
            break;
        }

        SC_InGameChat out{};

        out.header.size =
            sizeof(out);

        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_IN_GAME_CHAT
                );

        out.senderPlayerId =
            session->GetPlayerId();

        out.channel =
            pkt->channel;

        out.senderTeam =
            static_cast<uint8_t>(
                senderMember.team
                );

        memset(
            out.nickname,
            0,
            sizeof(out.nickname)
        );

        strncpy_s(
            out.nickname,
            sizeof(out.nickname),
            senderMember.nickname.c_str(),
            _TRUNCATE
        );

        memset(
            out.message,
            0,
            sizeof(out.message)
        );

        strncpy_s(
            out.message,
            sizeof(out.message),
            pkt->message,
            _TRUNCATE
        );

        int sentCount = 0;

        for (const RoomMember& receiverMember :
            room.members)
        {
            if (isTeamChat &&
                receiverMember.team !=
                senderMember.team)
            {
                continue;
            }

            auto receiver =
                SessionManager::Instance().Find(
                    receiverMember.playerId
                );

            if (receiver == nullptr)
                continue;

            if (receiver->GetCurrentRoomId() !=
                roomId)
            {
                continue;
            }

            if (receiver->GetState() !=
                SessionState::IN_GAME)
            {
                continue;
            }

            receiver->Send(
                &out
            );

            sentCount++;
        }

        std::cout
            << "[IN GAME CHAT] roomId="
            << roomId
            << " sender="
            << session->GetPlayerId()
            << " team="
            << static_cast<int>(
                senderMember.team
                )
            << " channel="
            << static_cast<int>(
                pkt->channel
                )
            << " targetCount="
            << sentCount
            << " message="
            << pkt->message
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_DRONE_DEPLOY:
    {
        auto* pkt = reinterpret_cast<CS_DroneDeploy*>(header);

        if (header->size != sizeof(CS_DroneDeploy))
        {
            std::cout
                << "[DRONE DEPLOY BLOCK] invalid size. size="
                << header->size
                << " expected=" << sizeof(CS_DroneDeploy)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[DRONE DEPLOY BLOCK] invalid state. playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[DRONE DEPLOY BLOCK] dead player. playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout
                << "[DRONE DEPLOY BLOCK] roomId is 0. playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t ownerPlayerId = session->GetPlayerId();
        uint32_t droneId = 0;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            if (g_ownerDroneIds.find(ownerPlayerId) != g_ownerDroneIds.end())
            {
                std::cout
                    << "[DRONE DEPLOY BLOCK] already has drone. owner="
                    << ownerPlayerId
                    << std::endl;

                break;
            }

            droneId = g_nextDroneId++;

            ServerDroneState drone{};
            drone.droneId = droneId;
            drone.ownerPlayerId = ownerPlayerId;
            drone.roomId = roomId;

            drone.x = pkt->x;
            drone.y = pkt->y;
            drone.z = pkt->z;

            drone.yaw = pkt->yaw;
            drone.pitch = pkt->pitch;

            drone.hp = DRONE_MAX_HP;

            g_drones[droneId] = drone;
            g_ownerDroneIds[ownerPlayerId] = droneId;
        }

        BroadcastDroneSpawn(
            roomId,
            droneId,
            ownerPlayerId,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->yaw,
            pkt->pitch
        );

        BroadcastDroneHpUpdate(
            roomId,
            droneId,
            ownerPlayerId,
            DRONE_MAX_HP,
            DRONE_MAX_HP
        );

        std::cout
            << "[DRONE DEPLOY OK] "
            << "owner=" << ownerPlayerId
            << " droneId=" << droneId
            << " hp=" << DRONE_MAX_HP
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_DRONE_STATE:
    {
        auto* pkt = reinterpret_cast<CS_DroneState*>(header);

        if (header->size != sizeof(CS_DroneState))
            break;

        if (session->GetState() != SessionState::IN_GAME)
            break;

        uint32_t roomId = session->GetCurrentRoomId();
        uint32_t ownerPlayerId = session->GetPlayerId();

        if (roomId == 0)
            break;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto it = g_drones.find(pkt->droneId);

            if (it == g_drones.end())
                break;

            ServerDroneState& drone = it->second;

            if (drone.ownerPlayerId != ownerPlayerId)
                break;

            if (drone.roomId != roomId)
                break;

            drone.x = pkt->x;
            drone.y = pkt->y;
            drone.z = pkt->z;

            drone.yaw = pkt->yaw;
            drone.pitch = pkt->pitch;
        }

        BroadcastDroneState(
            roomId,
            pkt->droneId,
            ownerPlayerId,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->yaw,
            pkt->pitch
        );

        break;
    }
    case PacketType::PKT_CS_DRONE_EXIT:
    {
        auto* pkt = reinterpret_cast<CS_DroneExit*>(header);

        if (header->size != sizeof(CS_DroneExit))
            break;

        if (session->GetState() != SessionState::IN_GAME)
            break;

        uint32_t roomId = session->GetCurrentRoomId();
        uint32_t ownerPlayerId = session->GetPlayerId();

        if (roomId == 0)
            break;

        bool destroyed = false;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto it = g_drones.find(pkt->droneId);

            if (it == g_drones.end())
                break;

            ServerDroneState& drone = it->second;

            if (drone.ownerPlayerId != ownerPlayerId)
                break;

            if (drone.roomId != roomId)
                break;

            g_ownerDroneIds.erase(ownerPlayerId);
            g_drones.erase(it);

            destroyed = true;
        }

        if (destroyed)
        {
            BroadcastDroneDestroy(
                roomId,
                pkt->droneId,
                ownerPlayerId,
                0
            );
        }

        break;
    }
    case PacketType::PKT_CS_DRONE_HIT:
    {
        auto* pkt = reinterpret_cast<CS_DroneHit*>(header);

        if (header->size != sizeof(CS_DroneHit))
        {
            std::cout
                << "[DRONE HIT BLOCK] invalid size. size="
                << header->size
                << " expected=" << sizeof(CS_DroneHit)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (session->IsDead())
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
            break;

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
            break;

        uint32_t attackerPlayerId = session->GetPlayerId();

        uint32_t ownerPlayerId = 0;
        int hpAfter = 0;
        bool destroyed = false;

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto it = g_drones.find(pkt->droneId);

            if (it == g_drones.end())
            {
                std::cout
                    << "[DRONE HIT BLOCK] drone not found. droneId="
                    << pkt->droneId
                    << std::endl;

                break;
            }

            ServerDroneState& drone = it->second;

            if (drone.roomId != roomId)
                break;

            if (drone.ownerPlayerId == attackerPlayerId)
            {
                std::cout
                    << "[DRONE HIT BLOCK] own drone. attacker="
                    << attackerPlayerId
                    << " droneId=" << pkt->droneId
                    << std::endl;

                break;
            }

            RoomMember attackerMember{};
            RoomMember ownerMember{};

            if (!RoomManager::Instance().GetMember(
                roomId,
                attackerPlayerId,
                attackerMember))
            {
                break;
            }

            if (!RoomManager::Instance().GetMember(
                roomId,
                drone.ownerPlayerId,
                ownerMember))
            {
                break;
            }

            if (attackerMember.team == ownerMember.team)
            {
                std::cout
                    << "[DRONE HIT BLOCK] friendly drone. attacker="
                    << attackerPlayerId
                    << " owner=" << drone.ownerPlayerId
                    << std::endl;

                break;
            }

            drone.hp -= weaponData->damage;

            if (drone.hp < 0)
                drone.hp = 0;

            ownerPlayerId = drone.ownerPlayerId;
            hpAfter = drone.hp;
            destroyed = drone.hp <= 0;

            std::cout
                << "[DRONE HIT APPLY] "
                << "attacker=" << attackerPlayerId
                << " droneId=" << pkt->droneId
                << " owner=" << ownerPlayerId
                << " weaponId=" << weaponId
                << " damage=" << weaponData->damage
                << " hp=" << hpAfter
                << "/" << DRONE_MAX_HP
                << std::endl;

            if (destroyed)
            {
                g_ownerDroneIds.erase(ownerPlayerId);
                g_drones.erase(it);
            }
        }

        BroadcastDroneHpUpdate(
            roomId,
            pkt->droneId,
            ownerPlayerId,
            hpAfter,
            DRONE_MAX_HP
        );

        if (destroyed)
        {
            BroadcastDroneDestroy(
                roomId,
                pkt->droneId,
                ownerPlayerId,
                1
            );
        }

        break;
    }
    case PacketType::PKT_CS_DRONE_TASER_FIRE:
    {
        auto* pkt = reinterpret_cast<CS_DroneTaserFire*>(header);

        if (header->size != sizeof(CS_DroneTaserFire))
        {
            std::cout
                << "[DRONE TASER BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[DRONE TASER BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[DRONE TASER BLOCK] owner dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        constexpr float DRONE_TASER_RANGE = 100.f;
        constexpr float DRONE_TASER_RANGE_SQ =
            DRONE_TASER_RANGE * DRONE_TASER_RANGE;

        constexpr float DRONE_TASER_DURATION = 3.f;
        constexpr int32_t DRONE_TASER_DAMAGE = 10;
        constexpr uint8_t DRONE_TASER_STACK = 1;

        ServerDroneState droneSnapshot{};

        {
            std::lock_guard<std::mutex> lock(g_droneMutex);

            auto droneIt = g_drones.find(pkt->droneId);

            if (droneIt == g_drones.end())
            {
                std::cout
                    << "[DRONE TASER BLOCK] drone not found droneId="
                    << pkt->droneId
                    << std::endl;

                break;
            }

            ServerDroneState& drone = droneIt->second;

            if (drone.ownerPlayerId != session->GetPlayerId())
            {
                std::cout
                    << "[DRONE TASER BLOCK] not owner playerId="
                    << session->GetPlayerId()
                    << " droneOwner="
                    << drone.ownerPlayerId
                    << std::endl;

                break;
            }

            if (drone.roomId != roomId)
            {
                std::cout
                    << "[DRONE TASER BLOCK] room mismatch playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (drone.taserUsed)
            {
                std::cout
                    << "[DRONE TASER BLOCK] already used droneId="
                    << pkt->droneId
                    << std::endl;

                break;
            }

            // 핵심:
            // 맞췄든 못 맞췄든 여기서 1회 사용 확정.
            drone.taserUsed = true;
            droneSnapshot = drone;
        }

        bool validHit = false;
        uint32_t finalTargetPlayerId = 0;

        if (pkt->targetPlayerId != 0)
        {
            auto target =
                SessionManager::Instance().Find(pkt->targetPlayerId);

            if (target != nullptr &&
                target->GetCurrentRoomId() == roomId &&
                !target->IsDead())
            {
                RoomMember ownerMember{};
                RoomMember targetMember{};

                bool ownerOk =
                    RoomManager::Instance().GetMember(
                        roomId,
                        session->GetPlayerId(),
                        ownerMember
                    );

                bool targetOk =
                    RoomManager::Instance().GetMember(
                        roomId,
                        target->GetPlayerId(),
                        targetMember
                    );

                if (ownerOk &&
                    targetOk &&
                    ownerMember.team != targetMember.team)
                {
                    Vector3 targetPos = target->GetPosition();

                    float dx = targetPos.x - droneSnapshot.x;
                    float dy = targetPos.y - droneSnapshot.y;
                    float dz = targetPos.z - droneSnapshot.z;

                    float distSq =
                        dx * dx +
                        dy * dy +
                        dz * dz;

                    if (distSq <= DRONE_TASER_RANGE_SQ)
                    {
                        validHit = true;
                        finalTargetPlayerId = target->GetPlayerId();

                        // 핵심:
                        // 드론 테이저 적중 시 감전 스택 1개 추가
                        AddElectricShockStack(
                            roomId,
                            target->GetPlayerId(),
                            DRONE_TASER_STACK
                        );

                        target->SetLastDamager(
                            session->GetPlayerId()
                        );

                        target->AddAssistDamageRecord(
                            session->GetPlayerId(),
                            DRONE_TASER_DAMAGE
                        );

                        int appliedDamage = target->Damage(
                            DRONE_TASER_DAMAGE
                        );
                        session->GetMatchStats().damageDealt += appliedDamage;

                        SC_HPUpdate hpPkt{};
                        FillHPUpdatePacket(
                            hpPkt,
                            target.get()
                        );

                        BroadcastPacketToRoomInternal(
                            roomId,
                            &hpPkt
                        );

                        std::cout
                            << "[DRONE TASER HIT] owner="
                            << session->GetPlayerId()
                            << " target="
                            << target->GetPlayerId()
                            << " damage="
                            << DRONE_TASER_DAMAGE
                            << " targetHp="
                            << target->GetHP()
                            << std::endl;

                        if (target->IsDead())
                        {
                            HandleDeath(target.get());
                        }
                    }
                }
            }
        }

        BroadcastDroneTaserFire(
            roomId,
            pkt->droneId,
            session->GetPlayerId(),
            finalTargetPlayerId,
            validHit,
            validHit ? DRONE_TASER_STACK : 0,
            DRONE_TASER_DURATION,
            validHit ? DRONE_TASER_DAMAGE : 0
        );



        if (!validHit)
        {
            std::cout
                << "[DRONE TASER MISS/BLOCKED] owner="
                << session->GetPlayerId()
                << " droneId="
                << pkt->droneId
                << " requestedTarget="
                << pkt->targetPlayerId
                << std::endl;
        }

        break;
    }

    case PacketType::PKT_CS_SCANNER_THROW:
    {
        auto* pkt = reinterpret_cast<CS_ScannerThrow*>(header);

        if (header->size != sizeof(CS_ScannerThrow))
        {
            std::cout
                << "[SCANNER THROW BLOCK] invalid size. size="
                << header->size
                << " expected=" << sizeof(CS_ScannerThrow)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[SCANNER THROW BLOCK] invalid state. playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[SCANNER THROW BLOCK] dead player. playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout
                << "[SCANNER THROW BLOCK] roomId is 0. playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t scannerId = 0;
        uint32_t ownerPlayerId = session->GetPlayerId();

        {
            std::lock_guard<std::mutex> lock(g_scannerMutex);

            scannerId = g_nextScannerId++;

            ServerScannerState scanner{};
            scanner.scannerId = scannerId;
            scanner.ownerPlayerId = ownerPlayerId;
            scanner.roomId = roomId;

            scanner.x = pkt->x;
            scanner.y = pkt->y;
            scanner.z = pkt->z;

            scanner.vx = pkt->vx;
            scanner.vy = pkt->vy;
            scanner.vz = pkt->vz;

            scanner.hp = SCANNER_MAX_HP;
            scanner.anchored = false;

            g_scanners[scannerId] = scanner;
        }

        BroadcastScannerSpawn(
            roomId,
            scannerId,
            ownerPlayerId,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->vx,
            pkt->vy,
            pkt->vz
        );

        std::cout
            << "[SCANNER THROW OK] "
            << "owner=" << ownerPlayerId
            << " scannerId=" << scannerId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SCANNER_ANCHOR:
    {
        auto* pkt = reinterpret_cast<CS_ScannerAnchor*>(header);

        if (header->size != sizeof(CS_ScannerAnchor))
        {
            std::cout
                << "[SCANNER ANCHOR BLOCK] invalid size. size="
                << header->size
                << " expected=" << sizeof(CS_ScannerAnchor)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (session->IsDead())
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        uint32_t ownerPlayerId = session->GetPlayerId();

        bool validScanner = false;

        {
            std::lock_guard<std::mutex> lock(g_scannerMutex);

            auto it = g_scanners.find(pkt->scannerId);

            if (it == g_scanners.end())
            {
                std::cout
                    << "[SCANNER ANCHOR BLOCK] scanner not found. scannerId="
                    << pkt->scannerId
                    << std::endl;

                break;
            }

            ServerScannerState& scanner = it->second;

            if (scanner.ownerPlayerId != ownerPlayerId)
                break;

            if (scanner.roomId != roomId)
                break;

            if (scanner.anchored)
                break;

            scanner.anchored = true;

            scanner.x = pkt->x;
            scanner.y = pkt->y;
            scanner.z = pkt->z;

            validScanner = true;
        }

        if (!validScanner)
            break;

        BroadcastScannerAnchored(
            roomId,
            pkt->scannerId,
            pkt->x,
            pkt->y,
            pkt->z
        );

        std::cout
            << "[SCANNER ANCHOR OK] "
            << "scannerId=" << pkt->scannerId
            << " owner=" << ownerPlayerId
            << std::endl;

        std::thread(
            [roomId, scannerId = pkt->scannerId]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(SCANNER_DESTROY_DELAY_MS)
                );

                bool removed = false;

                {
                    std::lock_guard<std::mutex> lock(g_scannerMutex);

                    auto it = g_scanners.find(scannerId);

                    if (it != g_scanners.end())
                    {
                        g_scanners.erase(it);
                        ClearScannerZapRecords(scannerId);
                        removed = true;
                    }
                }

                if (removed)
                {
                    BroadcastScannerDestroy(
                        roomId,
                        scannerId
                    );
                }
            }
        ).detach();

        break;
    }

    case PacketType::PKT_CS_SCANNER_HIT:
    {
        auto* pkt = reinterpret_cast<CS_ScannerHit*>(header);

        if (header->size != sizeof(CS_ScannerHit))
        {
            std::cout
                << "[SCANNER HIT BLOCK] invalid size. size="
                << header->size
                << " expected=" << sizeof(CS_ScannerHit)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (session->IsDead())
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
            break;

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
            break;

        uint32_t attackerPlayerId = session->GetPlayerId();

        bool destroyed = false;
        int hpAfter = 0;
        uint32_t ownerPlayerId = 0;

        {
            std::lock_guard<std::mutex> lock(g_scannerMutex);

            auto it = g_scanners.find(pkt->scannerId);

            if (it == g_scanners.end())
            {
                std::cout
                    << "[SCANNER HIT BLOCK] scanner not found. scannerId="
                    << pkt->scannerId
                    << std::endl;

                break;
            }

            ServerScannerState& scanner = it->second;

            if (scanner.roomId != roomId)
                break;

            if (scanner.ownerPlayerId == attackerPlayerId)
            {
                std::cout
                    << "[SCANNER HIT BLOCK] own scanner. attacker="
                    << attackerPlayerId
                    << " scannerId=" << pkt->scannerId
                    << std::endl;

                break;
            }

            RoomMember attackerMember{};
            RoomMember ownerMember{};

            if (!RoomManager::Instance().GetMember(
                roomId,
                attackerPlayerId,
                attackerMember))
            {
                break;
            }

            if (!RoomManager::Instance().GetMember(
                roomId,
                scanner.ownerPlayerId,
                ownerMember))
            {
                break;
            }

            if (attackerMember.team == ownerMember.team)
            {
                std::cout
                    << "[SCANNER HIT BLOCK] friendly scanner. attacker="
                    << attackerPlayerId
                    << " owner=" << scanner.ownerPlayerId
                    << std::endl;

                break;
            }

            scanner.hp -= weaponData->damage;

            if (scanner.hp < 0)
                scanner.hp = 0;

            ownerPlayerId = scanner.ownerPlayerId;
            hpAfter = scanner.hp;
            destroyed = scanner.hp <= 0;

            std::cout
                << "[SCANNER HIT APPLY] "
                << "attacker=" << attackerPlayerId
                << " scannerId=" << pkt->scannerId
                << " owner=" << ownerPlayerId
                << " weaponId=" << weaponId
                << " damage=" << weaponData->damage
                << " hp=" << hpAfter
                << "/" << SCANNER_MAX_HP
                << std::endl;

            if (destroyed)
            {
                g_scanners.erase(it);
                ClearScannerZapRecords(pkt->scannerId);
            }
        }

        if (destroyed)
        {
            BroadcastScannerDestroy(
                roomId,
                pkt->scannerId
            );
        }

        break;
    }

        case PacketType::PKT_CS_EMP_CAST:
        {
            auto* pkt = reinterpret_cast<CS_EmpCast*>(header);

            if (header->size != sizeof(CS_EmpCast))
            {
                std::cout
                    << "[EMP BLOCK] invalid size. size="
                    << header->size
                    << " expected=" << sizeof(CS_EmpCast)
                    << std::endl;

                break;
            }

            if (session->GetState() != SessionState::IN_GAME)
            {
                std::cout
                    << "[EMP BLOCK] not in game. playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (session->IsDead())
            {
                std::cout
                    << "[EMP BLOCK] caster dead. playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (IsPlayerStunned(session->GetPlayerId()))
            {
                std::cout
                    << "[EMP BLOCK] caster stunned. playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            uint32_t roomId =
                session->GetCurrentRoomId();

            if (roomId == 0)
                break;

            uint32_t casterPlayerId =
                session->GetPlayerId();

            Room room{};

            if (!RoomManager::Instance().GetRoom(roomId, room))
                break;

            RoomMember casterMember{};

            if (!RoomManager::Instance().GetMember(
                roomId,
                casterPlayerId,
                casterMember))
            {
                break;
            }

            Vector3 casterPos =
                session->GetPosition();

            std::vector<uint32_t> hitTargets;

            for (const RoomMember& member : room.members)
            {
                if (member.team == RoomTeam::OBSERVER)
                    continue;

                if (member.playerId == casterPlayerId)
                    continue;

                // 핵심: 적군만
                if (member.team == casterMember.team)
                    continue;

                auto target =
                    SessionManager::Instance().Find(member.playerId);

                if (target == nullptr)
                    continue;

                if (target->GetCurrentRoomId() != roomId)
                    continue;

                if (target->IsDead())
                    continue;

                Vector3 targetPos =
                    target->GetPosition();

                float dx = targetPos.x - casterPos.x;
                float dy = targetPos.y - casterPos.y;
                float dz = targetPos.z - casterPos.z;

                float distSq =
                    dx * dx +
                    dy * dy +
                    dz * dz;

                if (distSq > EMP_RADIUS_SQ)
                    continue;

                uint8_t stack =
                    ConsumeElectricShockStack(member.playerId);

                if (stack == 0)
                    continue;

                float stunSeconds =
                    GetStunDurationByStack(stack);

                ApplyElectricStun(
                    roomId,
                    member.playerId,
                    stunSeconds
                );

                hitTargets.push_back(member.playerId);

                std::cout
                    << "[EMP HIT] "
                    << "caster=" << casterPlayerId
                    << " target=" << member.playerId
                    << " stack=" << static_cast<int>(stack)
                    << " stun=" << stunSeconds
                    << std::endl;
            }

            BroadcastEmpCast(
                roomId,
                casterPlayerId,
                hitTargets
            );

            break;
        }
    
    case PacketType::PKT_CS_ULTIMATE_CAST:
    {
        auto* pkt =
            reinterpret_cast<CS_UltimateCast*>(header);

        if (header->size != sizeof(CS_UltimateCast))
        {
            std::cout
                << "[ULTIMATE BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[ULTIMATE BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[ULTIMATE BLOCK] player dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout
                << "[ULTIMATE BLOCK] stunned playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneBlockedAction(session, "ULTIMATE_CAST"))
            break;

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            break;

        RoomMember casterMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            casterMember))
        {
            break;
        }

        Vector3 casterPos =
            session->GetPosition();

        float dirX = pkt->dirX;
        float dirY = pkt->dirY;
        float dirZ = pkt->dirZ;

        float dirLen =
            std::sqrt(
                dirX * dirX +
                dirY * dirY +
                dirZ * dirZ
            );

        if (dirLen <= 0.0001f)
        {
            std::cout
                << "[ULTIMATE BLOCK] invalid direction playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        dirX /= dirLen;
        dirY /= dirLen;
        dirZ /= dirLen;

        std::vector<uint32_t> hitTargets;

        for (const RoomMember& member : room.members)
        {
            if (member.team == RoomTeam::OBSERVER)
                continue;

            if (member.playerId == session->GetPlayerId())
                continue;

            if (member.team == casterMember.team)
                continue;

            auto target =
                SessionManager::Instance().Find(
                    member.playerId
                );

            if (target == nullptr)
                continue;

            if (target->GetCurrentRoomId() != roomId)
                continue;

            if (target->IsDead())
                continue;

            Vector3 targetPos =
                target->GetPosition();

            float toX = targetPos.x - casterPos.x;
            float toY = targetPos.y - casterPos.y;
            float toZ = targetPos.z - casterPos.z;

            float distSq =
                toX * toX +
                toY * toY +
                toZ * toZ;

            if (distSq > ULTIMATE_RANGE_SQ)
                continue;

            float dist =
                std::sqrt(distSq);

            if (dist <= 0.0001f)
                continue;

            float ndX = toX / dist;
            float ndY = toY / dist;
            float ndZ = toZ / dist;

            float dot =
                ndX * dirX +
                ndY * dirY +
                ndZ * dirZ;

            if (dot < ULTIMATE_CONE_DOT)
                continue;

            target->SetLastDamager(
                session->GetPlayerId()
            );

            target->AddAssistDamageRecord(
                session->GetPlayerId(),
                ULTIMATE_DAMAGE
            );

            int appliedDamage = target->Damage(
                ULTIMATE_DAMAGE
            );
            session->GetMatchStats().damageDealt += appliedDamage;

            SC_HPUpdate hpPkt{};

            FillHPUpdatePacket(
                hpPkt,
                target.get()
            );

            BroadcastPacketToRoomInternal(
                roomId,
                &hpPkt
            );

            if (!target->IsDead())
            {
                AddElectricShockStack(
                    roomId,
                    target->GetPlayerId(),
                    ULTIMATE_STACK
                );
            }

            hitTargets.push_back(
                target->GetPlayerId()
            );

            std::cout
                << "[ULTIMATE HIT] caster="
                << session->GetPlayerId()
                << " target="
                << target->GetPlayerId()
                << " damage="
                << ULTIMATE_DAMAGE
                << " stack="
                << static_cast<int>(ULTIMATE_STACK)
                << " hp="
                << target->GetHP()
                << std::endl;

            if (target->IsDead())
            {
                HandleDeath(target.get());
            }
        }

        BroadcastUltimateCast(
            roomId,
            session->GetPlayerId(),
            dirX,
            dirY,
            dirZ,
            hitTargets,
            ULTIMATE_DAMAGE,
            ULTIMATE_STACK
        );

        break;
    }

    case PacketType::PKT_CS_BEAST_STEALTH_CAST:
    {
        auto* pkt =
            reinterpret_cast<CS_BeastStealthCast*>(header);

        if (header->size != sizeof(CS_BeastStealthCast))
            break;

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (session->IsDead())
            break;

        if (IsPlayerStunned(session->GetPlayerId()))
            break;

        if (IsDroneBlockedAction(session, "BEAST_STEALTH_CAST"))
            break;

        if (IsBeastStealthActive(session->GetPlayerId()))
            break;

        StartBeastStealth(session);

        break;
    }

    case PacketType::PKT_CS_BEAST_STEALTH_CANCEL:
    {
        auto* pkt =
            reinterpret_cast<CS_BeastStealthCancel*>(header);

        if (header->size != sizeof(CS_BeastStealthCancel))
            break;

        CancelBeastStealth(
            session->GetPlayerId(),
            "client_cancel"
        );

        break;
    }

    case PacketType::PKT_CS_ENTRY_SMOKE_THROW:
    {
        auto* pkt =
            reinterpret_cast<CS_EntrySmokeThrow*>(header);

        if (header->size != sizeof(CS_EntrySmokeThrow))
        {
            std::cout
                << "[ENTRY SMOKE THROW BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << " receivedSize="
                << header->size
                << " expectedSize="
                << sizeof(CS_EntrySmokeThrow)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[ENTRY SMOKE THROW BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[ENTRY SMOKE THROW BLOCK] dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout
                << "[ENTRY SMOKE THROW BLOCK] stunned playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneBlockedAction(session, "ENTRY_SMOKE_THROW"))
            break;

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout
                << "[ENTRY SMOKE THROW BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        CancelBeastStealth(
            session->GetPlayerId(),
            "entry_smoke_throw"
        );

        uint32_t smokeId = 0;

        {
            std::lock_guard<std::mutex> lock(g_entrySmokeMutex);

            smokeId =
                g_nextEntrySmokeId++;

            EntrySmokeState state{};

            state.smokeId =
                smokeId;

            state.ownerPlayerId =
                session->GetPlayerId();

            state.roomId =
                roomId;

            state.active =
                false;

            // fallback 활성화 위치.
            // STATE/ATTACH가 들어오면 이 값이 계속 갱신됨.
            state.x = pkt->x;
            state.y = pkt->y;
            state.z = pkt->z;

            state.nx = 0.0f;
            state.ny = 1.0f;
            state.nz = 0.0f;

            g_entrySmokes[smokeId] =
                state;
        }

        BroadcastEntrySmokeSpawn(
            roomId,
            smokeId,
            session->GetPlayerId(),
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->vx,
            pkt->vy,
            pkt->vz
        );

        StartEntrySmokeAutoActivateTimer(
            roomId,
            smokeId
        );

        std::cout
            << "[ENTRY SMOKE THROW APPLY] playerId="
            << session->GetPlayerId()
            << " smokeId="
            << smokeId
            << " pos=("
            << pkt->x << ", " << pkt->y << ", " << pkt->z
            << ") velocity=("
            << pkt->vx << ", " << pkt->vy << ", " << pkt->vz
            << ")"
            << std::endl;

        break;
    }
    case PacketType::PKT_CS_ENTRY_SMOKE_STATE:
    {
        auto* pkt =
            reinterpret_cast<CS_EntrySmokeState*>(header);

        if (header->size != sizeof(CS_EntrySmokeState))
        {
            std::cout
                << "[ENTRY SMOKE STATE BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << " receivedSize="
                << header->size
                << " expectedSize="
                << sizeof(CS_EntrySmokeState)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
            break;

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        uint32_t ownerPlayerId = 0;
        bool valid = false;

        {
            std::lock_guard<std::mutex> lock(g_entrySmokeMutex);

            auto it =
                g_entrySmokes.find(pkt->smokeId);

            if (it == g_entrySmokes.end())
                break;

            EntrySmokeState& state =
                it->second;

            if (state.ownerPlayerId != session->GetPlayerId())
                break;

            if (state.roomId != roomId)
                break;

            if (state.active)
                break;

            // fallback 위치 갱신
            state.x = pkt->x;
            state.y = pkt->y;
            state.z = pkt->z;

            ownerPlayerId =
                state.ownerPlayerId;

            valid =
                true;
        }

        if (!valid)
            break;

        BroadcastEntrySmokeState(
            roomId,
            pkt->smokeId,
            ownerPlayerId,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->rotX,
            pkt->rotY,
            pkt->rotZ,
            pkt->rotW
        );

        break;
    }
    case PacketType::PKT_CS_ENTRY_SMOKE_ATTACH:
    {
        auto* pkt =
            reinterpret_cast<CS_EntrySmokeAttach*>(header);

        if (header->size != sizeof(CS_EntrySmokeAttach))
        {
            std::cout
                << "[ENTRY SMOKE ATTACH BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << " receivedSize="
                << header->size
                << " expectedSize="
                << sizeof(CS_EntrySmokeAttach)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[ENTRY SMOKE ATTACH BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout
                << "[ENTRY SMOKE ATTACH BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        uint32_t ownerPlayerId = 0;
        bool valid = false;

        {
            std::lock_guard<std::mutex> lock(g_entrySmokeMutex);

            auto it =
                g_entrySmokes.find(pkt->smokeId);

            if (it == g_entrySmokes.end())
            {
                std::cout
                    << "[ENTRY SMOKE ATTACH BLOCK] smoke not found smokeId="
                    << pkt->smokeId
                    << std::endl;

                break;
            }

            EntrySmokeState& state =
                it->second;

            if (state.ownerPlayerId != session->GetPlayerId())
            {
                std::cout
                    << "[ENTRY SMOKE ATTACH BLOCK] not owner smokeId="
                    << pkt->smokeId
                    << " owner="
                    << state.ownerPlayerId
                    << " requester="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (state.roomId != roomId)
            {
                std::cout
                    << "[ENTRY SMOKE ATTACH BLOCK] room mismatch smokeId="
                    << pkt->smokeId
                    << " stateRoom="
                    << state.roomId
                    << " requesterRoom="
                    << roomId
                    << std::endl;

                break;
            }

            if (state.active)
            {
                std::cout
                    << "[ENTRY SMOKE ATTACH BLOCK] already active smokeId="
                    << pkt->smokeId
                    << std::endl;

                break;
            }

            state.x = pkt->x;
            state.y = pkt->y;
            state.z = pkt->z;

            state.nx = pkt->nx;
            state.ny = pkt->ny;
            state.nz = pkt->nz;

            state.active = true;

            ownerPlayerId =
                state.ownerPlayerId;

            valid =
                true;
        }

        if (!valid)
            break;

        BroadcastEntrySmokeActivate(
            roomId,
            pkt->smokeId,
            ownerPlayerId,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->nx,
            pkt->ny,
            pkt->nz,
            ENTRY_SMOKE_RADIUS,
            ENTRY_SMOKE_DURATION
        );

        std::cout
            << "[ENTRY SMOKE ATTACH APPLY] playerId="
            << session->GetPlayerId()
            << " smokeId="
            << pkt->smokeId
            << " pos=("
            << pkt->x << ", " << pkt->y << ", " << pkt->z
            << ") normal=("
            << pkt->nx << ", " << pkt->ny << ", " << pkt->nz
            << ")"
            << std::endl;

        std::thread(
            [roomId, smokeId = pkt->smokeId]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<int>(
                            ENTRY_SMOKE_DURATION * 1000.0f
                            )
                    )
                );

                bool shouldDestroy = false;

                {
                    std::lock_guard<std::mutex> lock(g_entrySmokeMutex);

                    auto it =
                        g_entrySmokes.find(smokeId);

                    if (it != g_entrySmokes.end())
                    {
                        g_entrySmokes.erase(it);
                        shouldDestroy = true;
                    }
                }

                if (shouldDestroy)
                {
                    BroadcastEntrySmokeDestroy(
                        roomId,
                        smokeId
                    );
                }
            }
        ).detach();

        break;
    }

    case PacketType::PKT_CS_ENTRY_DASH_CAST:
    {
        auto* pkt =
            reinterpret_cast<CS_EntryDashCast*>(header);

        if (header->size != sizeof(CS_EntryDashCast))
        {
            std::cout
                << "[ENTRY DASH BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << " receivedSize="
                << header->size
                << " expectedSize="
                << sizeof(CS_EntryDashCast)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[ENTRY DASH BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[ENTRY DASH BLOCK] dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout
                << "[ENTRY DASH BLOCK] stunned playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneBlockedAction(session, "ENTRY_DASH"))
            break;

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout
                << "[ENTRY DASH BLOCK] roomId is 0 playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        float dirX = pkt->dirX;
        float dirY = 0.0f;
        float dirZ = pkt->dirZ;

        float lenSq =
            dirX * dirX +
            dirY * dirY +
            dirZ * dirZ;

        if (lenSq <= 0.0001f)
        {
            std::cout
                << "[ENTRY DASH BLOCK] invalid dir playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        float invLen =
            1.0f / std::sqrt(lenSq);

        dirX *= invLen;
        dirY = 0.0f;
        dirZ *= invLen;

        // 돌진 사용 시 야수화 해제
        CancelBeastStealth(
            session->GetPlayerId(),
            "entry_dash"
        );

        BroadcastEntryDashCast(
            roomId,
            session->GetPlayerId(),
            dirX,
            dirY,
            dirZ,
            ENTRY_DASH_FORCE,
            ENTRY_DASH_DURATION
        );

        std::thread(
            [roomId, playerId = session->GetPlayerId()]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<int>(
                            ENTRY_DASH_DURATION * 1000.0f
                            )
                    )
                );

                BroadcastEntryDashEnd(
                    roomId,
                    playerId
                );
            }
        ).detach();

        std::cout
            << "[ENTRY DASH APPLY] playerId="
            << session->GetPlayerId()
            << " dir=("
            << dirX << ", " << dirY << ", " << dirZ
            << ")"
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ENTRY_BLADE_ACTIVATE:
    {
        if (header->size != sizeof(CS_EntryBladeActivate))
        {
            std::cout
                << "[ENTRY BLADE ACTIVATE BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << " receivedSize="
                << header->size
                << " expectedSize="
                << sizeof(CS_EntryBladeActivate)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[ENTRY BLADE ACTIVATE BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[ENTRY BLADE ACTIVATE BLOCK] dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout
                << "[ENTRY BLADE ACTIVATE BLOCK] stunned playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneBlockedAction(session, "ENTRY_BLADE_ACTIVATE"))
            break;

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        {
            std::lock_guard<std::mutex> lock(g_entryBladeMutex);

            auto it =
                g_entryBladeStates.find(session->GetPlayerId());

            if (it != g_entryBladeStates.end() &&
                it->second.active)
            {
                std::cout
                    << "[ENTRY BLADE ACTIVATE BLOCK] already active playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }
        }

        CancelBeastStealth(
            session->GetPlayerId(),
            "entry_blade_activate"
        );

        {
            std::lock_guard<std::mutex> lock(g_entryBladeMutex);

            EntryBladeState state{};

            state.playerId =
                session->GetPlayerId();

            state.roomId =
                roomId;

            state.active =
                true;

            state.bladeCount =
                ENTRY_BLADE_MAX_COUNT;

            state.endTime =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(
                    static_cast<int>(
                        ENTRY_BLADE_DURATION * 1000.0f
                        )
                );

            g_entryBladeStates[session->GetPlayerId()] =
                state;
        }

        BroadcastEntryBladeState(
            roomId,
            session->GetPlayerId(),
            true,
            ENTRY_BLADE_MAX_COUNT,
            ENTRY_BLADE_DURATION
        );

        std::thread(
            [roomId, playerId = session->GetPlayerId()]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<int>(
                            ENTRY_BLADE_DURATION * 1000.0f
                            )
                    )
                );

                EndEntryBladeUltimate(
                    roomId,
                    playerId,
                    "duration_end"
                );
            }
        ).detach();

        std::cout
            << "[ENTRY BLADE ACTIVATE APPLY] playerId="
            << session->GetPlayerId()
            << " bladeCount="
            << static_cast<int>(ENTRY_BLADE_MAX_COUNT)
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_ENTRY_BLADE_FIRE:
    {
        auto* pkt =
            reinterpret_cast<CS_EntryBladeFire*>(header);

        if (header->size != sizeof(CS_EntryBladeFire))
        {
            std::cout
                << "[ENTRY BLADE FIRE BLOCK] invalid size playerId="
                << session->GetPlayerId()
                << " receivedSize="
                << header->size
                << " expectedSize="
                << sizeof(CS_EntryBladeFire)
                << std::endl;

            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout
                << "[ENTRY BLADE FIRE BLOCK] not in game playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (session->IsDead())
        {
            std::cout
                << "[ENTRY BLADE FIRE BLOCK] dead playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout
                << "[ENTRY BLADE FIRE BLOCK] stunned playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        if (IsDroneBlockedAction(session, "ENTRY_BLADE_FIRE"))
            break;

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        uint8_t remainingBlades = 0;

        {
            std::lock_guard<std::mutex> lock(g_entryBladeMutex);

            auto it =
                g_entryBladeStates.find(session->GetPlayerId());

            if (it == g_entryBladeStates.end())
            {
                std::cout
                    << "[ENTRY BLADE FIRE BLOCK] ultimate not active playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            EntryBladeState& state =
                it->second;

            if (!state.active)
            {
                std::cout
                    << "[ENTRY BLADE FIRE BLOCK] inactive playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (state.roomId != roomId)
            {
                std::cout
                    << "[ENTRY BLADE FIRE BLOCK] room mismatch playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (state.bladeCount == 0)
            {
                std::cout
                    << "[ENTRY BLADE FIRE BLOCK] empty playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (std::chrono::steady_clock::now() > state.endTime)
            {
                g_entryBladeStates.erase(it);

                std::cout
                    << "[ENTRY BLADE FIRE BLOCK] expired playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            state.bladeCount--;

            remainingBlades =
                state.bladeCount;
        }

        float dirX = pkt->dirX;
        float dirY = pkt->dirY;
        float dirZ = pkt->dirZ;

        float lenSq =
            dirX * dirX +
            dirY * dirY +
            dirZ * dirZ;

        if (lenSq <= 0.0001f)
        {
            std::cout
                << "[ENTRY BLADE FIRE BLOCK] invalid dir playerId="
                << session->GetPlayerId()
                << std::endl;

            break;
        }

        float invLen =
            1.0f / std::sqrt(lenSq);

        dirX *= invLen;
        dirY *= invLen;
        dirZ *= invLen;

        Vector3 shooterPos =
            session->GetPosition();

        float shooterX =
            shooterPos.x;

        float shooterY =
            shooterPos.y;

        float shooterZ =
            shooterPos.z;

        float startX =
            shooterX;

        float startY =
            shooterY + 1.2f;

        float startZ =
            shooterZ;

        bool validHit = false;
        uint32_t finalTargetPlayerId = 0;

        float endX =
            startX + dirX * ENTRY_BLADE_RANGE;

        float endY =
            startY + dirY * ENTRY_BLADE_RANGE;

        float endZ =
            startZ + dirZ * ENTRY_BLADE_RANGE;

        if (pkt->targetPlayerId != 0)
        {
            auto target =
                SessionManager::Instance().Find(
                    pkt->targetPlayerId
                );

            if (target != nullptr &&
                target->GetState() == SessionState::IN_GAME &&
                !target->IsDead() &&
                target->GetCurrentRoomId() == roomId &&
                target->GetPlayerId() != session->GetPlayerId())
            {
                RoomMember shooterMember{};
                RoomMember targetMember{};

                bool shooterOk =
                    RoomManager::Instance().GetMember(
                        roomId,
                        session->GetPlayerId(),
                        shooterMember
                    );

                bool targetOk =
                    RoomManager::Instance().GetMember(
                        roomId,
                        target->GetPlayerId(),
                        targetMember
                    );

                if (shooterOk &&
                    targetOk &&
                    shooterMember.team != RoomTeam::OBSERVER &&
                    targetMember.team != RoomTeam::OBSERVER &&
                    shooterMember.team != targetMember.team)
                {
                    Vector3 targetPos =
                        target->GetPosition();

                    float toX =
                        targetPos.x - startX;

                    float toY =
                        (targetPos.y + 1.0f) - startY;

                    float toZ =
                        targetPos.z - startZ;

                    float distSq =
                        toX * toX +
                        toY * toY +
                        toZ * toZ;

                    if (distSq <= ENTRY_BLADE_RANGE_SQ)
                    {
                        float dist =
                            std::sqrt(distSq);

                        if (dist > 0.0001f)
                        {
                            float tx =
                                toX / dist;

                            float ty =
                                toY / dist;

                            float tz =
                                toZ / dist;

                            float dot =
                                tx * dirX +
                                ty * dirY +
                                tz * dirZ;

                            if (dot >= ENTRY_BLADE_HIT_DOT)
                            {
                                validHit =
                                    true;

                                finalTargetPlayerId =
                                    target->GetPlayerId();

                                endX =
                                    targetPos.x;

                                endY =
                                    targetPos.y + 1.0f;

                                endZ =
                                    targetPos.z;

                                target->SetLastDamager(
                                    session->GetPlayerId()
                                );

                                target->AddAssistDamageRecord(
                                    session->GetPlayerId(),
                                    ENTRY_BLADE_DAMAGE
                                );

                                int appliedDamage = target->Damage(
                                    ENTRY_BLADE_DAMAGE
                                );
                                session->GetMatchStats().damageDealt += appliedDamage;

                                SC_HPUpdate hpPkt{};

                                FillHPUpdatePacket(
                                    hpPkt,
                                    target.get()
                                );

                                BroadcastPacketToRoomInternal(
                                    roomId,
                                    &hpPkt
                                );

                                std::cout
                                    << "[ENTRY BLADE HIT] shooter="
                                    << session->GetPlayerId()
                                    << " target="
                                    << target->GetPlayerId()
                                    << " damage="
                                    << ENTRY_BLADE_DAMAGE
                                    << " hp="
                                    << target->GetHP()
                                    << std::endl;

                                if (target->IsDead())
                                {
                                    HandleDeath(target.get());
                                }
                            }
                        }
                    }
                }
            }
        }

        BroadcastEntryBladeFire(
            roomId,
            session->GetPlayerId(),
            finalTargetPlayerId,
            validHit,
            remainingBlades,
            startX,
            startY,
            startZ,
            endX,
            endY,
            endZ
        );

        if (remainingBlades == 0)
        {
            EndEntryBladeUltimate(
                roomId,
                session->GetPlayerId(),
                "empty"
            );
        }

        std::cout
            << "[ENTRY BLADE FIRE APPLY] shooter="
            << session->GetPlayerId()
            << " target="
            << finalTargetPlayerId
            << " hit="
            << validHit
            << " remaining="
            << static_cast<int>(remainingBlades)
            << std::endl;

        break;
    }

        case PacketType::PKT_CS_CONTROLLER_SWAMP_THROW:
        {
            auto* pkt =
                reinterpret_cast<CS_ControllerSwampThrow*>(header);

            if (header->size != sizeof(CS_ControllerSwampThrow))
            {
                std::cout
                    << "[CONTROLLER SWAMP THROW BLOCK] invalid size playerId="
                    << session->GetPlayerId()
                    << " receivedSize="
                    << header->size
                    << " expectedSize="
                    << sizeof(CS_ControllerSwampThrow)
                    << std::endl;

                break;
            }

            if (session->GetState() != SessionState::IN_GAME)
            {
                std::cout
                    << "[CONTROLLER SWAMP THROW BLOCK] not in game playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (session->IsDead())
            {
                std::cout
                    << "[CONTROLLER SWAMP THROW BLOCK] dead playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (IsPlayerStunned(session->GetPlayerId()))
            {
                std::cout
                    << "[CONTROLLER SWAMP THROW BLOCK] stunned playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (IsDroneBlockedAction(session, "CONTROLLER_SWAMP_THROW"))
                break;

            uint32_t roomId =
                session->GetCurrentRoomId();

            if (roomId == 0)
            {
                std::cout
                    << "[CONTROLLER SWAMP THROW BLOCK] roomId=0 playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            uint32_t trapId =
                g_nextControllerSwampTrapId.fetch_add(1);

            ControllerSwampTrapState state{};
            state.trapId = trapId;
            state.roomId = roomId;
            state.ownerPlayerId = session->GetPlayerId();
            state.activated = false;

            {
                std::lock_guard<std::mutex> lock(
                    g_controllerSwampMutex
                );

                g_controllerSwampTraps[trapId] = state;
            }

            BroadcastControllerSwampSpawn(
                roomId,
                trapId,
                session->GetPlayerId(),
                pkt->x,
                pkt->y,
                pkt->z,
                pkt->vx,
                pkt->vy,
                pkt->vz
            );

            std::cout
                << "[CONTROLLER SWAMP THROW APPLY] "
                << "playerId=" << session->GetPlayerId()
                << " trapId=" << trapId
                << " pos=("
                << pkt->x << ", " << pkt->y << ", " << pkt->z
                << ") velocity=("
                << pkt->vx << ", " << pkt->vy << ", " << pkt->vz
                << ")"
                << std::endl;

            break;
        }
        case PacketType::PKT_CS_CONTROLLER_SWAMP_ACTIVATE:
        {
            auto* pkt =
                reinterpret_cast<CS_ControllerSwampActivate*>(header);

            if (header->size != sizeof(CS_ControllerSwampActivate))
            {
                std::cout
                    << "[CONTROLLER SWAMP ACTIVATE BLOCK] invalid size playerId="
                    << session->GetPlayerId()
                    << " receivedSize="
                    << header->size
                    << " expectedSize="
                    << sizeof(CS_ControllerSwampActivate)
                    << std::endl;

                break;
            }

            if (session->GetState() != SessionState::IN_GAME)
            {
                std::cout
                    << "[CONTROLLER SWAMP ACTIVATE BLOCK] not in game playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            if (session->IsDead())
            {
                std::cout
                    << "[CONTROLLER SWAMP ACTIVATE BLOCK] dead playerId="
                    << session->GetPlayerId()
                    << std::endl;

                break;
            }

            uint32_t roomId = 0;
            uint32_t ownerPlayerId = 0;

            {
                std::lock_guard<std::mutex> lock(
                    g_controllerSwampMutex
                );

                auto it =
                    g_controllerSwampTraps.find(pkt->trapId);

                if (it == g_controllerSwampTraps.end())
                {
                    std::cout
                        << "[CONTROLLER SWAMP ACTIVATE BLOCK] trap not found trapId="
                        << pkt->trapId
                        << std::endl;

                    break;
                }

                ControllerSwampTrapState& state =
                    it->second;

                if (state.ownerPlayerId != session->GetPlayerId())
                {
                    std::cout
                        << "[CONTROLLER SWAMP ACTIVATE BLOCK] not owner trapId="
                        << pkt->trapId
                        << " requester=" << session->GetPlayerId()
                        << " owner=" << state.ownerPlayerId
                        << std::endl;

                    break;
                }

                if (state.activated)
                {
                    std::cout
                        << "[CONTROLLER SWAMP ACTIVATE BLOCK] already activated trapId="
                        << pkt->trapId
                        << std::endl;

                    break;
                }

                state.activated = true;

                roomId =
                    state.roomId;

                ownerPlayerId =
                    state.ownerPlayerId;
            }

            if (roomId == 0)
                break;

            BroadcastControllerSwampActivate(
                roomId,
                pkt->trapId,
                ownerPlayerId,
                pkt->x,
                pkt->y,
                pkt->z,
                pkt->normalX,
                pkt->normalY,
                pkt->normalZ
            );

            std::thread(
                [roomId, trapId = pkt->trapId]()
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(
                            static_cast<int>(
                                CONTROLLER_SWAMP_DURATION * 1000.0f
                                )
                        )
                    );

                    bool shouldDestroy = false;

                    {
                        std::lock_guard<std::mutex> lock(
                            g_controllerSwampMutex
                        );

                        auto it =
                            g_controllerSwampTraps.find(trapId);

                        if (it != g_controllerSwampTraps.end())
                        {
                            g_controllerSwampTraps.erase(it);
                            shouldDestroy = true;
                        }
                    }

                    if (shouldDestroy)
                    {
                        BroadcastControllerSwampDestroy(
                            roomId,
                            trapId
                        );
                    }
                }
            ).detach();

            std::cout
                << "[CONTROLLER SWAMP ACTIVATE APPLY] "
                << "trapId=" << pkt->trapId
                << " owner=" << ownerPlayerId
                << " pos=("
                << pkt->x << ", " << pkt->y << ", " << pkt->z
                << ") normal=("
                << pkt->normalX << ", " << pkt->normalY << ", " << pkt->normalZ
                << ")"
                << std::endl;

            break;
        }
    case PacketType::PKT_CS_CONTROLLER_SMOKE_DROP:
    {
        auto* pkt = reinterpret_cast<CS_ControllerSmokeDrop*>(header);

        if (header->size != sizeof(CS_ControllerSmokeDrop))
        {
            std::cout << "[CONTROLLER SMOKE DROP BLOCK] invalid size playerId=" << session->GetPlayerId() << " receivedSize=" << header->size << " expectedSize=" << sizeof(CS_ControllerSmokeDrop) << std::endl;
            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[CONTROLLER SMOKE DROP BLOCK] not in game playerId=" << session->GetPlayerId() << std::endl;
            break;
        }

        if (session->IsDead())
        {
            std::cout << "[CONTROLLER SMOKE DROP BLOCK] dead playerId=" << session->GetPlayerId() << std::endl;
            break;
        }

        if (IsPlayerStunned(session->GetPlayerId()))
        {
            std::cout << "[CONTROLLER SMOKE DROP BLOCK] stunned playerId=" << session->GetPlayerId() << std::endl;
            break;
        }

        if (IsDroneBlockedAction(session, "CONTROLLER_SMOKE_DROP"))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
        {
            std::cout << "[CONTROLLER SMOKE DROP BLOCK] roomId=0 playerId=" << session->GetPlayerId() << std::endl;
            break;
        }

        uint32_t smokeId = g_nextControllerSmokeId.fetch_add(1);

        ControllerSmokeState state{};
        state.smokeId = smokeId;
        state.roomId = roomId;
        state.ownerPlayerId = session->GetPlayerId();

        {
            std::lock_guard<std::mutex> lock(g_controllerSmokeMutex);
            g_controllerSmokes[smokeId] = state;
        }

        BroadcastControllerSmokeSpawn(roomId, smokeId, session->GetPlayerId(), pkt->x, pkt->y + 0.05f, pkt->z);

        std::thread([roomId, smokeId]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(CONTROLLER_SMOKE_DURATION * 1000.0f)));

                bool removed = false;

                {
                    std::lock_guard<std::mutex> lock(g_controllerSmokeMutex);

                    auto it = g_controllerSmokes.find(smokeId);

                    if (it != g_controllerSmokes.end())
                    {
                        g_controllerSmokes.erase(it);
                        removed = true;
                    }
                }

                if (removed)
                {
                    BroadcastControllerSmokeDestroy(roomId, smokeId);
                }
            }).detach();

        std::cout << "[CONTROLLER SMOKE DROP APPLY] playerId=" << session->GetPlayerId() << " smokeId=" << smokeId << " pos=(" << pkt->x << "," << pkt->y << "," << pkt->z << ")" << std::endl;

        break;
    }

    case PacketType::PKT_CS_CONTROLLER_DECOY_THROW:
    {
        if (header->size != sizeof(CS_ControllerDecoyThrow))
        {
            std::cout
                << "[CONTROLLER DECOY THROW INVALID SIZE] size="
                << header->size
                << " expected="
                << sizeof(CS_ControllerDecoyThrow)
                << std::endl;

            break;
        }

        auto* pkt =
            reinterpret_cast<CS_ControllerDecoyThrow*>(header);

        uint32_t playerId =
            session->GetPlayerId();

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (playerId == 0 || roomId == 0)
        {
            std::cout
                << "[CONTROLLER DECOY THROW FAIL] invalid player or room"
                << " playerId=" << playerId
                << " roomId=" << roomId
                << std::endl;

            break;
        }

        uint32_t decoyId =
            g_nextControllerDecoyId.fetch_add(1);

        {
            std::lock_guard<std::mutex> lock(g_controllerDecoyMutex);

            ControllerDecoyState state{};
            state.decoyId = decoyId;
            state.roomId = roomId;
            state.ownerPlayerId = playerId;
            state.activated = false;

            g_controllerDecoys[decoyId] = state;
        }

        BroadcastControllerDecoySpawn(
            roomId,
            decoyId,
            playerId,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->vx,
            pkt->vy,
            pkt->vz
        );

        std::cout
            << "[CONTROLLER DECOY THROW APPLY] playerId=" << playerId
            << " decoyId=" << decoyId
            << " pos=(" << pkt->x << ", " << pkt->y << ", " << pkt->z << ")"
            << " velocity=(" << pkt->vx << ", " << pkt->vy << ", " << pkt->vz << ")"
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_CONTROLLER_DECOY_ACTIVATE:
    {
        if (header->size != sizeof(CS_ControllerDecoyActivate))
        {
            std::cout
                << "[CONTROLLER DECOY ACTIVATE INVALID SIZE] size="
                << header->size
                << " expected="
                << sizeof(CS_ControllerDecoyActivate)
                << std::endl;

            break;
        }

        auto* pkt =
            reinterpret_cast<CS_ControllerDecoyActivate*>(header);

        uint32_t playerId =
            session->GetPlayerId();

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (playerId == 0 || roomId == 0)
        {
            std::cout
                << "[CONTROLLER DECOY ACTIVATE FAIL] invalid player or room"
                << " playerId=" << playerId
                << " roomId=" << roomId
                << std::endl;

            break;
        }

        ControllerDecoyState state{};

        {
            std::lock_guard<std::mutex> lock(g_controllerDecoyMutex);

            auto it =
                g_controllerDecoys.find(pkt->decoyId);

            if (it == g_controllerDecoys.end())
            {
                std::cout
                    << "[CONTROLLER DECOY ACTIVATE FAIL] decoy not found"
                    << " decoyId=" << pkt->decoyId
                    << std::endl;

                break;
            }

            if (it->second.roomId != roomId)
            {
                std::cout
                    << "[CONTROLLER DECOY ACTIVATE FAIL] room mismatch"
                    << " decoyId=" << pkt->decoyId
                    << " stateRoom=" << it->second.roomId
                    << " sessionRoom=" << roomId
                    << std::endl;

                break;
            }

            if (it->second.ownerPlayerId != playerId)
            {
                std::cout
                    << "[CONTROLLER DECOY ACTIVATE FAIL] owner mismatch"
                    << " decoyId=" << pkt->decoyId
                    << " owner=" << it->second.ownerPlayerId
                    << " requester=" << playerId
                    << std::endl;

                break;
            }

            if (it->second.activated)
            {
                std::cout
                    << "[CONTROLLER DECOY ACTIVATE IGNORE] already activated"
                    << " decoyId=" << pkt->decoyId
                    << std::endl;

                break;
            }

            it->second.activated = true;
            state = it->second;
        }

        BroadcastControllerDecoyActivate(
            roomId,
            pkt->decoyId,
            state.ownerPlayerId,
            pkt->x,
            pkt->y,
            pkt->z,
            pkt->normalX,
            pkt->normalY,
            pkt->normalZ,
            CONTROLLER_DECOY_DURATION
        );

        ScheduleControllerDecoyDestroy(
            roomId,
            pkt->decoyId,
            CONTROLLER_DECOY_DURATION
        );

        std::cout
            << "[CONTROLLER DECOY ACTIVATE APPLY] decoyId=" << pkt->decoyId
            << " owner=" << state.ownerPlayerId
            << " pos=(" << pkt->x << ", " << pkt->y << ", " << pkt->z << ")"
            << " normal=(" << pkt->normalX << ", " << pkt->normalY << ", " << pkt->normalZ << ")"
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_CONTROLLER_ULTIMATE_CAST:
    {
        if (header->size != sizeof(CS_ControllerUltimateCast))
        {
            std::cout
                << "[CONTROLLER ULTIMATE CAST INVALID SIZE] size="
                << header->size
                << " expected="
                << sizeof(CS_ControllerUltimateCast)
                << std::endl;

            break;
        }

        auto* pkt =
            reinterpret_cast<CS_ControllerUltimateCast*>(header);

        uint32_t playerId =
            session->GetPlayerId();

        uint32_t roomId =
            session->GetCurrentRoomId();

        if (playerId == 0 || roomId == 0)
        {
            std::cout
                << "[CONTROLLER ULTIMATE CAST FAIL] invalid player or room"
                << " playerId=" << playerId
                << " roomId=" << roomId
                << std::endl;

            break;
        }

        uint32_t ultimateId =
            g_nextControllerUltimateId.fetch_add(1);

        {
            std::lock_guard<std::mutex> lock(g_controllerUltimateMutex);

            ControllerUltimateState state{};
            state.ultimateId = ultimateId;
            state.roomId = roomId;
            state.ownerPlayerId = playerId;

            g_controllerUltimates[ultimateId] = state;
        }

        BroadcastControllerUltimateSpawn(
            roomId,
            ultimateId,
            playerId,
            pkt->x,
            pkt->y,
            pkt->z,
            CONTROLLER_ULTIMATE_RADIUS,
            CONTROLLER_ULTIMATE_DURATION
        );

        ScheduleControllerUltimateDestroy(
            roomId,
            ultimateId,
            CONTROLLER_ULTIMATE_DURATION
        );

        std::cout
            << "[CONTROLLER ULTIMATE CAST APPLY] playerId=" << playerId
            << " ultimateId=" << ultimateId
            << " pos=(" << pkt->x << ", " << pkt->y << ", " << pkt->z << ")"
            << " radius=" << CONTROLLER_ULTIMATE_RADIUS
            << " duration=" << CONTROLLER_ULTIMATE_DURATION
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SENTINEL_C_THROW:
    {
        auto* pkt = reinterpret_cast<CS_SentinelCThrow*>(header);

        if (header->size != sizeof(CS_SentinelCThrow))
        {
            std::cout << "[SENTINEL C THROW BLOCK] invalid size" << std::endl;
            break;
        }

        if (session->GetState() != SessionState::IN_GAME)
        {
            std::cout << "[SENTINEL C THROW BLOCK] not in game playerId="
                << session->GetPlayerId() << std::endl;
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        RoomMember ownerMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            ownerMember))
        {
            std::cout << "[SENTINEL C THROW BLOCK] owner member not found" << std::endl;
            break;
        }

        uint32_t trapId = CreateSentinelCTrapId();
        uint8_t ownerTeam = static_cast<uint8_t>(ownerMember.team);

        {
            std::lock_guard<std::mutex> lock(g_sentinelCMutex);

            SentinelCTrapRuntime trap{};
            trap.trapId = trapId;
            trap.roomId = roomId;
            trap.ownerPlayerId = session->GetPlayerId();
            trap.ownerTeam = ownerTeam;
            trap.x = pkt->x;
            trap.y = pkt->y;
            trap.z = pkt->z;
            trap.hp = SENTINEL_C_MAX_HP;
            trap.destroyed = false;

            g_sentinelCTraps[trapId] = trap;
        }

        SC_SentinelCSpawn out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SENTINEL_C_SPAWN);

        out.trapId = trapId;
        out.ownerPlayerId = session->GetPlayerId();
        out.ownerTeam = ownerTeam;

        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;

        out.vx = pkt->vx;
        out.vy = pkt->vy;
        out.vz = pkt->vz;

        out.hp = SENTINEL_C_MAX_HP;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SENTINEL C SPAWN] trapId="
            << trapId
            << " owner=" << session->GetPlayerId()
            << " roomId=" << roomId
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SENTINEL_C_ANCHOR:
    {
        auto* pkt = reinterpret_cast<CS_SentinelCAnchor*>(header);

        if (header->size != sizeof(CS_SentinelCAnchor))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        {
            std::lock_guard<std::mutex> lock(g_sentinelCMutex);

            auto it = g_sentinelCTraps.find(pkt->trapId);

            if (it == g_sentinelCTraps.end())
                break;

            SentinelCTrapRuntime& trap = it->second;

            if (trap.destroyed)
                break;

            if (trap.roomId != roomId)
                break;

            if (trap.ownerPlayerId != session->GetPlayerId())
                break;

            trap.x = pkt->x;
            trap.y = pkt->y;
            trap.z = pkt->z;
        }

        SC_SentinelCAnchor out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SENTINEL_C_ANCHOR);

        out.trapId = pkt->trapId;

        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;

        out.nx = pkt->nx;
        out.ny = pkt->ny;
        out.nz = pkt->nz;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SENTINEL C ANCHOR] trapId="
            << pkt->trapId << std::endl;

        break;
    }

    case PacketType::PKT_CS_SENTINEL_C_TRIGGER:
    {
        auto* pkt = reinterpret_cast<CS_SentinelCTrigger*>(header);

        if (header->size != sizeof(CS_SentinelCTrigger))
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        SentinelCTrapRuntime trapCopy{};

        {
            std::lock_guard<std::mutex> lock(g_sentinelCMutex);

            auto it = g_sentinelCTraps.find(pkt->trapId);

            if (it == g_sentinelCTraps.end())
                break;

            SentinelCTrapRuntime& trap = it->second;

            if (trap.destroyed)
                break;

            if (trap.roomId != roomId)
                break;

            if (trap.ownerPlayerId != session->GetPlayerId())
                break;

            trapCopy = trap;
        }

        SC_SentinelCTrigger out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_SENTINEL_C_TRIGGER);

        out.trapId = pkt->trapId;
        out.ownerPlayerId = trapCopy.ownerPlayerId;
        out.ownerTeam = trapCopy.ownerTeam;

        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;

        out.radius = pkt->radius;
        out.revealDuration = pkt->revealDuration;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout << "[SENTINEL C TRIGGER] trapId="
            << pkt->trapId << std::endl;

        break;
    }

    case PacketType::PKT_CS_SENTINEL_C_HIT:
    {
        auto* pkt = reinterpret_cast<CS_SentinelCHit*>(header);

        if (header->size != sizeof(CS_SentinelCHit))
            break;

        if (session->GetState() != SessionState::IN_GAME)
            break;

        if (session->IsDead())
            break;

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
            break;

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
            break;

        uint32_t attackerPlayerId = session->GetPlayerId();

        RoomMember attackerMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            attackerPlayerId,
            attackerMember))
        {
            break;
        }

        bool destroyed = false;

        {
            std::lock_guard<std::mutex> lock(g_sentinelCMutex);

            auto it = g_sentinelCTraps.find(pkt->trapId);

            if (it == g_sentinelCTraps.end())
                break;

            SentinelCTrapRuntime& trap = it->second;

            if (trap.destroyed)
                break;

            if (trap.roomId != roomId)
                break;

            if (trap.ownerPlayerId == attackerPlayerId)
                break;

            if (static_cast<uint8_t>(attackerMember.team) == trap.ownerTeam)
                break;

            if (IsSentinelSkillProtected(
                roomId,
                trap.ownerPlayerId))
            {
                std::cout
                    << "[SENTINEL C HIT BLOCK] protected by X trapId="
                    << pkt->trapId
                    << " owner=" << trap.ownerPlayerId
                    << std::endl;
                break;
            }

            trap.hp -= static_cast<float>(weaponData->damage);

            std::cout << "[SENTINEL C HIT] trapId="
                << pkt->trapId
                << " attacker=" << attackerPlayerId
                << " weaponId=" << weaponId
                << " damage=" << weaponData->damage
                << " hp=" << trap.hp
                << std::endl;

            if (trap.hp <= 0.f)
            {
                destroyed = true;
                g_sentinelCTraps.erase(it);
            }
        }

        if (destroyed)
        {
            BroadcastSentinelCDestroy(roomId, pkt->trapId);

            std::cout << "[SENTINEL C DESTROY] trapId="
                << pkt->trapId << std::endl;
        }

        break;
    }

    case PacketType::PKT_CS_SENTINEL_Q_PLACE:
    {
        auto* pkt = reinterpret_cast<CS_SentinelQPlace*>(header);

        if (header->size != sizeof(CS_SentinelQPlace))
            break;

        if (session->GetState() != SessionState::IN_GAME ||
            session->IsDead())
        {
            break;
        }

        if (!std::isfinite(pkt->x) ||
            !std::isfinite(pkt->y) ||
            !std::isfinite(pkt->z))
        {
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        RoomMember ownerMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            ownerMember))
        {
            break;
        }

        std::vector<uint32_t> replacedTrapIds;

        {
            std::lock_guard<std::mutex> lock(g_sentinelQMutex);

            for (auto it = g_sentinelQTraps.begin();
                it != g_sentinelQTraps.end();)
            {
                if (it->second.roomId == roomId &&
                    it->second.ownerPlayerId == session->GetPlayerId())
                {
                    replacedTrapIds.push_back(it->first);
                    it = g_sentinelQTraps.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (uint32_t replacedTrapId : replacedTrapIds)
        {
            BroadcastSentinelQDestroy(roomId, replacedTrapId);
        }

        uint32_t trapId = CreateSentinelQTrapId();
        uint8_t ownerTeam = static_cast<uint8_t>(ownerMember.team);

        {
            std::lock_guard<std::mutex> lock(g_sentinelQMutex);

            SentinelQTrapRuntime trap{};
            trap.trapId = trapId;
            trap.roomId = roomId;
            trap.ownerPlayerId = session->GetPlayerId();
            trap.ownerTeam = ownerTeam;
            trap.x = pkt->x;
            trap.y = pkt->y;
            trap.z = pkt->z;
            trap.hp = SENTINEL_Q_MAX_HP;
            trap.activated = false;

            g_sentinelQTraps[trapId] = trap;
        }

        SC_SentinelQSpawn out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_Q_SPAWN
            );

        out.trapId = trapId;
        out.ownerPlayerId = session->GetPlayerId();
        out.ownerTeam = ownerTeam;
        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;
        out.hp = SENTINEL_Q_MAX_HP;
        out.detectionRadius = SENTINEL_Q_DETECTION_RADIUS;
        out.zoneRadius = SENTINEL_Q_ZONE_RADIUS;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SENTINEL Q PLACE] trapId=" << trapId
            << " owner=" << session->GetPlayerId()
            << " roomId=" << roomId
            << " pos=(" << pkt->x << ", " << pkt->y << ", " << pkt->z << ")"
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SENTINEL_Q_ACTIVATE:
    {
        auto* pkt = reinterpret_cast<CS_SentinelQActivate*>(header);

        if (header->size != sizeof(CS_SentinelQActivate))
            break;

        if (session->GetState() != SessionState::IN_GAME ||
            session->IsDead())
        {
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        bool activated = false;

        {
            std::lock_guard<std::mutex> lock(g_sentinelQMutex);

            auto it = g_sentinelQTraps.find(pkt->trapId);

            if (it == g_sentinelQTraps.end())
                break;

            SentinelQTrapRuntime& trap = it->second;

            if (trap.roomId != roomId ||
                trap.ownerPlayerId != session->GetPlayerId() ||
                trap.activated)
            {
                break;
            }

            trap.activated = true;
            activated = true;
        }

        if (!activated)
            break;

        SC_SentinelQActivate out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_Q_ACTIVATE
            );
        out.trapId = pkt->trapId;
        out.duration = SENTINEL_Q_ZONE_DURATION;
        out.slowMultiplier = SENTINEL_Q_SLOW_MULTIPLIER;

        BroadcastPacketToRoomInternal(roomId, &out);

        uint32_t activatedTrapId = pkt->trapId;

        std::thread(
            [roomId, activatedTrapId]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<int>(
                            SENTINEL_Q_ZONE_DURATION * 1000.f
                        )
                    )
                );

                bool shouldDestroy = false;

                {
                    std::lock_guard<std::mutex> lock(g_sentinelQMutex);

                    auto it = g_sentinelQTraps.find(activatedTrapId);

                    if (it != g_sentinelQTraps.end() &&
                        it->second.roomId == roomId)
                    {
                        g_sentinelQTraps.erase(it);
                        shouldDestroy = true;
                    }
                }

                if (shouldDestroy)
                {
                    BroadcastSentinelQDestroy(
                        roomId,
                        activatedTrapId
                    );
                }
            }
        ).detach();

        std::cout
            << "[SENTINEL Q ACTIVATE] trapId=" << pkt->trapId
            << " owner=" << session->GetPlayerId()
            << " duration=" << SENTINEL_Q_ZONE_DURATION
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SENTINEL_Q_HIT:
    {
        auto* pkt = reinterpret_cast<CS_SentinelQHit*>(header);

        if (header->size != sizeof(CS_SentinelQHit))
            break;

        if (session->GetState() != SessionState::IN_GAME ||
            session->IsDead())
        {
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
            break;

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
            break;

        uint32_t attackerPlayerId = session->GetPlayerId();
        RoomMember attackerMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            attackerPlayerId,
            attackerMember))
        {
            break;
        }

        bool destroyed = false;

        {
            std::lock_guard<std::mutex> lock(g_sentinelQMutex);

            auto it = g_sentinelQTraps.find(pkt->trapId);

            if (it == g_sentinelQTraps.end())
                break;

            SentinelQTrapRuntime& trap = it->second;

            if (trap.roomId != roomId ||
                trap.ownerPlayerId == attackerPlayerId ||
                static_cast<uint8_t>(attackerMember.team) == trap.ownerTeam)
            {
                break;
            }

            if (IsSentinelSkillProtected(
                roomId,
                trap.ownerPlayerId))
            {
                std::cout
                    << "[SENTINEL Q HIT BLOCK] protected by X trapId="
                    << pkt->trapId
                    << " owner=" << trap.ownerPlayerId
                    << std::endl;
                break;
            }

            trap.hp -= static_cast<float>(weaponData->damage);

            std::cout
                << "[SENTINEL Q HIT] trapId=" << pkt->trapId
                << " attacker=" << attackerPlayerId
                << " weaponId=" << weaponId
                << " damage=" << weaponData->damage
                << " hp=" << trap.hp
                << std::endl;

            if (trap.hp <= 0.f)
            {
                g_sentinelQTraps.erase(it);
                destroyed = true;
            }
        }

        if (destroyed)
            BroadcastSentinelQDestroy(roomId, pkt->trapId);

        break;
    }

    case PacketType::PKT_CS_SENTINEL_E_PLACE:
    {
        auto* pkt = reinterpret_cast<CS_SentinelEPlace*>(header);

        if (header->size != sizeof(CS_SentinelEPlace))
            break;

        if (session->GetState() != SessionState::IN_GAME ||
            session->IsDead())
        {
            break;
        }

        if (!std::isfinite(pkt->x) ||
            !std::isfinite(pkt->y) ||
            !std::isfinite(pkt->z) ||
            !std::isfinite(pkt->nx) ||
            !std::isfinite(pkt->ny) ||
            !std::isfinite(pkt->nz))
        {
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        RoomMember ownerMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            ownerMember))
        {
            break;
        }

        Vector3 ownerPosition = session->GetPosition();
        float dx = pkt->x - ownerPosition.x;
        float dy = pkt->y - ownerPosition.y;
        float dz = pkt->z - ownerPosition.z;
        float placeDistanceSquared =
            dx * dx + dy * dy + dz * dz;

        if (placeDistanceSquared >
            (SENTINEL_E_PLACE_RANGE + 0.75f) *
            (SENTINEL_E_PLACE_RANGE + 0.75f))
        {
            std::cout
                << "[SENTINEL E PLACE BLOCK] out of range playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        float normalLength = std::sqrt(
            pkt->nx * pkt->nx +
            pkt->ny * pkt->ny +
            pkt->nz * pkt->nz
        );

        if (normalLength < 0.5f)
            break;

        float nx = pkt->nx / normalLength;
        float ny = pkt->ny / normalLength;
        float nz = pkt->nz / normalLength;

        if (std::abs(ny) > 0.6f)
        {
            std::cout
                << "[SENTINEL E PLACE BLOCK] not a wall playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        uint32_t trapId = 0;
        uint8_t ownerTeam =
            static_cast<uint8_t>(ownerMember.team);
        uint64_t cooldownKey =
            MakeSentinelECooldownKey(
                roomId,
                session->GetPlayerId()
            );
        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(g_sentinelEMutex);

            bool alreadyPlaced = false;

            for (const auto& pair : g_sentinelETraps)
            {
                const SentinelETrapRuntime& existing =
                    pair.second;

                if (existing.roomId == roomId &&
                    existing.ownerPlayerId ==
                    session->GetPlayerId())
                {
                    alreadyPlaced = true;
                    break;
                }
            }

            if (alreadyPlaced)
                break;

            auto cooldownIt =
                g_sentinelECooldowns.find(cooldownKey);

            if (cooldownIt != g_sentinelECooldowns.end())
            {
                if (now < cooldownIt->second)
                    break;

                g_sentinelECooldowns.erase(cooldownIt);
            }

            trapId = g_nextSentinelETrapId++;

            SentinelETrapRuntime trap{};
            trap.trapId = trapId;
            trap.roomId = roomId;
            trap.ownerPlayerId = session->GetPlayerId();
            trap.ownerTeam = ownerTeam;
            trap.x = pkt->x;
            trap.y = pkt->y;
            trap.z = pkt->z;
            trap.nx = nx;
            trap.ny = ny;
            trap.nz = nz;
            trap.hp = SENTINEL_E_MAX_HP;

            g_sentinelETraps[trapId] = trap;
        }

        if (trapId == 0)
            break;

        SC_SentinelESpawn out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_E_SPAWN
            );
        out.trapId = trapId;
        out.ownerPlayerId = session->GetPlayerId();
        out.ownerTeam = ownerTeam;
        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;
        out.nx = nx;
        out.ny = ny;
        out.nz = nz;
        out.hp = SENTINEL_E_MAX_HP;
        out.radius = SENTINEL_E_EFFECT_RADIUS;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SENTINEL E PLACE] trapId=" << trapId
            << " owner=" << session->GetPlayerId()
            << " roomId=" << roomId
            << " pos=(" << pkt->x << ", "
            << pkt->y << ", " << pkt->z << ")"
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SENTINEL_E_ACTIVATE:
    {
        auto* pkt =
            reinterpret_cast<CS_SentinelEActivate*>(header);

        if (header->size != sizeof(CS_SentinelEActivate))
            break;

        if (session->GetState() != SessionState::IN_GAME ||
            session->IsDead())
        {
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        SentinelETrapRuntime trapCopy{};
        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(g_sentinelEMutex);

            auto it = g_sentinelETraps.find(pkt->trapId);

            if (it == g_sentinelETraps.end())
                break;

            const SentinelETrapRuntime& trap = it->second;

            if (trap.roomId != roomId ||
                trap.ownerPlayerId != session->GetPlayerId())
            {
                break;
            }

            if (trap.lastActivated !=
                std::chrono::steady_clock::time_point{})
            {
                float elapsed =
                    std::chrono::duration<float>(
                        now - trap.lastActivated
                    ).count();

                if (elapsed < SENTINEL_E_REARM_SECONDS)
                    break;
            }

            trapCopy = trap;
        }

        Room room{};

        if (!RoomManager::Instance().GetRoom(roomId, room))
            break;

        bool enemyInRange = false;
        float radiusSquared =
            SENTINEL_E_EFFECT_RADIUS *
            SENTINEL_E_EFFECT_RADIUS;

        for (const RoomMember& member : room.members)
        {
            if (member.playerId == trapCopy.ownerPlayerId ||
                static_cast<uint8_t>(member.team) ==
                trapCopy.ownerTeam ||
                member.team == RoomTeam::OBSERVER)
            {
                continue;
            }

            auto target =
                SessionManager::Instance().Find(member.playerId);

            if (target == nullptr ||
                target->GetState() != SessionState::IN_GAME ||
                target->IsDead())
            {
                continue;
            }

            Vector3 targetPosition = target->GetPosition();
            float targetDx = targetPosition.x - trapCopy.x;
            float targetDy = targetPosition.y - trapCopy.y;
            float targetDz = targetPosition.z - trapCopy.z;
            float distanceSquared =
                targetDx * targetDx +
                targetDy * targetDy +
                targetDz * targetDz;

            if (distanceSquared <= radiusSquared)
            {
                enemyInRange = true;
                break;
            }
        }

        if (!enemyInRange)
        {
            std::cout
                << "[SENTINEL E ACTIVATE BLOCK] no enemy trapId="
                << pkt->trapId
                << std::endl;
            break;
        }

        {
            std::lock_guard<std::mutex> lock(g_sentinelEMutex);

            auto it = g_sentinelETraps.find(pkt->trapId);

            if (it == g_sentinelETraps.end() ||
                it->second.roomId != roomId ||
                it->second.ownerPlayerId !=
                session->GetPlayerId())
            {
                break;
            }

            it->second.lastActivated = now;
        }

        SC_SentinelEActivate out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_E_ACTIVATE
            );
        out.trapId = pkt->trapId;
        out.radius = SENTINEL_E_EFFECT_RADIUS;
        out.slowDuration = SENTINEL_E_SLOW_DURATION;
        out.slowMultiplier = SENTINEL_E_SLOW_MULTIPLIER;
        out.stunDuration = SENTINEL_E_STUN_DURATION;

        BroadcastPacketToRoomInternal(roomId, &out);

        std::cout
            << "[SENTINEL E ACTIVATE] trapId=" << pkt->trapId
            << " owner=" << session->GetPlayerId()
            << std::endl;

        break;
    }

    case PacketType::PKT_CS_SENTINEL_E_HIT:
    {
        auto* pkt = reinterpret_cast<CS_SentinelEHit*>(header);

        if (header->size != sizeof(CS_SentinelEHit))
            break;

        if (session->GetState() != SessionState::IN_GAME ||
            session->IsDead())
        {
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        int weaponId = session->GetCurrentWeaponId();

        if (!session->HasWeapon(weaponId))
            break;

        const ServerWeaponData* weaponData =
            ServerWeaponTable::Find(weaponId);

        if (weaponData == nullptr)
            break;

        uint32_t attackerPlayerId = session->GetPlayerId();
        RoomMember attackerMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            attackerPlayerId,
            attackerMember))
        {
            break;
        }

        bool destroyed = false;
        uint32_t ownerPlayerId = 0;

        {
            std::lock_guard<std::mutex> lock(g_sentinelEMutex);

            auto it = g_sentinelETraps.find(pkt->trapId);

            if (it == g_sentinelETraps.end())
                break;

            SentinelETrapRuntime& trap = it->second;

            if (trap.roomId != roomId ||
                trap.ownerPlayerId == attackerPlayerId ||
                static_cast<uint8_t>(attackerMember.team) ==
                trap.ownerTeam)
            {
                break;
            }

            if (IsSentinelSkillProtected(
                roomId,
                trap.ownerPlayerId))
            {
                std::cout
                    << "[SENTINEL E HIT BLOCK] protected by X trapId="
                    << pkt->trapId
                    << " owner=" << trap.ownerPlayerId
                    << std::endl;
                break;
            }

            trap.hp -= static_cast<float>(weaponData->damage);

            std::cout
                << "[SENTINEL E HIT] trapId=" << pkt->trapId
                << " attacker=" << attackerPlayerId
                << " weaponId=" << weaponId
                << " damage=" << weaponData->damage
                << " hp=" << trap.hp
                << std::endl;

            if (trap.hp <= 0.f)
            {
                ownerPlayerId = trap.ownerPlayerId;
                g_sentinelETraps.erase(it);

                uint64_t cooldownKey =
                    MakeSentinelECooldownKey(
                        roomId,
                        ownerPlayerId
                    );

                g_sentinelECooldowns[cooldownKey] =
                    std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(
                        static_cast<int>(
                            SENTINEL_E_DESTROY_COOLDOWN *
                            1000.f
                        )
                    );

                destroyed = true;
            }
        }

        if (destroyed)
        {
            BroadcastSentinelEDestroy(
                roomId,
                pkt->trapId,
                SENTINEL_E_REMOVE_DESTROYED,
                SENTINEL_E_DESTROY_COOLDOWN
            );
        }

        break;
    }

    case PacketType::PKT_CS_SENTINEL_E_RECALL:
    {
        auto* pkt =
            reinterpret_cast<CS_SentinelERecall*>(header);

        if (header->size != sizeof(CS_SentinelERecall))
            break;

        if (session->GetState() != SessionState::IN_GAME ||
            session->IsDead())
        {
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        bool recalled = false;

        {
            std::lock_guard<std::mutex> lock(g_sentinelEMutex);

            auto it = g_sentinelETraps.find(pkt->trapId);

            if (it == g_sentinelETraps.end())
                break;

            const SentinelETrapRuntime& trap = it->second;

            if (trap.roomId != roomId ||
                trap.ownerPlayerId != session->GetPlayerId())
            {
                break;
            }

            g_sentinelETraps.erase(it);

            uint64_t cooldownKey =
                MakeSentinelECooldownKey(
                    roomId,
                    session->GetPlayerId()
                );

            g_sentinelECooldowns[cooldownKey] =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(
                    static_cast<int>(
                        SENTINEL_E_RECALL_COOLDOWN *
                        1000.f
                    )
                );

            recalled = true;
        }

        if (recalled)
        {
            BroadcastSentinelEDestroy(
                roomId,
                pkt->trapId,
                SENTINEL_E_REMOVE_RECALLED,
                SENTINEL_E_RECALL_COOLDOWN
            );
        }

        break;
    }

    case PacketType::PKT_CS_SENTINEL_X_CAST:
    {
        auto* pkt =
            reinterpret_cast<CS_SentinelXCast*>(header);

        if (header->size != sizeof(CS_SentinelXCast))
            break;

        if (session->GetState() != SessionState::IN_GAME ||
            session->IsDead() ||
            IsPlayerStunned(session->GetPlayerId()) ||
            IsDroneBlockedAction(session, "SENTINEL_X_CAST"))
        {
            break;
        }

        if (!std::isfinite(pkt->x) ||
            !std::isfinite(pkt->y) ||
            !std::isfinite(pkt->z))
        {
            break;
        }

        uint32_t roomId = session->GetCurrentRoomId();

        if (roomId == 0)
            break;

        RoomMember ownerMember{};

        if (!RoomManager::Instance().GetMember(
            roomId,
            session->GetPlayerId(),
            ownerMember))
        {
            break;
        }

        if (ownerMember.team == RoomTeam::OBSERVER)
            break;

        Vector3 ownerPosition = session->GetPosition();
        float dx = pkt->x - ownerPosition.x;
        float dy = pkt->y - ownerPosition.y;
        float dz = pkt->z - ownerPosition.z;
        float distanceSquared = dx * dx + dy * dy + dz * dz;
        float allowedRange = SENTINEL_X_CAST_RANGE + 0.75f;

        if (distanceSquared > allowedRange * allowedRange)
        {
            std::cout
                << "[SENTINEL X CAST BLOCK] out of range playerId="
                << session->GetPlayerId()
                << std::endl;
            break;
        }

        uint32_t ultimateId =
            g_nextSentinelXUltimateId.fetch_add(1);
        uint8_t ownerTeam =
            static_cast<uint8_t>(ownerMember.team);
        std::vector<uint32_t> replacedUltimateIds;
        auto now = std::chrono::steady_clock::now();
        auto protectionEnd =
            now +
            std::chrono::milliseconds(
                static_cast<int>(
                    SENTINEL_X_PROTECTION_DURATION * 1000.f
                )
            );

        {
            std::lock_guard<std::mutex> lock(g_sentinelXMutex);

            for (auto it = g_sentinelXUltimates.begin();
                it != g_sentinelXUltimates.end();)
            {
                if (it->second.roomId == roomId &&
                    it->second.ownerPlayerId ==
                    session->GetPlayerId())
                {
                    replacedUltimateIds.push_back(it->first);
                    it = g_sentinelXUltimates.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            SentinelXRuntime state{};
            state.ultimateId = ultimateId;
            state.roomId = roomId;
            state.ownerPlayerId = session->GetPlayerId();
            state.ownerTeam = ownerTeam;
            state.x = pkt->x;
            state.y = pkt->y;
            state.z = pkt->z;

            g_sentinelXUltimates[ultimateId] = state;

            uint64_t protectionKey =
                MakeSentinelXProtectionKey(
                    roomId,
                    session->GetPlayerId()
                );
            auto protectionIt =
                g_sentinelXProtectionEnds.find(protectionKey);

            if (protectionIt ==
                    g_sentinelXProtectionEnds.end() ||
                protectionIt->second < protectionEnd)
            {
                g_sentinelXProtectionEnds[protectionKey] =
                    protectionEnd;
            }
        }

        for (uint32_t replacedId : replacedUltimateIds)
            BroadcastSentinelXDestroy(roomId, replacedId);

        SC_SentinelXSpawn out{};
        out.header.size = sizeof(out);
        out.header.type =
            static_cast<uint16_t>(
                PacketType::PKT_SC_SENTINEL_X_SPAWN
            );
        out.ultimateId = ultimateId;
        out.ownerPlayerId = session->GetPlayerId();
        out.ownerTeam = ownerTeam;
        out.x = pkt->x;
        out.y = pkt->y;
        out.z = pkt->z;
        out.radius = SENTINEL_X_RADIUS;
        out.duration = SENTINEL_X_DURATION;
        out.slowMultiplier = SENTINEL_X_SLOW_MULTIPLIER;
        out.protectionDuration =
            SENTINEL_X_PROTECTION_DURATION;

        BroadcastPacketToRoomInternal(roomId, &out);

        uint32_t ownerPlayerId = session->GetPlayerId();
        float areaX = pkt->x;
        float areaY = pkt->y;
        float areaZ = pkt->z;

        std::thread(
            [roomId,
             ultimateId,
             ownerPlayerId,
             ownerTeam,
             areaX,
             areaY,
             areaZ]()
            {
                constexpr int tickCount =
                    static_cast<int>(
                        SENTINEL_X_DURATION * 1000.f /
                        SENTINEL_X_TICK_MILLISECONDS
                    );

                for (int tick = 0; tick < tickCount; ++tick)
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(
                            SENTINEL_X_TICK_MILLISECONDS
                        )
                    );

                    {
                        std::lock_guard<std::mutex> lock(
                            g_sentinelXMutex
                        );

                        auto it =
                            g_sentinelXUltimates.find(ultimateId);

                        if (it == g_sentinelXUltimates.end() ||
                            it->second.roomId != roomId)
                        {
                            return;
                        }
                    }

                    Room room{};

                    if (!RoomManager::Instance().GetRoom(roomId, room))
                        continue;

                    float radiusSquared =
                        SENTINEL_X_RADIUS * SENTINEL_X_RADIUS;

                    for (const RoomMember& member : room.members)
                    {
                        if (member.playerId == ownerPlayerId ||
                            member.team == RoomTeam::OBSERVER ||
                            static_cast<uint8_t>(member.team) ==
                            ownerTeam)
                        {
                            continue;
                        }

                        auto target =
                            SessionManager::Instance().Find(
                                member.playerId
                            );

                        if (target == nullptr ||
                            target->GetState() !=
                            SessionState::IN_GAME ||
                            target->IsDead())
                        {
                            continue;
                        }

                        Vector3 targetPosition =
                            target->GetPosition();
                        float targetDx =
                            targetPosition.x - areaX;
                        float targetDy =
                            targetPosition.y - areaY;
                        float targetDz =
                            targetPosition.z - areaZ;
                        float targetDistanceSquared =
                            targetDx * targetDx +
                            targetDy * targetDy +
                            targetDz * targetDz;

                        if (targetDistanceSquared > radiusSquared)
                            continue;

                        target->SetLastDamager(ownerPlayerId);
                        target->AddAssistDamageRecord(
                            ownerPlayerId,
                            SENTINEL_X_DAMAGE_PER_TICK
                        );
                        int appliedDamage = target->Damage(
                            SENTINEL_X_DAMAGE_PER_TICK
                        );

                        auto owner =
                            SessionManager::Instance().Find(ownerPlayerId);
                        if (owner != nullptr)
                            owner->GetMatchStats().damageDealt += appliedDamage;

                        SC_HPUpdate hpPacket{};
                        FillHPUpdatePacket(hpPacket, target.get());
                        BroadcastPacketToRoomInternal(
                            roomId,
                            &hpPacket
                        );

                        if (target->IsDead())
                            PacketDispatcher::HandleDeath(target.get());
                    }
                }

                bool shouldDestroy = false;

                {
                    std::lock_guard<std::mutex> lock(
                        g_sentinelXMutex
                    );

                    auto it =
                        g_sentinelXUltimates.find(ultimateId);

                    if (it != g_sentinelXUltimates.end() &&
                        it->second.roomId == roomId)
                    {
                        g_sentinelXUltimates.erase(it);
                        shouldDestroy = true;
                    }
                }

                if (shouldDestroy)
                {
                    BroadcastSentinelXDestroy(
                        roomId,
                        ultimateId
                    );
                }
            }
        ).detach();

        std::cout
            << "[SENTINEL X CAST] ultimateId=" << ultimateId
            << " owner=" << session->GetPlayerId()
            << " roomId=" << roomId
            << " pos=(" << pkt->x << ", "
            << pkt->y << ", " << pkt->z << ")"
            << std::endl;

        break;
    }

    default:
    {
        std::cout << "[UNHANDLED PACKET] type=" << (uint16_t)type
            << " session=" << session
            << std::endl;
        break;
    }
    }
}

void AddKillStatByRound(Session* player, uint32_t roomId)
{
    if (player == nullptr)
        return;

    PlayerMatchStats& stats = player->GetMatchStats();

    stats.kills++;

    uint8_t currentRound = 1;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);
        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
            currentRound = it->second.currentRound;
    }

    if (IsFirstHalfRound(currentRound))
        stats.firstHalfKills++;
    else if (IsSecondHalfRound(currentRound))
        stats.secondHalfKills++;
    else
        stats.overtimeKills++;
}

void AddDeathStatByRound(Session* player, uint32_t roomId)
{
    if (player == nullptr)
        return;

    PlayerMatchStats& stats = player->GetMatchStats();

    stats.deaths++;

    uint8_t currentRound = 1;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);
        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
            currentRound = it->second.currentRound;
    }

    if (IsFirstHalfRound(currentRound))
        stats.firstHalfDeaths++;
    else if (IsSecondHalfRound(currentRound))
        stats.secondHalfDeaths++;
    else
        stats.overtimeDeaths++;
}

void AddAssistStatByRound(Session* player, uint32_t roomId)
{
    if (player == nullptr)
        return;

    PlayerMatchStats& stats = player->GetMatchStats();

    stats.assists++;

    uint8_t currentRound = 1;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);
        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
            currentRound = it->second.currentRound;
    }

    if (IsFirstHalfRound(currentRound))
        stats.firstHalfAssists++;
    else if (IsSecondHalfRound(currentRound))
        stats.secondHalfAssists++;
    else
        stats.overtimeAssists++;
}

void PacketDispatcher::HandleDeath(Session* target)
{
    if (target == nullptr)
        return;

    uint32_t roomId = target->GetCurrentRoomId();

    DestroyDroneForPlayer(target->GetPlayerId(), 1);

    EndEntryBladeUltimate(
        roomId,
        target->GetPlayerId(),
        "death"
    );

    constexpr int ASSIST_VALID_SECONDS = 10;

    uint32_t killerId = target->GetLastDamager();
    auto killer = SessionManager::Instance().Find(killerId);

    SC_Die diePkt{};
    diePkt.header.size = sizeof(diePkt);
    diePkt.header.type = static_cast<uint16_t>(PacketType::PKT_SC_DIE);

    diePkt.killerPlayerId = 0;
    diePkt.deadPlayerId = target->GetPlayerId();

    diePkt.killerTeam = 255;
    diePkt.deadTeam = 255;

    diePkt.killerCharacterId = -1;
    diePkt.deadCharacterId = -1;

    memset(diePkt.killerNickname, 0, sizeof(diePkt.killerNickname));
    memset(diePkt.deadNickname, 0, sizeof(diePkt.deadNickname));

    RoomMember deadMember{};

    if (RoomManager::Instance().GetMember(roomId, target->GetPlayerId(), deadMember))
    {
        diePkt.deadTeam =
            static_cast<uint8_t>(deadMember.team);

        diePkt.deadCharacterId =
            deadMember.lockedCharacterId;

        strncpy_s(
            diePkt.deadNickname,
            sizeof(diePkt.deadNickname),
            deadMember.nickname.c_str(),
            _TRUNCATE
        );
    }

    // 킬러가 정상적으로 존재할 때만 킬러 정보 입력
    if (killer != nullptr && killer.get() != target)
    {
        diePkt.killerPlayerId =
            killer->GetPlayerId();

        RoomMember killerMember{};

        if (RoomManager::Instance().GetMember(
            roomId,
            killer->GetPlayerId(),
            killerMember))
        {
            diePkt.killerTeam =
                static_cast<uint8_t>(killerMember.team);

            diePkt.killerCharacterId =
                killerMember.lockedCharacterId;

            strncpy_s(
                diePkt.killerNickname,
                sizeof(diePkt.killerNickname),
                killerMember.nickname.c_str(),
                _TRUNCATE
            );
        }
    }

    if (killer != nullptr && killer.get() != target)
    {
        AddKillStatByRound(killer.get(), roomId);
        if (target->GetLastDamageWasHeadshot())
            killer->GetMatchStats().headshotKills++;
        if (IsKnifeWeapon(target->GetLastDamageWeaponId()))
            killer->GetMatchStats().meleeKills++;
        killer->AddCredits(GetCreditReward("KILL"));

        AddDeathStatByRound(target, roomId);

        std::cout << "[CREDIT KILL] killer="
            << killer->GetPlayerId()
            << " add=" << GetCreditReward("KILL")
            << " total=" << killer->GetCredits()
            << std::endl;

        std::vector<uint32_t> assistPlayerIds =
            target->GetAssistPlayerIds(killerId, ASSIST_VALID_SECONDS);

        for (uint32_t assistPlayerId : assistPlayerIds)
        {
            auto assister = SessionManager::Instance().Find(assistPlayerId);

            if (assister == nullptr)
                continue;

            if (assister == killer)
                continue;

            if (assister.get() == target)
                continue;

            AddAssistStatByRound(assister.get(), roomId);

            std::cout << "[ASSIST UPDATE] assister="
                << assister->GetPlayerId()
                << " target=" << target->GetPlayerId()
                << std::endl;
        }

        std::cout << "[KDA UPDATE] killer="
            << killer->GetPlayerId()
            << " target=" << target->GetPlayerId()
            << std::endl;
    }
    else
    {
        AddDeathStatByRound(target, roomId);
    }

    target->ClearAssistDamageRecords();

    std::vector<PlayerWeaponState> ownedWeapons =
        target->GetOwnedWeapons();

    Vector3 deathPos = target->GetPosition();

    int dropOffset = 0;

    for (const PlayerWeaponState& weapon : ownedWeapons)
    {
        CreateDroppedWeapon(
            roomId,
            target->GetPlayerId(),
            weapon.weaponId,
            weapon.magAmmo,
            weapon.reserveAmmo,
            weapon.skinId,
            deathPos.x + 0.3f * dropOffset,
            deathPos.y + 0.5f,
            deathPos.z,
            0.f,
            0.f,
            1.f
        );

        dropOffset++;
    }

    for (const PlayerWeaponState& weapon : ownedWeapons)
    {
        target->RemoveWeapon(weapon.weaponId);
    }

    bool shouldDropSpike = false;
    Vector3 dropPos{};

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
        {
            MatchRuntimeState& state = it->second;

            if (!state.spikePlanted &&
                state.spikeCarrierPlayerId == target->GetPlayerId())
            {
                dropPos = target->GetPosition();
                state.spikeCarrierPlayerId = 0;
                shouldDropSpike = true;
            }
        }
    }

    if (shouldDropSpike)
    {
        BroadcastSpikeDropped(
            roomId,
            dropPos.x,
            dropPos.y,
            dropPos.z,
            0.0f,
            0.2f,
            0.0f,
            target->GetPlayerId()
        );

        std::cout << "[SPIKE DROP BY DEATH] playerId="
            << target->GetPlayerId()
            << " roomId=" << roomId
            << std::endl;
    }

    BroadcastPacketToRoomInternal(roomId, &diePkt);

    SC_HPUpdate hpPkt{};
    FillHPUpdatePacket(hpPkt, target);
    BroadcastPacketToRoomInternal(roomId, &hpPkt);

    std::cout << "[PLAYER DEAD] playerId="
        << target->GetPlayerId()
        << " roomId=" << roomId
        << " hp=" << target->GetHP()
        << std::endl;

    bool shouldCancelDefuse = false;
    bool halfDefused = false;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
        {
            MatchRuntimeState& state = it->second;

            if (state.spikeDefuserPlayerId == target->GetPlayerId())
            {
                state.spikeDefuserPlayerId = 0;
                halfDefused = state.spikeHalfDefused;
                shouldCancelDefuse = true;
            }
        }
    }

    if (shouldCancelDefuse)
    {
        BroadcastSpikeDefuseState(
            roomId,
            0,
            halfDefused,
            0
        );
    }

    BroadcastScoreboardUpdate(roomId);

    bool spikePlanted = false;

    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);

        auto it = g_matchStates.find(roomId);
        if (it != g_matchStates.end())
        {
            MatchRuntimeState& state = it->second;

            spikePlanted = state.spikePlanted;

            std::cout << "[SPIKE DEATH CHECK] deadPlayerId="
                << target->GetPlayerId()
                << " carrier=" << state.spikeCarrierPlayerId
                << " planted=" << state.spikePlanted
                << std::endl;
        }
    }

    TryResolveRoundByElimination(roomId);
}
