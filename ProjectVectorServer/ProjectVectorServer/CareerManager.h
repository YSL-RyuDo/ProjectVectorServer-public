#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
namespace VectorShop { class Engine; struct Result; }

struct CareerSeason
{
    int seasonId = 0;
    std::string seasonName;
    int64_t startUnix = 0;
    int64_t endUnix = 0;
    bool isActive = false;
};

struct CareerRankTier
{
    int tierId = 0;
    std::string tierName;
    int minPoints = 0;
    int maxPoints = 0;
    int rankImageId = 0;
    int leaderboardTop = 0;
};

struct CareerSeasonStats
{
    std::string userId;
    int seasonId = 0;
    int rankPoints = 0;
    int rankedMatches = 0;
    int wins = 0;
    int roundsPlayed = 0;
    int kills = 0;
    int totalDamage = 0;
    int mvpCount = 0;
};

struct CareerOverview
{
    CareerSeason season;
    CareerRankTier tier;
    CareerSeasonStats stats;
    int remainingSeasonDays = 0;
};

struct CareerRankingEntry
{
    int position = 0;
    std::string userId;
    std::string nickname;
    CareerRankTier tier;
    CareerSeasonStats stats;
    int characterId = -1;
    int score = 0;
    int matches = 0;
    int wins = 0;
};

struct CareerRankingPage
{
    int rankingType = 0;
    int characterId = 0;
    int page = 1;
    int totalPages = 1;
    int totalEntries = 0;
    std::vector<CareerRankingEntry> entries;
};

struct CareerCharacterStats
{
    std::string userId;
    int seasonId = 0;
    int characterId = 0;
    int masteryPoints = 0;
    int matches = 0;
    int wins = 0;
};

struct CareerDetailCharacterStats
{
    int characterId = 0;
    int performanceScore = 0;
    int matches = 0;
    int wins = 0;
    int kills = 0;
    int deaths = 0;
    int maxKills = 0;
    int maxDamage = 0;
};

struct CareerDetailGameStats
{
    int playCount = 0;
    int wins = 0;
    int roundsPlayed = 0;
    int totalDamage = 0;
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int headshotKills = 0;
    int meleeKills = 0;
    int spikePlants = 0;
    int spikeDefuses = 0;
    int maxAssists = 0;
    int maxRoundDamageHundredths = 0;
    int maxRoundKillsHundredths = 0;
};

struct CareerDetail
{
    int seasonId = 0;
    int modeType = 0;
    int selectedCharacterId = 0;
    bool hasData = false;
    std::vector<CareerSeason> seasons;
    std::vector<CareerDetailCharacterStats> characters;
    CareerDetailGameStats game;
};

struct CareerMatch
{
    uint64_t matchId = 0;
    int seasonId = 0;
    std::string mapName;
    int64_t startedAtUnix = 0;
    int durationSeconds = 0;
    int scoreA = 0;
    int scoreB = 0;
    int winnerTeam = 0;
    // 0: normal/private room, 1: ranked
    int matchType = 0;
};

struct CareerMatchPlayer
{
    uint64_t matchId = 0;
    std::string userId;
    std::string nickname;
    int team = 0;
    int characterId = 0;
    int combatScore = 0;
    int attackScore = 0;
    int skillScore = 0;
    int teamworkScore = 0;
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int damage = 0;
    int headshotKills = 0;
    int spikePlants = 0;
    int spikeDefuses = 0;
    int meleeKills = 0;
    bool isMvp = false;
    uint64_t partyId = 0;
    int partySize = 1;
};

struct CareerMatchmakingRating
{
    std::string userId;
    int queueType = 0;
    int matchmakingRating = 0;
    int ratingDeviation = 350;
    int matches = 0;
    int wins = 0;
};

struct CareerMatchHistoryEntry
{
    CareerMatch match;
    int myTeam = 0;
    std::vector<CareerMatchPlayer> players;
};

struct CareerItem
{
    int itemId = 0;
    std::string itemName;
    std::string description;
    int iconId = 0;
};

struct CareerInventoryEntry
{
    CareerItem item;
    int quantity = 0;
    int64_t acquiredAtUnix = 0;
};

