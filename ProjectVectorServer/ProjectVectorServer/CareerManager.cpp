#include "CareerManager.h"
#include "RankRatingCalculator.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>
#define NOMINMAX
#include <Windows.h>

namespace
{
    constexpr const char* SEASONS_FILE = "career_seasons.csv";
    constexpr const char* RANK_TIERS_FILE = "career_rank_tiers.csv";
    constexpr const char* SEASON_STATS_FILE = "career_season_stats.csv";
    constexpr const char* MATCHMAKING_RATINGS_FILE =
        "career_matchmaking_ratings.csv";
    constexpr const char* CHARACTER_STATS_FILE = "career_character_stats.csv";
    constexpr const char* MATCHES_FILE = "career_matches.csv";
    constexpr const char* MATCH_PLAYERS_FILE = "career_match_players.csv";
    constexpr const char* ITEMS_FILE = "career_items.csv";
    constexpr const char* INVENTORY_FILE = "career_user_inventory.csv";
    constexpr const char* WALLETS_FILE = "career_user_wallet.csv";
    constexpr const char* HONOR_FILE = "career_user_honor.csv";
    constexpr const char* HONOR_HISTORY_FILE = "career_honor_history.csv";

    constexpr const char* SEASONS_HEADER =
        "seasonId,seasonName,startUnix,endUnix,isActive";
    constexpr const char* RANK_TIERS_HEADER =
        "tierId,tierName,minPoints,maxPoints,rankImageId,leaderboardTop";
    constexpr const char* SEASON_STATS_HEADER =
        "userId,seasonId,rankPoints,rankedMatches,wins,roundsPlayed,kills,totalDamage,mvpCount";
    constexpr const char* MATCHMAKING_RATINGS_HEADER =
        "userId,queueType,matchmakingRating,ratingDeviation,matches,wins";
    constexpr const char* CHARACTER_STATS_HEADER =
        "userId,seasonId,characterId,masteryPoints,matches,wins";
    constexpr const char* LEGACY_MATCHES_HEADER =
        "matchId,seasonId,mapName,startedAtUnix,durationSeconds,scoreA,scoreB,winnerTeam";
    constexpr const char* MATCHES_HEADER =
        "matchId,seasonId,mapName,startedAtUnix,durationSeconds,scoreA,scoreB,winnerTeam,matchType";
    constexpr const char* LEGACY_MATCH_PLAYERS_HEADER =
        "matchId,userId,team,characterId,combatScore,attackScore,skillScore,teamworkScore,kills,deaths,assists,damage,isMvp";
    constexpr const char* PREVIOUS_MATCH_PLAYERS_HEADER =
        "matchId,userId,team,characterId,combatScore,attackScore,skillScore,teamworkScore,kills,deaths,assists,damage,isMvp,headshotKills,spikePlants,spikeDefuses";
    constexpr const char* MATCH_PLAYERS_HEADER =
        "matchId,userId,team,characterId,combatScore,attackScore,skillScore,teamworkScore,kills,deaths,assists,damage,isMvp,headshotKills,spikePlants,spikeDefuses,meleeKills";
    constexpr const char* ITEMS_HEADER =
        "itemId,itemName,description,iconId";
    constexpr const char* INVENTORY_HEADER =
        "userId,itemId,quantity,acquiredAtUnix";
    constexpr const char* WALLETS_HEADER =
        "userId,freeCurrency,victoryCurrency,claimedLevelReward,claimedHonorGrade";
    constexpr const char* PREVIOUS_WALLETS_HEADER =
        "userId,freeCurrency,victoryCurrency,claimedLevelReward";
    constexpr const char* LEGACY_WALLETS_HEADER =
        "userId,freeCurrency,victoryCurrency";
    constexpr const char* LEGACY_HONOR_HEADER = "userId,score,dailyEarned,earnedDayUtc";
    constexpr const char* HONOR_HEADER = "userId,score,dailyEarned,earnedDayUtc,history";
    constexpr const char* HONOR_HISTORY_HEADER =
        "userId,appliedAtUnix,delta,reason,matchId";
    constexpr int HONOR_INITIAL_SCORE = 601;
    constexpr int HONOR_MAX_SCORE = 1800;
    constexpr int HONOR_DAILY_GAIN_CAP = 60;
    constexpr size_t HONOR_HISTORY_LIMIT = 50; // Five pages of ten rows.

    constexpr int CAREER_CHARACTER_COUNT = 4;

    int CalculateTeamPartyRatingBonus(
        const std::vector<CareerMatchPlayer>& players,
        int team)
    {
        std::unordered_map<uint64_t, int> partySizes;
        for (const CareerMatchPlayer& player : players)
        {
            if (player.team != team ||
                player.partyId == 0 ||
                player.partySize <= 1)
            {
                continue;
            }

            partySizes[player.partyId] = std::max(
                partySizes[player.partyId],
                std::clamp(player.partySize, 1, 3));
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

    std::vector<std::string> ParseCsvLine(const std::string& line)
    {
        std::vector<std::string> fields;
        std::string current;
        bool insideQuotes = false;

        for (size_t i = 0; i < line.size(); ++i)
        {
            char c = line[i];

            if (insideQuotes)
            {
                if (c == '"')
                {
                    if (i + 1 < line.size() && line[i + 1] == '"')
                    {
                        current.push_back('"');
                        ++i;
                    }
                    else
                    {
                        insideQuotes = false;
                    }
                }
                else
                {
                    current.push_back(c);
                }
            }
            else if (c == ',')
            {
                fields.push_back(current);
                current.clear();
            }
            else if (c == '"')
            {
                insideQuotes = true;
            }
            else
            {
                current.push_back(c);
            }
        }

        fields.push_back(current);
        return fields;
    }

    std::string EscapeCsv(const std::string& value)
    {
        bool needsQuotes =
            value.find(',') != std::string::npos ||
            value.find('"') != std::string::npos ||
            value.find('\n') != std::string::npos ||
            value.find('\r') != std::string::npos;

        if (!needsQuotes)
            return value;

        std::string escaped = "\"";
        for (char c : value)
        {
            if (c == '"')
                escaped += "\"\"";
            else
                escaped.push_back(c);
        }
        escaped.push_back('"');
        return escaped;
    }

    bool ReadHeader(std::ifstream& file, const char* expectedHeader)
    {
        std::string header;
        if (!std::getline(file, header))
            return false;

        if (!header.empty() && header.back() == '\r')
            header.pop_back();

        return header == expectedHeader;
    }

    bool TryParseInt(const std::string& value, int& outValue)
    {
        try
        {
            size_t consumed = 0;
            long long parsed = std::stoll(value, &consumed);
            if (consumed != value.size() ||
                parsed < std::numeric_limits<int>::min() ||
                parsed > std::numeric_limits<int>::max())
            {
                return false;
            }

            outValue = static_cast<int>(parsed);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool TryParseInt64(const std::string& value, int64_t& outValue)
    {
        try
        {
            size_t consumed = 0;
            long long parsed = std::stoll(value, &consumed);
            if (consumed != value.size())
                return false;

            outValue = static_cast<int64_t>(parsed);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool TryParseUInt64(const std::string& value, uint64_t& outValue)
    {
        try
        {
            size_t consumed = 0;
            unsigned long long parsed = std::stoull(value, &consumed);
            if (consumed != value.size())
                return false;

            outValue = static_cast<uint64_t>(parsed);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool RanksBefore(
        const CareerSeasonStats& lhs,
        const CareerSeasonStats& rhs)
    {
        if (lhs.rankPoints != rhs.rankPoints)
            return lhs.rankPoints > rhs.rankPoints;
        if (lhs.wins != rhs.wins)
            return lhs.wins > rhs.wins;
        if (lhs.rankedMatches != rhs.rankedMatches)
            return lhs.rankedMatches < rhs.rankedMatches;
        return lhs.userId < rhs.userId;
    }

    std::string ToLowerAscii(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
        return value;
    }
}

CareerManager& CareerManager::Instance()
{
    static CareerManager instance;
    return instance;
}

void CareerManager::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);

    users_.clear();
    seasons_.clear();
    rankTiers_.clear();
    seasonStats_.clear();
    matchmakingRatings_.clear();
    characterStats_.clear();
    matches_.clear();
    matchPlayers_.clear();
    items_.clear();
    inventory_.clear();
    wallets_.clear();
    honor_.clear();
    nextMatchId_ = 1;

    LoadUsersUnlocked();
    LoadSeasonsUnlocked();
    LoadRankTiersUnlocked();
    LoadSeasonStatsUnlocked();
    LoadMatchmakingRatingsUnlocked();
    LoadCharacterStatsUnlocked();
    LoadMatchesUnlocked();
    LoadMatchPlayersUnlocked();
    LoadItemsUnlocked();
    LoadInventoryUnlocked();
    LoadWalletsUnlocked();
    LoadHonorUnlocked();

    if (seasons_.empty())
        SeedDefaultSeasonUnlocked();

    if (rankTiers_.empty())
        SeedDefaultRankTiersUnlocked();

    if (items_.empty())
    {
        CareerItem item{};
        item.itemId = 1;
        item.itemName = "Career Starter Badge";
        item.description = "Default career inventory catalog item";
        item.iconId = 0;
        items_[item.itemId] = item;
    }

    int activeSeasonId = GetActiveSeasonIdUnlocked();
    for (const auto& pair : users_)
    {
        CareerWallet& wallet = wallets_[pair.first];
        wallet.userId = pair.first;
        honor_.try_emplace(pair.first);
        CareerSeasonStats& seasonStats =
            EnsureSeasonStatsUnlocked(pair.first, activeSeasonId);
        EnsureMatchmakingRatingUnlocked(
            pair.first,
            0,
            seasonStats.rankPoints);
        std::string rankedRatingKey =
            MakeMatchmakingRatingKey(pair.first, 1);
        bool needsRankedRatingMigration =
            matchmakingRatings_.count(rankedRatingKey) == 0;
        CareerMatchmakingRating& rankedRating =
            EnsureMatchmakingRatingUnlocked(
                pair.first,
                1,
                seasonStats.rankPoints);
        if (needsRankedRatingMigration)
        {
            rankedRating.matches = seasonStats.rankedMatches;
            rankedRating.wins = seasonStats.wins;
            rankedRating.ratingDeviation =
                RankRatingCalculator::EstimateLegacyRatingDeviation(
                    seasonStats.rankedMatches);
        }
        for (int characterId = 0;
            characterId < CAREER_CHARACTER_COUNT;
            ++characterId)
        {
            EnsureCharacterStatsUnlocked(
                pair.first,
                activeSeasonId,
                characterId);
        }
    }

    SaveSeasonsUnlocked();
    SaveRankTiersUnlocked();
    SaveSeasonStatsUnlocked();
    SaveMatchmakingRatingsUnlocked();
    SaveCharacterStatsUnlocked();
    SaveMatchesUnlocked();
    SaveMatchPlayersUnlocked();
    SaveItemsUnlocked();
    SaveInventoryUnlocked();
    SaveWalletsUnlocked();
    SaveHonorUnlocked();
    SaveHonorHistoryUnlocked();

    std::cout << "[CAREER DATA] users=" << users_.size()
        << " seasons=" << seasons_.size()
        << " stats=" << seasonStats_.size()
        << " matchmakingRatings=" << matchmakingRatings_.size()
        << " characterStats=" << characterStats_.size()
        << " matches=" << matches_.size()
        << " participants=" << matchPlayers_.size()
        << " inventory=" << inventory_.size()
        << " wallets=" << wallets_.size()
        << std::endl;
}

void CareerManager::InitializeUser(
    const std::string& userId,
    const std::string& nickname)
{
    if (userId.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex_);

    users_[userId] = nickname;
    int activeSeasonId = GetActiveSeasonIdUnlocked();

    if (activeSeasonId <= 0)
    {
        SeedDefaultSeasonUnlocked();
        activeSeasonId = GetActiveSeasonIdUnlocked();
        SaveSeasonsUnlocked();
    }

    CareerSeasonStats& seasonStats =
        EnsureSeasonStatsUnlocked(userId, activeSeasonId);
    EnsureMatchmakingRatingUnlocked(userId, 0, seasonStats.rankPoints);
    EnsureMatchmakingRatingUnlocked(userId, 1, seasonStats.rankPoints);
    CareerWallet& wallet = wallets_[userId];
    wallet.userId = userId;
    honor_.try_emplace(userId);
    for (int characterId = 0;
        characterId < CAREER_CHARACTER_COUNT;
        ++characterId)
    {
        EnsureCharacterStatsUnlocked(userId, activeSeasonId, characterId);
    }
    SaveSeasonStatsUnlocked();
    SaveMatchmakingRatingsUnlocked();
    SaveCharacterStatsUnlocked();
    SaveWalletsUnlocked();
    SaveHonorUnlocked();

    std::cout << "[CAREER USER INIT] userId=" << userId
        << " seasonId=" << activeSeasonId
        << std::endl;
}

bool CareerManager::GetWallet(
    const std::string& userId,
    CareerWallet& outWallet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = wallets_.find(userId);
    if (it == wallets_.end())
        return false;
    outWallet = it->second;
    return true;
}

int CareerManager::HonorGradeForScore(int score)
{
    if (score >= 1601) return 6;
    if (score >= 1201) return 5;
    if (score >= 901) return 4;
    if (score >= 601) return 3;
    if (score >= 301) return 2;
    return 1;
}

bool CareerManager::GetHonorState(const std::string& userId, CareerHonorState& outState)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = honor_.find(userId);
    if (it == honor_.end()) return false;
    outState = it->second;
    auto wallet = wallets_.find(userId);
    if (wallet != wallets_.end()) outState.claimedGrade = wallet->second.claimedHonorGrade;
    return true;
}

CareerHonorClaim CareerManager::ClaimHonorRewards(
    const std::string& userId, int targetGrade)
{
    std::lock_guard<std::mutex> lock(mutex_);
    CareerHonorClaim result{};
    auto state = honor_.find(userId);
    auto walletIt = wallets_.find(userId);
    if (state == honor_.end() || walletIt == wallets_.end()) return result;
    CareerWallet& wallet = walletIt->second;
    result.claimedGrade = wallet.claimedHonorGrade;
    result.totalFreeCurrency = wallet.freeCurrency;
    result.totalVictoryCurrency = wallet.victoryCurrency;
    const int grade = HonorGradeForScore(state->second.score);
    if (targetGrade < 5 || targetGrade > 6 || targetGrade > grade ||
        targetGrade <= wallet.claimedHonorGrade) return result;

    // Claiming grade 6 also grants an unclaimed grade 5 reward.
    const int freeAward = (wallet.claimedHonorGrade < 5 ? 1000 : 0) +
        (targetGrade == 6 ? 1500 : 0);
    const int victoryAward = (wallet.claimedHonorGrade < 5 ? 2 : 0) +
        (targetGrade == 6 ? 3 : 0);
    if (wallet.freeCurrency > std::numeric_limits<int>::max() - freeAward ||
        wallet.victoryCurrency > std::numeric_limits<int>::max() - victoryAward)
        return result;
    const CareerWallet previous = wallet;
    wallet.freeCurrency += freeAward;
    wallet.victoryCurrency += victoryAward;
    wallet.claimedHonorGrade = targetGrade;
    if (!SaveWalletsUnlocked()) { wallet = previous; return result; }
    result.succeeded = true;
    result.claimedGrade = targetGrade;
    result.freeCurrencyAwarded = freeAward;
    result.victoryCurrencyAwarded = victoryAward;
    result.totalFreeCurrency = wallet.freeCurrency;
    result.totalVictoryCurrency = wallet.victoryCurrency;
    return result;
}

bool CareerManager::ApplyHonorViolation(
    const std::string& userId, HonorChangeReason reason)
{
    int penalty = 0;
    switch (reason)
    {
    case HonorChangeReason::MatchAbandoned: penalty = -100; break;
    case HonorChangeReason::ConfirmationDeclined: penalty = -50; break;
    case HonorChangeReason::DisruptivePlay: penalty = -160; break;
    case HonorChangeReason::ChatViolation: penalty = -120; break;
    case HonorChangeReason::MatchManipulation: penalty = -240; break;
    default: return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = honor_.find(userId);
    if (it == honor_.end()) return false;
    CareerHonorState previous = it->second;
    CareerHonorState& state = it->second;
    const int after = std::max(0, state.score + penalty);
    const int actual = after - state.score;
    state.score = after;
    state.history.insert(state.history.begin(),
        { static_cast<int64_t>(std::time(nullptr)), actual, reason, 0 });
    if (state.history.size() > HONOR_HISTORY_LIMIT)
        state.history.resize(HONOR_HISTORY_LIMIT);
    if (!SaveHonorUnlocked())
    {
        state = previous;
        return false;
    }
    SaveHonorHistoryUnlocked(); // Human-readable mirror; snapshot above is authoritative.
    return true;
}

CareerLevelRewardClaim CareerManager::ClaimLevelRewards(
    const std::string& userId, int playerLevel, int targetLevel)
{
    std::lock_guard<std::mutex> lock(mutex_);
    CareerLevelRewardClaim result{};
    auto it = wallets_.find(userId);
    if (it == wallets_.end()) return result;

    CareerWallet& wallet = it->second;
    result.claimedLevel = wallet.claimedLevelReward;
    result.totalFreeCurrency = wallet.freeCurrency;
    result.totalVictoryCurrency = wallet.victoryCurrency;
    if (targetLevel <= wallet.claimedLevelReward || targetLevel < 1 ||
        targetLevel > playerLevel || targetLevel > 100)
        return result;

    const int levelCount = targetLevel - wallet.claimedLevelReward;
    const int freeAward = levelCount * 300;
    const int victoryAward = targetLevel / 5 - wallet.claimedLevelReward / 5;
    if (wallet.freeCurrency > std::numeric_limits<int>::max() - freeAward ||
        wallet.victoryCurrency > std::numeric_limits<int>::max() - victoryAward)
        return result;

    const CareerWallet previous = wallet;
    wallet.freeCurrency += freeAward;
    wallet.victoryCurrency += victoryAward;
    wallet.claimedLevelReward = targetLevel;
    if (!SaveWalletsUnlocked())
    {
        wallet = previous;
        return result;
    }

    result.succeeded = true;
    result.claimedLevel = targetLevel;
    result.freeCurrencyAwarded = freeAward;
    result.victoryCurrencyAwarded = victoryAward;
    result.totalFreeCurrency = wallet.freeCurrency;
    result.totalVictoryCurrency = wallet.victoryCurrency;
    return result;
}

bool CareerManager::GetOverview(
    const std::string& userId,
    CareerOverview& outOverview)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (users_.count(userId) == 0)
        return false;

    int seasonId = GetActiveSeasonIdUnlocked();
    auto seasonIt = seasons_.find(seasonId);
    if (seasonIt == seasons_.end())
        return false;

    CareerSeasonStats& stats = EnsureSeasonStatsUnlocked(userId, seasonId);

    outOverview = CareerOverview{};
    outOverview.season = seasonIt->second;
    outOverview.stats = stats;
    outOverview.tier = ResolveDisplayedTierUnlocked(stats);

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (outOverview.season.endUnix > now)
    {
        constexpr int64_t secondsPerDay = 24 * 60 * 60;
        int64_t remainingSeconds = outOverview.season.endUnix - now;
        outOverview.remainingSeasonDays = static_cast<int>(
            (remainingSeconds + secondsPerDay - 1) / secondsPerDay);
    }

    return true;
}

bool CareerManager::GetCareerDetail(
    const std::string& userId,
    int seasonId,
    int modeType,
    int characterId,
    CareerDetail& outDetail)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (users_.count(userId) == 0)
        return false;

    if (seasons_.count(seasonId) == 0)
        seasonId = GetActiveSeasonIdUnlocked();
    if (seasons_.count(seasonId) == 0)
        return false;

    modeType = std::clamp(modeType, 0, 2);
    characterId = std::clamp(characterId, 0, CAREER_CHARACTER_COUNT - 1);

    outDetail = CareerDetail{};
    outDetail.seasonId = seasonId;
    outDetail.modeType = modeType;
    outDetail.selectedCharacterId = characterId;

    for (const auto& pair : seasons_)
        outDetail.seasons.push_back(pair.second);
    std::sort(
        outDetail.seasons.begin(),
        outDetail.seasons.end(),
        [](const CareerSeason& lhs, const CareerSeason& rhs)
        {
            return lhs.seasonId > rhs.seasonId;
        });

    outDetail.characters.resize(CAREER_CHARACTER_COUNT);
    for (int i = 0; i < CAREER_CHARACTER_COUNT; ++i)
        outDetail.characters[i].characterId = i;

    for (const CareerMatchPlayer& player : matchPlayers_)
    {
        if (player.userId != userId)
            continue;

        auto matchIt = matches_.find(player.matchId);
        if (matchIt == matches_.end())
            continue;

        const CareerMatch& match = matchIt->second;
        if (match.seasonId != seasonId || match.matchType != modeType)
            continue;

        int rounds = std::max(1, match.scoreA + match.scoreB);
        bool won = player.team == match.winnerTeam;
        CareerDetailGameStats& game = outDetail.game;
        game.playCount++;
        game.wins += won ? 1 : 0;
        game.roundsPlayed += rounds;
        game.totalDamage += std::max(0, player.damage);
        game.kills += std::max(0, player.kills);
        game.deaths += std::max(0, player.deaths);
        game.assists += std::max(0, player.assists);
        game.headshotKills += std::max(0, player.headshotKills);
        game.meleeKills += std::max(0, player.meleeKills);
        game.spikePlants += std::max(0, player.spikePlants);
        game.spikeDefuses += std::max(0, player.spikeDefuses);
        game.maxAssists = std::max(game.maxAssists, std::max(0, player.assists));
        game.maxRoundDamageHundredths = std::max(
            game.maxRoundDamageHundredths,
            static_cast<int>((static_cast<int64_t>(std::max(0, player.damage)) * 100 + rounds / 2) / rounds));
        game.maxRoundKillsHundredths = std::max(
            game.maxRoundKillsHundredths,
            static_cast<int>((static_cast<int64_t>(std::max(0, player.kills)) * 100 + rounds / 2) / rounds));

        if (player.characterId >= 0 &&
            player.characterId < CAREER_CHARACTER_COUNT)
        {
            CareerDetailCharacterStats& character =
                outDetail.characters[player.characterId];
            character.performanceScore += std::max(0, player.combatScore);
            character.matches++;
            character.wins += won ? 1 : 0;
            character.kills += std::max(0, player.kills);
            character.deaths += std::max(0, player.deaths);
            character.maxKills = std::max(
                character.maxKills,
                std::max(0, player.kills));
            character.maxDamage = std::max(
                character.maxDamage,
                std::max(0, player.damage));
        }
    }

    outDetail.hasData = outDetail.game.playCount > 0;
    for (CareerDetailCharacterStats& character : outDetail.characters)
    {
        if (character.matches > 0)
            character.performanceScore /= character.matches;
    }

    return true;
}

std::vector<CareerRankingEntry> CareerManager::GetRankings(size_t limit)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<CareerRankingEntry> result = BuildOverallRankingsUnlocked();
    if (result.size() > limit)
        result.resize(limit);

    return result;
}

CareerRankingPage CareerManager::GetRankingPage(
    int rankingType,
    int characterId,
    int page,
    size_t pageSize,
    const std::string& searchQuery)
{
    std::lock_guard<std::mutex> lock(mutex_);

    constexpr size_t normalRankingLimit = 100;
    rankingType = rankingType == 1 ? 1 : 0;
    characterId = std::max(
        0,
        std::min(characterId, CAREER_CHARACTER_COUNT - 1));
    pageSize = std::max<size_t>(
        1,
        std::min<size_t>(pageSize, 10));

    std::vector<CareerRankingEntry> rankings =
        rankingType == 1
        ? BuildCharacterRankingsUnlocked(characterId)
        : BuildOverallRankingsUnlocked();

    std::string normalizedQuery = ToLowerAscii(searchQuery);
    if (!normalizedQuery.empty())
    {
        rankings.erase(
            std::remove_if(
                rankings.begin(),
                rankings.end(),
                [&normalizedQuery](const CareerRankingEntry& entry)
                {
                    std::string userId = ToLowerAscii(entry.userId);
                    std::string nickname = ToLowerAscii(entry.nickname);
                    return userId.find(normalizedQuery) == std::string::npos &&
                        nickname.find(normalizedQuery) == std::string::npos;
                }),
            rankings.end());
    }
    else if (rankings.size() > normalRankingLimit)
    {
        rankings.resize(normalRankingLimit);
    }

    CareerRankingPage result{};
    result.rankingType = rankingType;
    result.characterId = characterId;
    result.totalEntries = static_cast<int>(rankings.size());
    result.totalPages = std::max(
        1,
        static_cast<int>((rankings.size() + pageSize - 1) / pageSize));
    result.page = std::max(1, std::min(page, result.totalPages));

    size_t startIndex =
        static_cast<size_t>(result.page - 1) * pageSize;
    if (startIndex < rankings.size())
    {
        size_t endIndex = std::min(startIndex + pageSize, rankings.size());
        result.entries.insert(
            result.entries.end(),
            rankings.begin() + startIndex,
            rankings.begin() + endIndex);
    }

    return result;
}

std::vector<CareerMatchHistoryEntry> CareerManager::GetMatchHistory(
    const std::string& userId,
    int filterType,
    size_t limit)
{
    std::lock_guard<std::mutex> lock(mutex_);

    filterType = std::max(0, std::min(filterType, 2));

    std::unordered_set<uint64_t> userMatchIds;
    std::unordered_map<uint64_t, int> userTeams;

    for (const CareerMatchPlayer& player : matchPlayers_)
    {
        if (player.userId == userId)
        {
            userMatchIds.insert(player.matchId);
            userTeams[player.matchId] = player.team;
        }
    }

    std::vector<CareerMatch> matches;
    for (uint64_t matchId : userMatchIds)
    {
        auto matchIt = matches_.find(matchId);
        if (matchIt == matches_.end())
            continue;

        const CareerMatch& match = matchIt->second;
        if (filterType == 1 && match.matchType != 0)
            continue;
        if (filterType == 2 && match.matchType != 1)
            continue;

        matches.push_back(match);
    }

    std::sort(
        matches.begin(),
        matches.end(),
        [](const CareerMatch& lhs, const CareerMatch& rhs)
        {
            if (lhs.startedAtUnix != rhs.startedAtUnix)
                return lhs.startedAtUnix > rhs.startedAtUnix;
            return lhs.matchId > rhs.matchId;
        });

    if (matches.size() > limit)
        matches.resize(limit);

    std::vector<CareerMatchHistoryEntry> result;
    result.reserve(matches.size());

    for (const CareerMatch& match : matches)
    {
        CareerMatchHistoryEntry history{};
        history.match = match;
        history.myTeam = userTeams[match.matchId];

        for (const CareerMatchPlayer& storedPlayer : matchPlayers_)
        {
            if (storedPlayer.matchId != match.matchId)
                continue;

            CareerMatchPlayer player = storedPlayer;
            auto userIt = users_.find(player.userId);
            player.nickname =
                userIt != users_.end() ? userIt->second : player.userId;
            history.players.push_back(player);
        }

        std::sort(
            history.players.begin(),
            history.players.end(),
            [](const CareerMatchPlayer& lhs, const CareerMatchPlayer& rhs)
            {
                if (lhs.team != rhs.team)
                    return lhs.team < rhs.team;
                if (lhs.combatScore != rhs.combatScore)
                    return lhs.combatScore > rhs.combatScore;
                return lhs.userId < rhs.userId;
            });

        result.push_back(history);
    }

    return result;
}

std::vector<CareerInventoryEntry> CareerManager::GetInventory(
    const std::string& userId,
    size_t limit)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<CareerInventoryEntry> result;

    for (const auto& pair : inventory_)
    {
        const std::string& key = pair.first;
        std::string prefix = userId + "\x1f";
        if (key.rfind(prefix, 0) != 0 || pair.second.quantity <= 0)
            continue;

        result.push_back(pair.second);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const CareerInventoryEntry& lhs, const CareerInventoryEntry& rhs)
        {
            return lhs.item.itemId < rhs.item.itemId;
        });

    if (result.size() > limit)
        result.resize(limit);

    return result;
}

CareerMatchReward CareerManager::GrantMatchReward(
    const std::string& userId,
    bool won)
{
    std::lock_guard<std::mutex> lock(mutex_);

    CareerMatchReward reward{};
    if (users_.count(userId) == 0)
        return reward;

    constexpr int participationReward = 100;
    constexpr int winReward = 1;

    CareerWallet& wallet = wallets_[userId];
    wallet.userId = userId;
    reward.freeCurrencyReward = participationReward;
    reward.victoryCurrencyReward = won ? winReward : 0;

    wallet.freeCurrency += reward.freeCurrencyReward;
    wallet.victoryCurrency += reward.victoryCurrencyReward;
    reward.totalFreeCurrency = wallet.freeCurrency;
    reward.totalVictoryCurrency = wallet.victoryCurrency;

    SaveWalletsUnlocked();
    return reward;
}

bool CareerManager::GetMatchmakingRating(
    const std::string& userId,
    int queueType,
    CareerMatchmakingRating& outRating)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (users_.count(userId) == 0)
        return false;