struct CareerWallet
{
    std::string userId;
    int freeCurrency = 0;
    int victoryCurrency = 0;
    int claimedLevelReward = 0;
    int claimedHonorGrade = 0;
    std::string shopState;
    std::string passState;
};

enum class HonorChangeReason : uint8_t
{
    MatchCompleted = 1,
    MatchAbandoned = 2,
    ConfirmationDeclined = 3,
    DisruptivePlay = 4,
    ChatViolation = 5,
    MatchManipulation = 6
};

struct CareerHonorChange
{
    int64_t appliedAtUnix = 0;
    int delta = 0;
    HonorChangeReason reason = HonorChangeReason::MatchCompleted;
    uint64_t matchId = 0;
};

struct CareerHonorState
{
    int score = 601; // All accounts begin at grade 3 (Normal).
    int dailyEarned = 0;
    int64_t earnedDayUtc = 0;
    int claimedGrade = 0;
    std::vector<CareerHonorChange> history;
};

struct CareerHonorClaim
{
    bool succeeded = false;
    int claimedGrade = 0;
    int freeCurrencyAwarded = 0;
    int victoryCurrencyAwarded = 0;
    int totalFreeCurrency = 0;
    int totalVictoryCurrency = 0;
};

struct CareerLevelRewardClaim
{
    bool succeeded = false;
    int claimedLevel = 0;
    int freeCurrencyAwarded = 0;
    int victoryCurrencyAwarded = 0;
    int totalFreeCurrency = 0;
    int totalVictoryCurrency = 0;
};

struct CareerMatchReward
{
    int freeCurrencyReward = 0;
    int victoryCurrencyReward = 0;
    int totalFreeCurrency = 0;
    int totalVictoryCurrency = 0;
};

struct CareerRankAdjustment
{
    std::string userId;
    int rankPointsBefore = 0;
    int rankPointsAfter = 0;
    int matchmakingRatingBefore = 0;
    int matchmakingRatingAfter = 0;
    int ratingDeviationAfter = 350;
    int rankPointDelta = 0;
    uint16_t expectedWinRatePermille = 500;
    uint8_t modifierFlags = 0;
};

class CareerManager
{
public:
    struct PassResult
    {
        int32_t result = 1, level = 1, xp = 0, rewardMask = 0, questMask = 0;
        int32_t currency0 = 0, currency1 = 0;
        int64_t week = 0;
        int32_t progress[6]{};
    };
    PassResult ExecutePass(const std::string& userId, int operation, int index, int64_t expectedWeek);
    static CareerManager& Instance();

    // Loads all normalized CSV tables and repairs missing current-season rows
    // for users that already exist in users.csv.
    void Load();

    // Called after registration. Empty inventories are represented by no rows in
    // career_user_inventory.csv, so only the required season-stat row is added.
    void InitializeUser(const std::string& userId, const std::string& nickname);

    bool GetOverview(const std::string& userId, CareerOverview& outOverview);
    bool GetCareerDetail(
        const std::string& userId,
        int seasonId,
        int modeType,
        int characterId,
        CareerDetail& outDetail);
    bool GetWallet(const std::string& userId, CareerWallet& outWallet);
    VectorShop::Result ExecuteShop(VectorShop::Engine& engine, const std::string& userId,
        int operation, int currency, int count, int itemId, unsigned long long request);
    CareerLevelRewardClaim ClaimLevelRewards(
        const std::string& userId, int playerLevel, int targetLevel);
    bool GetHonorState(const std::string& userId, CareerHonorState& outState);
    CareerHonorClaim ClaimHonorRewards(const std::string& userId, int targetGrade);
    bool ApplyHonorViolation(const std::string& userId, HonorChangeReason reason);
    static int HonorGradeForScore(int score);
    std::vector<CareerRankingEntry> GetRankings(size_t limit);
    CareerRankingPage GetRankingPage(
        int rankingType,
        int characterId,
        int page,
        size_t pageSize,
        const std::string& searchQuery);
    std::vector<CareerMatchHistoryEntry> GetMatchHistory(
        const std::string& userId,
        int filterType,
        size_t limit);
    std::vector<CareerInventoryEntry> GetInventory(
        const std::string& userId,
        size_t limit);
    CareerMatchReward GrantMatchReward(
        const std::string& userId,
        bool won);
    bool GetMatchmakingRating(
        const std::string& userId,
        int queueType,
        CareerMatchmakingRating& outRating);