    int activeSeasonId = GetActiveSeasonIdUnlocked();
    CareerSeasonStats& seasonStats =
        EnsureSeasonStatsUnlocked(userId, activeSeasonId);
    CareerMatchmakingRating& rating = EnsureMatchmakingRatingUnlocked(
        userId,
        queueType == 1 ? 1 : 0,
        seasonStats.rankPoints);
    outRating = rating;
    return true;
}

uint64_t CareerManager::RecordMatch(
    const std::string& mapName,
    int64_t startedAtUnix,
    int durationSeconds,
    int scoreA,
    int scoreB,
    int winnerTeam,
    const std::vector<CareerMatchPlayer>& players,
    int matchType,
    bool isMatchmade,
    std::vector<CareerRankAdjustment>* outRankAdjustments)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (outRankAdjustments != nullptr)
        outRankAdjustments->clear();

    std::vector<CareerMatchPlayer> validPlayers;
    std::unordered_set<std::string> seenUsers;

    for (const CareerMatchPlayer& player : players)
    {
        if (users_.count(player.userId) == 0 ||
            !seenUsers.insert(player.userId).second)
        {
            continue;
        }

        validPlayers.push_back(player);
    }

    if (validPlayers.empty())
        return 0;

    int seasonId = GetActiveSeasonIdUnlocked();
    uint64_t matchId = nextMatchId_++;

    CareerMatch match{};
    match.matchId = matchId;
    match.seasonId = seasonId;
    match.mapName = mapName.empty() ? "Unknown" : mapName;
    match.startedAtUnix = startedAtUnix;
    match.durationSeconds = std::max(0, durationSeconds);
    match.scoreA = std::max(0, scoreA);
    match.scoreB = std::max(0, scoreB);
    match.winnerTeam = winnerTeam;
    match.matchType = std::clamp(matchType, 0, 2);
    matches_[matchId] = match;

    int roundsPlayed = std::max(1, match.scoreA + match.scoreB);
    std::unordered_map<std::string, CareerRankAdjustment> ratingAdjustments;

    if (isMatchmade)
    {
        std::unordered_map<std::string, CareerSeasonStats> preMatchStats;
        std::unordered_map<std::string, CareerMatchmakingRating>
            preMatchRatings;
        std::unordered_map<int, double> teamMmrTotals;
        std::unordered_map<int, int> teamPlayerCounts;
        std::unordered_map<int, int> teamPartyBonuses;
        int queueType = match.matchType == 1 ? 1 : 0;

        for (const CareerMatchPlayer& player : validPlayers)
        {
            CareerSeasonStats& stats =
                EnsureSeasonStatsUnlocked(player.userId, seasonId);
            CareerMatchmakingRating& matchmakingRating =
                EnsureMatchmakingRatingUnlocked(
                    player.userId,
                    queueType,
                    stats.rankPoints);
            preMatchStats[player.userId] = stats;
            preMatchRatings[player.userId] = matchmakingRating;
            teamMmrTotals[player.team] +=
                matchmakingRating.matchmakingRating;
            teamPlayerCounts[player.team]++;
        }

        for (const CareerMatchPlayer& player : validPlayers)
        {
            teamPartyBonuses[player.team] =
                CalculateTeamPartyRatingBonus(validPlayers, player.team);
        }

        for (const CareerMatchPlayer& player : validPlayers)
        {
            const CareerSeasonStats& beforeStats =
                preMatchStats.at(player.userId);
            const CareerMatchmakingRating& beforeRating =
                preMatchRatings.at(player.userId);
            int opponentTeam = player.team == 0 ? 1 : 0;
            double ownTeamAverage = teamPlayerCounts[player.team] > 0
                ? teamMmrTotals[player.team] /
                    teamPlayerCounts[player.team]
                : static_cast<double>(beforeRating.matchmakingRating);
            double opponentTeamAverage = teamPlayerCounts[opponentTeam] > 0
                ? teamMmrTotals[opponentTeam] /
                    teamPlayerCounts[opponentTeam]
                : ownTeamAverage;
            ownTeamAverage += teamPartyBonuses[player.team];
            opponentTeamAverage += teamPartyBonuses[opponentTeam];
            bool won = player.team == winnerTeam;

            RankRatingInput input{};
            input.rankPoints = beforeStats.rankPoints;
            input.matchmakingRating = beforeRating.matchmakingRating;
            input.ratingDeviation = beforeRating.ratingDeviation;
            input.rankedMatches = beforeRating.matches;
            input.ownTeamAverageMmr = ownTeamAverage;
            input.opponentTeamAverageMmr = opponentTeamAverage;
            input.won = won;
            input.updateVisibleRank = match.matchType == 1;
            input.partyDisadvantage =
                teamPartyBonuses[player.team] <
                teamPartyBonuses[opponentTeam];
            input.scoreFor = player.team == 0 ? match.scoreA : match.scoreB;
            input.scoreAgainst = player.team == 0 ? match.scoreB : match.scoreA;

            RankRatingResult rating = RankRatingCalculator::Calculate(input);
            CareerRankAdjustment adjustment{};
            adjustment.userId = player.userId;
            adjustment.rankPointsBefore = beforeStats.rankPoints;
            adjustment.rankPointsAfter = rating.rankPointsAfter;
            adjustment.matchmakingRatingBefore =
                beforeRating.matchmakingRating;
            adjustment.matchmakingRatingAfter =
                rating.matchmakingRatingAfter;
            adjustment.ratingDeviationAfter = rating.ratingDeviationAfter;
            adjustment.rankPointDelta = rating.rankPointDelta;
            adjustment.expectedWinRatePermille =
                rating.expectedWinRatePermille;
            adjustment.modifierFlags = rating.modifierFlags;
            ratingAdjustments[player.userId] = adjustment;

            if (outRankAdjustments != nullptr)
                outRankAdjustments->push_back(adjustment);
        }
    }

    std::unordered_map<std::string, CareerHonorState> previousHonor;
    for (CareerMatchPlayer player : validPlayers)
    {
        player.matchId = matchId;
        player.nickname.clear();
        matchPlayers_.push_back(player);

        CareerSeasonStats& stats =
            EnsureSeasonStatsUnlocked(player.userId, seasonId);

        bool won = player.team == winnerTeam;
        if (isMatchmade)
        {
            auto ratingIt = ratingAdjustments.find(player.userId);
            if (ratingIt != ratingAdjustments.end())
            {
                int queueType = match.matchType == 1 ? 1 : 0;
                CareerMatchmakingRating& matchmakingRating =
                    EnsureMatchmakingRatingUnlocked(
                        player.userId,
                        queueType,
                        stats.rankPoints);
                matchmakingRating.matchmakingRating =
                    ratingIt->second.matchmakingRatingAfter;
                matchmakingRating.ratingDeviation =
                    ratingIt->second.ratingDeviationAfter;
                matchmakingRating.matches++;
                if (won)
                    matchmakingRating.wins++;

                if (match.matchType == 1)
                    stats.rankPoints = ratingIt->second.rankPointsAfter;
            }
        }

        if (match.matchType == 1 && isMatchmade)
        {
            stats.rankedMatches++;
            stats.roundsPlayed += roundsPlayed;
            stats.kills += std::max(0, player.kills);
            stats.totalDamage += std::max(0, player.damage);
            if (won)
                stats.wins++;
            if (player.isMvp)
                stats.mvpCount++;
        }

        if (player.characterId >= 0 &&
            player.characterId < CAREER_CHARACTER_COUNT)
        {
            CareerCharacterStats& characterStats =
                EnsureCharacterStatsUnlocked(
                    player.userId,
                    seasonId,
                    player.characterId);

            characterStats.matches++;
            if (won)
                characterStats.wins++;

            int masteryGain =
                10 +
                std::max(0, player.kills) * 10 +
                std::max(0, player.assists) * 5 +
                std::max(0, player.damage) / 100 +
                (won ? 25 : 0) +
                (player.isMvp ? 20 : 0);
            characterStats.masteryPoints += masteryGain;
        }

        if (isMatchmade && durationSeconds >= 180)
        {
            CareerHonorState& honor = honor_[player.userId];
            previousHonor.emplace(player.userId, honor);
            const int64_t now = static_cast<int64_t>(std::time(nullptr));
            const int64_t day = now / 86400;
            if (honor.earnedDayUtc != day)
            {
                honor.earnedDayUtc = day;
                honor.dailyEarned = 0;
            }
            const int baseGain = durationSeconds >= 600 ? 20 : 8;
            const int gain = std::max(0, std::min({baseGain,
                HONOR_DAILY_GAIN_CAP - honor.dailyEarned,
                HONOR_MAX_SCORE - honor.score}));
            if (gain > 0)
            {
                honor.score += gain;
                honor.dailyEarned += gain;
                honor.history.insert(honor.history.begin(),
                    { now, gain, HonorChangeReason::MatchCompleted, matchId });
                if (honor.history.size() > HONOR_HISTORY_LIMIT)
                    honor.history.resize(HONOR_HISTORY_LIMIT);
            }
        }
    }

    SaveSeasonStatsUnlocked();
    SaveMatchmakingRatingsUnlocked();
    SaveCharacterStatsUnlocked();
    SaveMatchesUnlocked();
    SaveMatchPlayersUnlocked();
    if (isMatchmade)
    {
        if (!SaveHonorUnlocked())
        {
            for (const auto& previous : previousHonor) honor_[previous.first] = previous.second;
            std::cerr << "[HONOR SAVE FAILED] matchId=" << matchId << std::endl;
        }
        SaveHonorHistoryUnlocked();
    }

    std::cout << "[CAREER MATCH SAVED] matchId=" << matchId
        << " seasonId=" << seasonId
        << " players=" << validPlayers.size()
        << std::endl;

    return matchId;
}

void CareerManager::LoadUsersUnlocked()
{
    std::ifstream file("users.csv");
    if (!file.is_open())
        return;

    std::string line;
    if (!std::getline(file, line))
        return;

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() < 2 || fields[0].empty())
            continue;

        users_[fields[0]] = fields[1];
    }
}

void CareerManager::LoadSeasonsUnlocked()
{
    std::ifstream file(SEASONS_FILE);
    if (!file.is_open() || !ReadHeader(file, SEASONS_HEADER))
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != 5)
            continue;

        CareerSeason season{};
        int isActive = 0;
        if (!TryParseInt(fields[0], season.seasonId) ||
            !TryParseInt64(fields[2], season.startUnix) ||
            !TryParseInt64(fields[3], season.endUnix) ||
            !TryParseInt(fields[4], isActive) ||
            season.seasonId <= 0)
        {
            continue;
        }

        season.seasonName = fields[1];
        season.isActive = isActive != 0;
        seasons_[season.seasonId] = season;
    }
}

void CareerManager::LoadRankTiersUnlocked()
{
    std::ifstream file(RANK_TIERS_FILE);
    if (!file.is_open() || !ReadHeader(file, RANK_TIERS_HEADER))
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != 6)
            continue;

        CareerRankTier tier{};
        if (!TryParseInt(fields[0], tier.tierId) ||
            !TryParseInt(fields[2], tier.minPoints) ||
            !TryParseInt(fields[3], tier.maxPoints) ||
            !TryParseInt(fields[4], tier.rankImageId) ||
            !TryParseInt(fields[5], tier.leaderboardTop) ||
            tier.tierId < 0 ||
            tier.minPoints < 0 ||
            (tier.maxPoints >= 0 && tier.maxPoints < tier.minPoints) ||
            tier.leaderboardTop < 0)
        {
            continue;
        }

        tier.tierName = fields[1];
        rankTiers_[tier.tierId] = tier;
    }
}