    uint64_t RecordMatch(
        const std::string& mapName,
        int64_t startedAtUnix,
        int durationSeconds,
        int scoreA,
        int scoreB,
        int winnerTeam,
        const std::vector<CareerMatchPlayer>& players,
        int matchType = 0,
        bool isMatchmade = false,
        std::vector<CareerRankAdjustment>* outRankAdjustments = nullptr);

private:
    void LoadUsersUnlocked();
    void LoadSeasonsUnlocked();
    void LoadRankTiersUnlocked();
    void LoadSeasonStatsUnlocked();
    void LoadMatchmakingRatingsUnlocked();
    void LoadCharacterStatsUnlocked();
    void LoadMatchesUnlocked();
    void LoadMatchPlayersUnlocked();
    void LoadItemsUnlocked();
    void LoadInventoryUnlocked();
    void LoadWalletsUnlocked();
    void LoadHonorUnlocked();

    void SaveSeasonsUnlocked();
    void SaveRankTiersUnlocked();
    void SaveSeasonStatsUnlocked();
    void SaveMatchmakingRatingsUnlocked();
    void SaveCharacterStatsUnlocked();
    void SaveMatchesUnlocked();
    void SaveMatchPlayersUnlocked();
    void SaveItemsUnlocked();
    void SaveInventoryUnlocked();
    bool SaveWalletsUnlocked();
    bool SaveHonorUnlocked();
    bool SaveHonorHistoryUnlocked();

    void SeedDefaultSeasonUnlocked();
    void SeedDefaultRankTiersUnlocked();
    int GetActiveSeasonIdUnlocked() const;
    CareerRankTier FindBaseTierUnlocked(int rankPoints) const;
    CareerRankTier ResolveDisplayedTierUnlocked(
        const CareerSeasonStats& stats) const;
    bool IsLeaderboardTierUnlocked(
        const CareerSeasonStats& stats,
        const CareerRankTier& tier) const;
    CareerSeasonStats& EnsureSeasonStatsUnlocked(
        const std::string& userId,
        int seasonId);
    CareerMatchmakingRating& EnsureMatchmakingRatingUnlocked(
        const std::string& userId,
        int queueType,
        int initialRating);
    CareerCharacterStats& EnsureCharacterStatsUnlocked(
        const std::string& userId,
        int seasonId,
        int characterId);
    std::vector<CareerRankingEntry> BuildOverallRankingsUnlocked();
    std::vector<CareerRankingEntry> BuildCharacterRankingsUnlocked(
        int characterId);

    static std::string MakeSeasonStatsKey(
        const std::string& userId,
        int seasonId);
    static std::string MakeMatchmakingRatingKey(
        const std::string& userId,
        int queueType);
    static std::string MakeInventoryKey(
        const std::string& userId,
        int itemId);
    static std::string MakeCharacterStatsKey(
        const std::string& userId,
        int seasonId,
        int characterId);

    std::mutex mutex_;
    std::unordered_map<std::string, std::string> users_;
    std::unordered_map<int, CareerSeason> seasons_;
    std::unordered_map<int, CareerRankTier> rankTiers_;
    std::unordered_map<std::string, CareerSeasonStats> seasonStats_;
    std::unordered_map<std::string, CareerMatchmakingRating>
        matchmakingRatings_;
    std::unordered_map<std::string, CareerCharacterStats> characterStats_;
    std::unordered_map<uint64_t, CareerMatch> matches_;
    std::vector<CareerMatchPlayer> matchPlayers_;
    std::unordered_map<int, CareerItem> items_;
    std::unordered_map<std::string, CareerInventoryEntry> inventory_;
    std::unordered_map<std::string, CareerWallet> wallets_;
    std::unordered_map<std::string, CareerHonorState> honor_;
    uint64_t nextMatchId_ = 1;
};