void CareerManager::LoadSeasonStatsUnlocked()
{
    std::ifstream file(SEASON_STATS_FILE);
    if (!file.is_open() || !ReadHeader(file, SEASON_STATS_HEADER))
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != 9 || users_.count(fields[0]) == 0)
            continue;

        CareerSeasonStats stats{};
        stats.userId = fields[0];
        if (!TryParseInt(fields[1], stats.seasonId) ||
            !TryParseInt(fields[2], stats.rankPoints) ||
            !TryParseInt(fields[3], stats.rankedMatches) ||
            !TryParseInt(fields[4], stats.wins) ||
            !TryParseInt(fields[5], stats.roundsPlayed) ||
            !TryParseInt(fields[6], stats.kills) ||
            !TryParseInt(fields[7], stats.totalDamage) ||
            !TryParseInt(fields[8], stats.mvpCount) ||
            seasons_.count(stats.seasonId) == 0)
        {
            continue;
        }

        stats.rankPoints = std::max(0, stats.rankPoints);
        seasonStats_[MakeSeasonStatsKey(stats.userId, stats.seasonId)] = stats;
    }
}

void CareerManager::LoadMatchmakingRatingsUnlocked()
{
    std::ifstream file(MATCHMAKING_RATINGS_FILE);
    if (!file.is_open() ||
        !ReadHeader(file, MATCHMAKING_RATINGS_HEADER))
    {
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != 6 || users_.count(fields[0]) == 0)
            continue;

        CareerMatchmakingRating rating{};
        rating.userId = fields[0];
        if (!TryParseInt(fields[1], rating.queueType) ||
            !TryParseInt(fields[2], rating.matchmakingRating) ||
            !TryParseInt(fields[3], rating.ratingDeviation) ||
            !TryParseInt(fields[4], rating.matches) ||
            !TryParseInt(fields[5], rating.wins) ||
            (rating.queueType != 0 && rating.queueType != 1))
        {
            continue;
        }

        rating.ratingDeviation = std::clamp(
            rating.ratingDeviation,
            RankRatingCalculator::MIN_RATING_DEVIATION,
            RankRatingCalculator::DEFAULT_RATING_DEVIATION);
        rating.matches = std::max(0, rating.matches);
        rating.wins = std::clamp(rating.wins, 0, rating.matches);
        matchmakingRatings_[MakeMatchmakingRatingKey(
            rating.userId,
            rating.queueType)] = rating;
    }
}

void CareerManager::LoadCharacterStatsUnlocked()
{
    std::ifstream file(CHARACTER_STATS_FILE);
    if (!file.is_open() || !ReadHeader(file, CHARACTER_STATS_HEADER))
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != 6 || users_.count(fields[0]) == 0)
            continue;

        CareerCharacterStats stats{};
        stats.userId = fields[0];
        if (!TryParseInt(fields[1], stats.seasonId) ||
            !TryParseInt(fields[2], stats.characterId) ||
            !TryParseInt(fields[3], stats.masteryPoints) ||
            !TryParseInt(fields[4], stats.matches) ||
            !TryParseInt(fields[5], stats.wins) ||
            seasons_.count(stats.seasonId) == 0 ||
            stats.characterId < 0 ||
            stats.characterId >= CAREER_CHARACTER_COUNT ||
            stats.masteryPoints < 0 ||
            stats.matches < 0 ||
            stats.wins < 0 ||
            stats.wins > stats.matches)
        {
            continue;
        }

        characterStats_[MakeCharacterStatsKey(
            stats.userId,
            stats.seasonId,
            stats.characterId)] = stats;
    }
}

void CareerManager::LoadMatchesUnlocked()
{
    std::ifstream file(MATCHES_FILE);
    if (!file.is_open())
        return;

    std::string header;
    if (!std::getline(file, header))
        return;
    if (!header.empty() && header.back() == '\r')
        header.pop_back();

    bool hasMatchType = header == MATCHES_HEADER;
    if (!hasMatchType && header != LEGACY_MATCHES_HEADER)
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != (hasMatchType ? 9u : 8u))
            continue;

        CareerMatch match{};
        if (!TryParseUInt64(fields[0], match.matchId) ||
            !TryParseInt(fields[1], match.seasonId) ||
            !TryParseInt64(fields[3], match.startedAtUnix) ||
            !TryParseInt(fields[4], match.durationSeconds) ||
            !TryParseInt(fields[5], match.scoreA) ||
            !TryParseInt(fields[6], match.scoreB) ||
            !TryParseInt(fields[7], match.winnerTeam) ||
            match.matchId == 0 ||
            seasons_.count(match.seasonId) == 0)
        {
            continue;
        }

        if (hasMatchType &&
            (!TryParseInt(fields[8], match.matchType) ||
                (match.matchType < 0 || match.matchType > 2)))
        {
            continue;
        }
        if (!hasMatchType)
            match.matchType = 1;

        match.mapName = fields[2];
        matches_[match.matchId] = match;
        nextMatchId_ = std::max(nextMatchId_, match.matchId + 1);
    }
}

void CareerManager::LoadMatchPlayersUnlocked()
{
    std::ifstream file(MATCH_PLAYERS_FILE);
    if (!file.is_open())
        return;

    std::string header;
    if (!std::getline(file, header))
        return;
    if (!header.empty() && header.back() == '\r')
        header.pop_back();
    bool legacy = header == LEGACY_MATCH_PLAYERS_HEADER;
    bool previous = header == PREVIOUS_MATCH_PLAYERS_HEADER;
    if (!legacy && !previous && header != MATCH_PLAYERS_HEADER)
        return;

    std::string line;
    std::unordered_set<std::string> uniqueRows;

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ParseCsvLine(line);
        if ((fields.size() != 13 && fields.size() != 16 && fields.size() != 17) ||
            users_.count(fields[1]) == 0)
            continue;

        CareerMatchPlayer player{};
        int isMvp = 0;
        if (!TryParseUInt64(fields[0], player.matchId) ||
            !TryParseInt(fields[2], player.team) ||
            !TryParseInt(fields[3], player.characterId) ||
            !TryParseInt(fields[4], player.combatScore) ||
            !TryParseInt(fields[5], player.attackScore) ||
            !TryParseInt(fields[6], player.skillScore) ||
            !TryParseInt(fields[7], player.teamworkScore) ||
            !TryParseInt(fields[8], player.kills) ||
            !TryParseInt(fields[9], player.deaths) ||
            !TryParseInt(fields[10], player.assists) ||
            !TryParseInt(fields[11], player.damage) ||
            !TryParseInt(fields[12], isMvp) ||
            matches_.count(player.matchId) == 0)
        {
            continue;
        }

        player.userId = fields[1];
        player.isMvp = isMvp != 0;
        if (!legacy && fields.size() >= 16)
        {
            TryParseInt(fields[13], player.headshotKills);
            TryParseInt(fields[14], player.spikePlants);
            TryParseInt(fields[15], player.spikeDefuses);
            if (fields.size() >= 17)
                TryParseInt(fields[16], player.meleeKills);
        }
        std::string key = std::to_string(player.matchId) + "\x1f" + player.userId;
        if (uniqueRows.insert(key).second)
            matchPlayers_.push_back(player);
    }
}

void CareerManager::LoadItemsUnlocked()
{
    std::ifstream file(ITEMS_FILE);
    if (!file.is_open() || !ReadHeader(file, ITEMS_HEADER))
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != 4)
            continue;

        CareerItem item{};
        if (!TryParseInt(fields[0], item.itemId) ||
            !TryParseInt(fields[3], item.iconId) ||
            item.itemId <= 0)
        {
            continue;
        }

        item.itemName = fields[1];
        item.description = fields[2];
        items_[item.itemId] = item;
    }
}

void CareerManager::LoadInventoryUnlocked()
{
    std::ifstream file(INVENTORY_FILE);
    if (!file.is_open() || !ReadHeader(file, INVENTORY_HEADER))
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != 4 || users_.count(fields[0]) == 0)
            continue;

        int itemId = 0;
        CareerInventoryEntry entry{};
        if (!TryParseInt(fields[1], itemId) ||
            !TryParseInt(fields[2], entry.quantity) ||
            !TryParseInt64(fields[3], entry.acquiredAtUnix) ||
            items_.count(itemId) == 0 ||
            entry.quantity <= 0)
        {
            continue;
        }

        entry.item = items_[itemId];
        inventory_[MakeInventoryKey(fields[0], itemId)] = entry;
    }
}

void CareerManager::LoadWalletsUnlocked()
{
    std::ifstream file(WALLETS_FILE);
    if (!file.is_open())
        return;

    std::string header;
    if (!std::getline(file, header)) return;
    if (!header.empty() && header.back() == '\r') header.pop_back();
    const bool legacy = header == LEGACY_WALLETS_HEADER;
    const bool previous = header == PREVIOUS_WALLETS_HEADER;
    if (!legacy && !previous && header != WALLETS_HEADER) return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::vector<std::string> fields = ParseCsvLine(line);
        if (fields.size() != (legacy ? 3u : previous ? 4u : 5u) ||
            users_.count(fields[0]) == 0)
            continue;

        CareerWallet wallet{};
        wallet.userId = fields[0];
        if (!TryParseInt(fields[1], wallet.freeCurrency) ||
            !TryParseInt(fields[2], wallet.victoryCurrency) ||
            wallet.freeCurrency < 0 ||
            wallet.victoryCurrency < 0)
        {
            continue;
        }

        if (!legacy &&
            (!TryParseInt(fields[3], wallet.claimedLevelReward) ||
             wallet.claimedLevelReward < 0 || wallet.claimedLevelReward > 100))
            continue;

        if (!legacy && !previous &&
            (!TryParseInt(fields[4], wallet.claimedHonorGrade) ||
             wallet.claimedHonorGrade < 0 || wallet.claimedHonorGrade > 6))
            continue;

        wallets_[wallet.userId] = wallet;
    }
}

void CareerManager::LoadHonorUnlocked()
{
    std::ifstream file(HONOR_FILE);
    std::string line;
    bool hasHeader = file.is_open() && static_cast<bool>(std::getline(file, line));
    if (hasHeader && !line.empty() && line.back() == '\r') line.pop_back();
    const bool snapshot = hasHeader && line == HONOR_HEADER;
    if (hasHeader && (snapshot || line == LEGACY_HONOR_HEADER))
    {
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto fields = ParseCsvLine(line);
            if (fields.size() != (snapshot ? 5 : 4) || users_.count(fields[0]) == 0) continue;
            CareerHonorState state{};
            if (!TryParseInt(fields[1], state.score) ||
                !TryParseInt(fields[2], state.dailyEarned) ||
                !TryParseInt64(fields[3], state.earnedDayUtc) ||
                state.score < 0 || state.score > HONOR_MAX_SCORE ||
                state.dailyEarned < 0 || state.dailyEarned > HONOR_DAILY_GAIN_CAP)
                continue;
            if (snapshot)
            {
                std::istringstream entries(fields[4]);
                std::string entry;
                while (state.history.size() < HONOR_HISTORY_LIMIT && std::getline(entries, entry, ';'))
                {
                    std::replace(entry.begin(), entry.end(), ':', ',');
                    auto values = ParseCsvLine(entry);
                    CareerHonorChange change{};
                    int reason = 0;
                    if (values.size() != 4 || !TryParseInt64(values[0], change.appliedAtUnix) ||
                        !TryParseInt(values[1], change.delta) || !TryParseInt(values[2], reason) ||
                        !TryParseUInt64(values[3], change.matchId) || reason < 1 || reason > 6) continue;
                    change.reason = static_cast<HonorChangeReason>(reason);
                    state.history.push_back(change);
                }
            }
            honor_[fields[0]] = state;
        }
    }
    if (snapshot) return;
    std::ifstream history(HONOR_HISTORY_FILE);
    if (!history.is_open() || !std::getline(history, line)) return;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != HONOR_HISTORY_HEADER) return;
    while (std::getline(history, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fields = ParseCsvLine(line);
        if (fields.size() != 5 || users_.count(fields[0]) == 0) continue;
        CareerHonorChange change{};
        int reason = 0;
        if (!TryParseInt64(fields[1], change.appliedAtUnix) ||
            !TryParseInt(fields[2], change.delta) ||
            !TryParseInt(fields[3], reason) ||
            !TryParseUInt64(fields[4], change.matchId) ||
            reason < 1 || reason > 6) continue;
        change.reason = static_cast<HonorChangeReason>(reason);
        auto& entries = honor_[fields[0]].history;
        if (entries.size() < HONOR_HISTORY_LIMIT) entries.push_back(change);
    }
}

void CareerManager::SaveSeasonsUnlocked()
{
    std::ofstream file(SEASONS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << SEASONS_HEADER << '\n';
    std::vector<int> ids;
    for (const auto& pair : seasons_)
        ids.push_back(pair.first);
    std::sort(ids.begin(), ids.end());

    for (int id : ids)
    {
        const CareerSeason& season = seasons_[id];
        file << season.seasonId << ','
            << EscapeCsv(season.seasonName) << ','
            << season.startUnix << ','
            << season.endUnix << ','
            << (season.isActive ? 1 : 0) << '\n';
    }
}

void CareerManager::SaveRankTiersUnlocked()
{
    std::ofstream file(RANK_TIERS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << RANK_TIERS_HEADER << '\n';
    std::vector<int> ids;
    for (const auto& pair : rankTiers_)
        ids.push_back(pair.first);
    std::sort(ids.begin(), ids.end());

    for (int id : ids)
    {
        const CareerRankTier& tier = rankTiers_[id];
        file << tier.tierId << ','
            << EscapeCsv(tier.tierName) << ','
            << tier.minPoints << ','
            << tier.maxPoints << ','
            << tier.rankImageId << ','
            << tier.leaderboardTop << '\n';
    }
}

void CareerManager::SaveSeasonStatsUnlocked()
{
    std::ofstream file(SEASON_STATS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << SEASON_STATS_HEADER << '\n';
    std::vector<CareerSeasonStats> rows;
    for (const auto& pair : seasonStats_)
        rows.push_back(pair.second);
    std::sort(
        rows.begin(),
        rows.end(),
        [](const CareerSeasonStats& lhs, const CareerSeasonStats& rhs)
        {
            if (lhs.userId != rhs.userId)
                return lhs.userId < rhs.userId;
            return lhs.seasonId < rhs.seasonId;
        });

    for (const CareerSeasonStats& stats : rows)
    {
        file << EscapeCsv(stats.userId) << ','
            << stats.seasonId << ','
            << stats.rankPoints << ','
            << stats.rankedMatches << ','
            << stats.wins << ','
            << stats.roundsPlayed << ','
            << stats.kills << ','
            << stats.totalDamage << ','
            << stats.mvpCount << '\n';
    }
}

void CareerManager::SaveMatchmakingRatingsUnlocked()
{
    std::ofstream file(MATCHMAKING_RATINGS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << MATCHMAKING_RATINGS_HEADER << '\n';
    std::vector<CareerMatchmakingRating> rows;
    rows.reserve(matchmakingRatings_.size());
    for (const auto& pair : matchmakingRatings_)
        rows.push_back(pair.second);

    std::sort(
        rows.begin(),
        rows.end(),
        [](const CareerMatchmakingRating& lhs,
            const CareerMatchmakingRating& rhs)
        {
            if (lhs.userId != rhs.userId)
                return lhs.userId < rhs.userId;
            return lhs.queueType < rhs.queueType;
        });

    for (const CareerMatchmakingRating& rating : rows)
    {
        file << EscapeCsv(rating.userId) << ','
            << rating.queueType << ','
            << rating.matchmakingRating << ','
            << rating.ratingDeviation << ','
            << rating.matches << ','
            << rating.wins << '\n';
    }
}

void CareerManager::SaveCharacterStatsUnlocked()
{
    std::ofstream file(CHARACTER_STATS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << CHARACTER_STATS_HEADER << '\n';
    std::vector<CareerCharacterStats> rows;
    rows.reserve(characterStats_.size());
    for (const auto& pair : characterStats_)
        rows.push_back(pair.second);

    std::sort(
        rows.begin(),
        rows.end(),
        [](const CareerCharacterStats& lhs,
            const CareerCharacterStats& rhs)
        {
            if (lhs.userId != rhs.userId)
                return lhs.userId < rhs.userId;
            if (lhs.seasonId != rhs.seasonId)
                return lhs.seasonId < rhs.seasonId;
            return lhs.characterId < rhs.characterId;
        });

    for (const CareerCharacterStats& stats : rows)
    {
        file << EscapeCsv(stats.userId) << ','
            << stats.seasonId << ','
            << stats.characterId << ','
            << stats.masteryPoints << ','
            << stats.matches << ','
            << stats.wins << '\n';
    }
}

void CareerManager::SaveMatchesUnlocked()
{
    std::ofstream file(MATCHES_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << MATCHES_HEADER << '\n';
    std::vector<uint64_t> ids;
    for (const auto& pair : matches_)
        ids.push_back(pair.first);
    std::sort(ids.begin(), ids.end());

    for (uint64_t id : ids)
    {
        const CareerMatch& match = matches_[id];
        file << match.matchId << ','
            << match.seasonId << ','
            << EscapeCsv(match.mapName) << ','
            << match.startedAtUnix << ','
            << match.durationSeconds << ','
            << match.scoreA << ','
            << match.scoreB << ','
            << match.winnerTeam << ','
            << match.matchType << '\n';
    }
}

void CareerManager::SaveMatchPlayersUnlocked()
{
    std::ofstream file(MATCH_PLAYERS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << MATCH_PLAYERS_HEADER << '\n';
    std::vector<CareerMatchPlayer> rows = matchPlayers_;
    std::sort(
        rows.begin(),
        rows.end(),
        [](const CareerMatchPlayer& lhs, const CareerMatchPlayer& rhs)
        {
            if (lhs.matchId != rhs.matchId)
                return lhs.matchId < rhs.matchId;
            return lhs.userId < rhs.userId;
        });

    for (const CareerMatchPlayer& player : rows)
    {
        file << player.matchId << ','
            << EscapeCsv(player.userId) << ','
            << player.team << ','
            << player.characterId << ','
            << player.combatScore << ','
            << player.attackScore << ','
            << player.skillScore << ','
            << player.teamworkScore << ','
            << player.kills << ','
            << player.deaths << ','
            << player.assists << ','
            << player.damage << ','
            << (player.isMvp ? 1 : 0) << ','
            << player.headshotKills << ','
            << player.spikePlants << ','
            << player.spikeDefuses << ','
            << player.meleeKills << '\n';
    }
}

void CareerManager::SaveItemsUnlocked()
{
    std::ofstream file(ITEMS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << ITEMS_HEADER << '\n';
    std::vector<int> ids;
    for (const auto& pair : items_)
        ids.push_back(pair.first);
    std::sort(ids.begin(), ids.end());

    for (int id : ids)
    {
        const CareerItem& item = items_[id];
        file << item.itemId << ','
            << EscapeCsv(item.itemName) << ','
            << EscapeCsv(item.description) << ','
            << item.iconId << '\n';
    }
}

void CareerManager::SaveInventoryUnlocked()
{
    std::ofstream file(INVENTORY_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << INVENTORY_HEADER << '\n';
    std::vector<std::pair<std::string, CareerInventoryEntry>> rows;
    for (const auto& pair : inventory_)
        rows.push_back(pair);
    std::sort(
        rows.begin(),
        rows.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return lhs.first < rhs.first;
        });

    for (const auto& row : rows)
    {
        size_t separator = row.first.find('\x1f');
        if (separator == std::string::npos)
            continue;

        std::string userId = row.first.substr(0, separator);
        file << EscapeCsv(userId) << ','
            << row.second.item.itemId << ','
            << row.second.quantity << ','
            << row.second.acquiredAtUnix << '\n';
    }
}

bool CareerManager::SaveWalletsUnlocked()
{
    constexpr const char* temporaryFile = "career_user_wallet.csv.tmp";
    std::ofstream file(temporaryFile, std::ios::trunc);
    if (!file.is_open())
        return false;

    file << WALLETS_HEADER << '\n';
    std::vector<std::string> userIds;
    userIds.reserve(wallets_.size());
    for (const auto& pair : wallets_)
        userIds.push_back(pair.first);
    std::sort(userIds.begin(), userIds.end());

    for (const std::string& userId : userIds)
    {
        const CareerWallet& wallet = wallets_[userId];
        file << EscapeCsv(userId) << ','
            << wallet.freeCurrency << ','
            << wallet.victoryCurrency << ','
            << wallet.claimedLevelReward << ','
            << wallet.claimedHonorGrade << '\n';
    }
    file.flush();
    if (!file.good()) return false;
    file.close();
    return MoveFileExA(temporaryFile, WALLETS_FILE,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool CareerManager::SaveHonorUnlocked()
{
    constexpr const char* temporaryFile = "career_user_honor.csv.tmp";
    std::ofstream file(temporaryFile, std::ios::trunc);
    if (!file.is_open()) return false;
    file << HONOR_HEADER << '\n';
    std::vector<std::string> users;
    for (const auto& pair : honor_) users.push_back(pair.first);
    std::sort(users.begin(), users.end());
    for (const auto& user : users)
    {
        const auto& state = honor_.at(user);
        file << EscapeCsv(user) << ',' << state.score << ','
            << state.dailyEarned << ',' << state.earnedDayUtc << ',';
        for (const auto& change : state.history)
            file << change.appliedAtUnix << ':' << change.delta << ':'
                << static_cast<int>(change.reason) << ':' << change.matchId << ';';
        file << '\n';
    }
    file.flush();
    if (!file.good()) return false;
    file.close();
    return MoveFileExA(temporaryFile, HONOR_FILE,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool CareerManager::SaveHonorHistoryUnlocked()
{
    constexpr const char* temporaryFile = "career_honor_history.csv.tmp";
    std::ofstream file(temporaryFile, std::ios::trunc);
    if (!file.is_open()) return false;
    file << HONOR_HISTORY_HEADER << '\n';
    std::vector<std::string> users;
    for (const auto& pair : honor_) users.push_back(pair.first);
    std::sort(users.begin(), users.end());
    for (const auto& user : users)
        for (const auto& change : honor_.at(user).history)
            file << EscapeCsv(user) << ',' << change.appliedAtUnix << ','
                << change.delta << ',' << static_cast<int>(change.reason)
                << ',' << change.matchId << '\n';
    file.flush();
    if (!file.good()) return false;
    file.close();
    return MoveFileExA(temporaryFile, HONOR_HISTORY_FILE,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

void CareerManager::SeedDefaultSeasonUnlocked()
{
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    constexpr int64_t secondsPerDay = 24 * 60 * 60;

    CareerSeason season{};
    season.seasonId = 0;
    season.seasonName = "시즌0";
    season.startUnix = now - 30 * secondsPerDay;
    season.endUnix = now + 90 * secondsPerDay;
    season.isActive = true;
    seasons_[season.seasonId] = season;
}

void CareerManager::SeedDefaultRankTiersUnlocked()
{
    const char* groupNames[] =
    {
        "Bronze",
        "Silver",
        "Gold",
        "Platinum",
        "Diamond"
    };

    int tierId = 0;
    for (int groupIndex = 0; groupIndex < 5; ++groupIndex)
    {
        for (int division = 5; division >= 1; --division)
        {
            CareerRankTier tier{};
            tier.tierId = tierId;
            tier.tierName = std::string(groupNames[groupIndex]) +
                " " + std::to_string(division);
            tier.minPoints = tierId * 100;
            tier.maxPoints = tier.minPoints + 99;
            tier.rankImageId = groupIndex;

            if (groupIndex == 4 && division == 1)
                tier.maxPoints = -1;

            rankTiers_[tier.tierId] = tier;
            ++tierId;
        }
    }

    CareerRankTier master{};
    master.tierId = tierId;
    master.tierName = "Master";
    master.minPoints = 2400;
    master.maxPoints = -1;
    master.rankImageId = 5;
    master.leaderboardTop = 30;
    rankTiers_[master.tierId] = master;
}

CareerRankTier CareerManager::FindBaseTierUnlocked(int rankPoints) const
{
    CareerRankTier result{};
    bool found = false;

    for (const auto& pair : rankTiers_)
    {
        const CareerRankTier& tier = pair.second;
        if (tier.leaderboardTop > 0)
            continue;

        bool isWithinRange =
            rankPoints >= tier.minPoints &&
            (tier.maxPoints < 0 || rankPoints <= tier.maxPoints);

        if (isWithinRange)
            return tier;

        if ((!found || tier.minPoints > result.minPoints) &&
            rankPoints >= tier.minPoints)
        {
            result = tier;
            found = true;
        }
    }

    if (found)
        return result;

    for (const auto& pair : rankTiers_)
    {
        const CareerRankTier& tier = pair.second;
        if (tier.leaderboardTop == 0 &&
            (!found || tier.minPoints < result.minPoints))
        {
            result = tier;
            found = true;
        }
    }

    return result;
}

CareerRankTier CareerManager::ResolveDisplayedTierUnlocked(
    const CareerSeasonStats& stats) const
{
    CareerRankTier result = FindBaseTierUnlocked(stats.rankPoints);

    for (const auto& pair : rankTiers_)
    {
        const CareerRankTier& tier = pair.second;
        if (tier.leaderboardTop <= 0)
            continue;

        bool isWithinRange =
            stats.rankPoints >= tier.minPoints &&
            (tier.maxPoints < 0 || stats.rankPoints <= tier.maxPoints);

        if (isWithinRange && IsLeaderboardTierUnlocked(stats, tier))
            return tier;
    }

    return result;
}

bool CareerManager::IsLeaderboardTierUnlocked(
    const CareerSeasonStats& stats,
    const CareerRankTier& tier) const
{
    if (tier.leaderboardTop <= 0)
        return false;

    size_t position = 1;
    for (const auto& pair : seasonStats_)
    {
        const CareerSeasonStats& candidate = pair.second;
        if (candidate.seasonId != stats.seasonId ||
            candidate.userId == stats.userId ||
            users_.count(candidate.userId) == 0 ||
            candidate.rankPoints < tier.minPoints ||
            (tier.maxPoints >= 0 && candidate.rankPoints > tier.maxPoints))
        {
            continue;
        }

        if (RanksBefore(candidate, stats))
            ++position;
    }

    return position <= static_cast<size_t>(tier.leaderboardTop);
}

int CareerManager::GetActiveSeasonIdUnlocked() const
{
    int activeSeasonId = 0;
    for (const auto& pair : seasons_)
    {
        if (pair.second.isActive && pair.first > activeSeasonId)
            activeSeasonId = pair.first;
    }

    if (activeSeasonId != 0)
        return activeSeasonId;

    for (const auto& pair : seasons_)
        activeSeasonId = std::max(activeSeasonId, pair.first);

    return activeSeasonId;
}

std::vector<CareerRankingEntry>
CareerManager::BuildOverallRankingsUnlocked()
{
    int seasonId = GetActiveSeasonIdUnlocked();
    std::vector<CareerSeasonStats> stats;

    for (const auto& pair : seasonStats_)
    {
        if (pair.second.seasonId == seasonId &&
            users_.count(pair.second.userId) != 0)
        {
            stats.push_back(pair.second);
        }
    }

    std::sort(stats.begin(), stats.end(), RanksBefore);

    std::vector<CareerRankingEntry> result;
    result.reserve(stats.size());
    for (size_t i = 0; i < stats.size(); ++i)
    {
        CareerRankingEntry entry{};
        entry.position = static_cast<int>(i + 1);
        entry.userId = stats[i].userId;
        entry.nickname = users_[stats[i].userId];
        entry.tier = ResolveDisplayedTierUnlocked(stats[i]);
        entry.stats = stats[i];
        entry.score = stats[i].rankPoints;
        entry.matches = stats[i].rankedMatches;
        entry.wins = stats[i].wins;
        result.push_back(entry);
    }

    return result;
}

std::vector<CareerRankingEntry>
CareerManager::BuildCharacterRankingsUnlocked(int characterId)
{
    int seasonId = GetActiveSeasonIdUnlocked();
    std::vector<CareerRankingEntry> result;
    result.reserve(users_.size());

    for (const auto& user : users_)
    {
        CareerCharacterStats stats{};
        auto statsIt = characterStats_.find(
            MakeCharacterStatsKey(user.first, seasonId, characterId));
        if (statsIt != characterStats_.end())
            stats = statsIt->second;

        CareerRankingEntry entry{};
        entry.userId = user.first;
        entry.nickname = user.second;
        entry.characterId = characterId;
        entry.score = stats.masteryPoints;
        entry.matches = stats.matches;
        entry.wins = stats.wins;
        result.push_back(entry);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const CareerRankingEntry& lhs, const CareerRankingEntry& rhs)
        {
            if (lhs.score != rhs.score)
                return lhs.score > rhs.score;
            if (lhs.wins != rhs.wins)
                return lhs.wins > rhs.wins;
            if (lhs.matches != rhs.matches)
                return lhs.matches < rhs.matches;
            return lhs.userId < rhs.userId;
        });

    for (size_t i = 0; i < result.size(); ++i)
        result[i].position = static_cast<int>(i + 1);

    return result;
}

CareerSeasonStats& CareerManager::EnsureSeasonStatsUnlocked(
    const std::string& userId,
    int seasonId)
{
    std::string key = MakeSeasonStatsKey(userId, seasonId);
    auto it = seasonStats_.find(key);
    if (it != seasonStats_.end())
        return it->second;

    CareerSeasonStats stats{};
    stats.userId = userId;
    stats.seasonId = seasonId;
    return seasonStats_.emplace(key, stats).first->second;
}

CareerMatchmakingRating& CareerManager::EnsureMatchmakingRatingUnlocked(
    const std::string& userId,
    int queueType,
    int initialRating)
{
    queueType = queueType == 1 ? 1 : 0;
    std::string key = MakeMatchmakingRatingKey(userId, queueType);
    auto it = matchmakingRatings_.find(key);
    if (it != matchmakingRatings_.end())
        return it->second;

    CareerMatchmakingRating rating{};
    rating.userId = userId;
    rating.queueType = queueType;
    rating.matchmakingRating = initialRating;
    rating.ratingDeviation =
        RankRatingCalculator::DEFAULT_RATING_DEVIATION;
    return matchmakingRatings_.emplace(key, rating).first->second;
}

CareerCharacterStats& CareerManager::EnsureCharacterStatsUnlocked(
    const std::string& userId,
    int seasonId,
    int characterId)
{
    std::string key = MakeCharacterStatsKey(
        userId,
        seasonId,
        characterId);
    auto it = characterStats_.find(key);
    if (it != characterStats_.end())
        return it->second;

    CareerCharacterStats stats{};
    stats.userId = userId;
    stats.seasonId = seasonId;
    stats.characterId = characterId;
    return characterStats_.emplace(key, stats).first->second;
}

std::string CareerManager::MakeSeasonStatsKey(
    const std::string& userId,
    int seasonId)
{
    return userId + "\x1f" + std::to_string(seasonId);
}

std::string CareerManager::MakeMatchmakingRatingKey(
    const std::string& userId,
    int queueType)
{
    return userId + "\x1f" + std::to_string(queueType == 1 ? 1 : 0);
}

std::string CareerManager::MakeInventoryKey(
    const std::string& userId,
    int itemId)
{
    return userId + "\x1f" + std::to_string(itemId);
}

std::string CareerManager::MakeCharacterStatsKey(
    const std::string& userId,
    int seasonId,
    int characterId)
{
    return userId + "\x1f" +
        std::to_string(seasonId) + "\x1f" +
        std::to_string(characterId);
}
