#include "ProfilePacketService.h"
#include "CareerManager.h"
#include "Packet.h"
#include "PacketType.h"
#include "ProfileManager.h"
#include "Session.h"
#include "UserManager.h"
#include "WeaponSkinManager.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace
{
std::string ReadFixed(const char* value, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && value[length] != '\0') ++length;
    return std::string(value, length);
}

void WriteChipCraftPreset(const ChipCraftPreset& source, ChipCraftPresetPacket& target)
{
    target.cardId = source.cardId;
    target.caseId = source.caseId;
    target.medalCount = source.medalCount;
    for (size_t i = 0; i < source.medals.size(); ++i)
    {
        target.medals[i].medalId = source.medals[i].id;
        target.medals[i].x = source.medals[i].x;
        target.medals[i].y = source.medals[i].y;
        target.medals[i].rotation = source.medals[i].rotation;
        target.medals[i].scale = source.medals[i].scale;
        target.medals[i].flipped = source.medals[i].flipped ? 1 : 0;
    }
}

ChipCraftPreset ReadChipCraftPreset(const ChipCraftPresetPacket& source)
{
    ChipCraftPreset target{};
    target.cardId = source.cardId;
    target.caseId = source.caseId;
    target.medalCount = source.medalCount;
    for (size_t i = 0; i < target.medals.size(); ++i)
    {
        target.medals[i].id = source.medals[i].medalId;
        target.medals[i].x = source.medals[i].x;
        target.medals[i].y = source.medals[i].y;
        target.medals[i].rotation = source.medals[i].rotation;
        target.medals[i].scale = source.medals[i].scale;
        target.medals[i].flipped = source.medals[i].flipped != 0;
    }
    return target;
}

void SendChipCraft(Session* session, const std::string& requestedUserId)
{
    const std::string targetId = requestedUserId.empty() ? session->GetUserId() : requestedUserId;
    SC_ChipCraft out{};
    out.header.size = sizeof(out);
    out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CHIPCRAFT);
    ChipCraftRecord record{};
    PlayerData player{};
    if (!UserManager::Instance().GetPlayerData(targetId, player) ||
        !ProfileManager::Instance().GetChipCraft(targetId, record))
    {
        out.result = 1;
    }
    else
    {
        out.isSelf = targetId == session->GetUserId() ? 1 : 0;
        strncpy_s(out.userId, sizeof(out.userId), targetId.c_str(), _TRUNCATE);
        // Inventory is needed only for the owner's editing screen.
        if (out.isSelf)
        {
            out.ownedCardMask = record.ownedCardMask;
            out.ownedCaseMask = record.ownedCaseMask;
            out.ownedMedalMask = record.ownedMedalMask;
        }
        WriteChipCraftPreset(record.preset, out.preset);
    }
    session->Send(&out);
}

void SendLevelRewardState(Session* session)
{
    SC_LevelRewardState out{};
    out.header.size = sizeof(out);
    out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_LEVEL_REWARD_STATE);
    PlayerData player{};
    CareerWallet wallet{};
    if (UserManager::Instance().GetPlayerData(session->GetUserId(), player) &&
        CareerManager::Instance().GetWallet(session->GetUserId(), wallet))
    {
        out.result = 0;
        out.level = player.level;
        out.currentExp = player.currentExp;
        out.requiredExp = UserManager::Instance().GetRequiredExpForLevel(player.level);
        out.claimedLevel = wallet.claimedLevelReward;
        out.freeCurrency = wallet.freeCurrency;
        out.victoryCurrency = wallet.victoryCurrency;
    }
    else out.result = 1;
    session->Send(&out);
}

void SendHonorState(Session* session, uint8_t requestedPage)
{
    static_assert(sizeof(SC_HonorState) == 154, "Honor state wire layout changed");
    static_assert(sizeof(CS_HonorRequest) == 5, "Honor request wire layout changed");
    static_assert(sizeof(SC_HonorClaimResult) == 25, "Honor claim wire layout changed");
    SC_HonorState out{};
    out.header.size = sizeof(out);
    out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_HONOR_STATE);
    CareerHonorState state{};
    CareerWallet wallet{};
    const std::string& userId = session->GetUserId();
    if (!CareerManager::Instance().GetHonorState(userId, state) ||
        !CareerManager::Instance().GetWallet(userId, wallet))
        out.result = 1;
    else
    {
        out.score = state.score;
        out.claimedGrade = state.claimedGrade;
        out.freeCurrency = wallet.freeCurrency;
        out.victoryCurrency = wallet.victoryCurrency;
        out.pageCount = static_cast<uint8_t>(std::max<size_t>(1,
            (state.history.size() + 9) / 10));
        out.page = std::min<uint8_t>(requestedPage, out.pageCount);
        if (out.page == 0) out.page = 1;
        const size_t first = static_cast<size_t>(out.page - 1) * 10;
        for (size_t index = first;
            index < state.history.size() && out.entryCount < 10; ++index)
        {
            const auto& change = state.history[index];
            auto& destination = out.entries[out.entryCount++];
            destination.appliedAtUnix = change.appliedAtUnix;
            destination.delta = change.delta;
            destination.reason = static_cast<uint8_t>(change.reason);
        }
    }
    session->Send(&out);
}

void SendProfile(Session* session, const std::string& requestedUserId)
{
    const std::string requesterId = session->GetUserId();
    const std::string targetId = requestedUserId.empty() ? requesterId : requestedUserId;
    PlayerData player{};
    ProfileSettings settings{};
    SC_Profile out{};
    out.header.size = sizeof(out);
    out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PROFILE);

    if (!UserManager::Instance().GetPlayerData(targetId, player) ||
        !ProfileManager::Instance().GetSettings(targetId, settings))
    {
        out.result = 1;
        session->Send(&out);
        return;
    }

    const bool isSelf = requesterId == targetId;
    if (!isSelf) ProfileManager::Instance().RecordVisit(targetId, requesterId);
    out.result = 0;
    out.isSelf = isSelf ? 1 : 0;
    out.privacyEnabled = settings.privacyEnabled ? 1 : 0;
    strncpy_s(out.userId, sizeof(out.userId), player.id.c_str(), _TRUNCATE);
    strncpy_s(out.nickname, sizeof(out.nickname), player.nickname.c_str(), _TRUNCATE);
    out.level = player.level;
    out.playerIconId = player.playerIconId;
    out.logoBorderId = player.logoBorderId;
    strncpy_s(out.joinDate, sizeof(out.joinDate), settings.joinDate.c_str(), _TRUNCATE);
    strncpy_s(out.region, sizeof(out.region), "\xEB\x8C\x80\xED\x95\x9C\xEB\xAF\xBC\xEA\xB5\xAD", _TRUNCATE);
    strncpy_s(out.birthday, sizeof(out.birthday), "----", _TRUNCATE);
    out.likeCount = settings.likeCount;

    CareerOverview overview{};
    if (CareerManager::Instance().GetOverview(targetId, overview))
    {
        out.seasonId = overview.season.seasonId;
        out.currentRankImageId = overview.tier.rankImageId;
        out.seasonHighestRankImageId = overview.tier.rankImageId;
        out.allTimeHighestRankImageId = overview.tier.rankImageId;
        out.allTimeHighestSeasonId = overview.season.seasonId;
        strncpy_s(out.currentTierName, sizeof(out.currentTierName), overview.tier.tierName.c_str(), _TRUNCATE);
        out.playCount = overview.stats.rankedMatches;
        out.wins = overview.stats.wins;
        out.roundsPlayed = overview.stats.roundsPlayed;
        out.totalKills = overview.stats.kills;
        out.totalDamage = overview.stats.totalDamage;
        out.mvpCount = overview.stats.mvpCount;
    }

    std::array<int, PROFILE_CHARACTER_COUNT> maxKills{};
    std::vector<CareerMatchHistoryEntry> history = CareerManager::Instance().GetMatchHistory(targetId, 0, 10000);
    for (const CareerMatchHistoryEntry& match : history)
    {
        for (const CareerMatchPlayer& entry : match.players)
        {
            if (entry.userId != targetId) continue;
            out.totalAssists += entry.assists;
            out.headshotKills += entry.headshotKills;
            out.spikePlants += entry.spikePlants;
            out.spikeDefuses += entry.spikeDefuses;
            if (entry.characterId >= 0 && entry.characterId < PROFILE_CHARACTER_COUNT)
                maxKills[entry.characterId] = std::max(maxKills[entry.characterId], entry.kills);
            break;
        }
    }

    CharacterOwnedData owned{};
    const bool hasOwnedData = UserManager::Instance().GetCharacterOwnedData(targetId, owned);
    CareerRankingPage characterPage;
    for (int characterId = 0; characterId < PROFILE_CHARACTER_COUNT; ++characterId)
    {
        ProfileCharacterPacketEntry& destination = out.characters[characterId];
        destination.characterId = characterId;
        if (hasOwnedData)
        {
            const int ownedFlags[] = { owned.c0, owned.c1, owned.c2, owned.c3 };
            destination.owned = ownedFlags[characterId] != 0 ? 1 : 0;
        }
        characterPage = CareerManager::Instance().GetRankingPage(1, characterId, 1, 10000, targetId);
        for (const CareerRankingEntry& entry : characterPage.entries)
        {
            if (entry.userId == targetId)
            {
                destination.matches = entry.matches;
                destination.wins = entry.wins;
                break;
            }
        }
        destination.maxKills = maxKills[characterId];
    }

    out.weaponSkinOwnedCount = WeaponSkinManager::Instance().GetOwnedSkinCount(targetId);
    out.weaponSkinTotalCount = WeaponSkinManager::Instance().GetDefinitionCount();
    if (hasOwnedData)
    {
        int ownedCount = (owned.c0 ? 1 : 0) + (owned.c1 ? 1 : 0) + (owned.c2 ? 1 : 0) + (owned.c3 ? 1 : 0);
        out.characterSkinOwnedCount = ownedCount * 3;
    }
    out.characterSkinTotalCount = PROFILE_CHARACTER_COUNT * 3;
    std::copy(settings.battleStatIds.begin(), settings.battleStatIds.end(), out.battleStatIds);

    if (isSelf)
    {
        std::vector<ProfileVisitor> visitors = ProfileManager::Instance().GetRecentVisitors(targetId, PROFILE_VISITOR_COUNT);
        out.visitorCount = static_cast<uint8_t>(visitors.size());
        for (size_t i = 0; i < visitors.size(); ++i)
        {
            PlayerData visitorData{};
            if (!UserManager::Instance().GetPlayerData(visitors[i].userId, visitorData)) continue;
            strncpy_s(out.visitors[i].userId, sizeof(out.visitors[i].userId), visitorData.id.c_str(), _TRUNCATE);
            strncpy_s(out.visitors[i].nickname, sizeof(out.visitors[i].nickname), visitorData.nickname.c_str(), _TRUNCATE);
            out.visitors[i].playerIconId = visitorData.playerIconId;
            out.visitors[i].visitedAtUnix = visitors[i].visitedAtUnix;
        }
    }

    session->Send(&out);
}
}

void ProfilePacketService::Dispatch(Session* session, PacketHeader* header)
{
    if (session == nullptr || header == nullptr) return;
    switch (static_cast<PacketType>(header->type))
    {
    case PacketType::PKT_CS_PROFILE_REQUEST:
        if (header->size == sizeof(CS_ProfileRequest))
        {
            const auto* request = reinterpret_cast<const CS_ProfileRequest*>(header);
            SendProfile(session, ReadFixed(request->targetUserId, USER_ID_LEN));
        }
        break;
    case PacketType::PKT_CS_PROFILE_PRIVACY_SET:
        if (header->size == sizeof(CS_ProfilePrivacySet))
        {
            const auto* request = reinterpret_cast<const CS_ProfilePrivacySet*>(header);
            SC_ProfilePrivacySetResult out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PROFILE_PRIVACY_SET_RESULT);
            out.enabled = request->enabled ? 1 : 0;
            out.result = ProfileManager::Instance().SetPrivacy(session->GetUserId(), out.enabled != 0) ? 0 : 1;
            session->Send(&out);
        }
        break;
    case PacketType::PKT_CS_PROFILE_LIKE:
        if (header->size == sizeof(CS_ProfileLike))
        {
            const auto* request = reinterpret_cast<const CS_ProfileLike*>(header);
            std::string targetId = ReadFixed(request->targetUserId, USER_ID_LEN);
            SC_ProfileLikeResult out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PROFILE_LIKE_RESULT);
            strncpy_s(out.targetUserId, sizeof(out.targetUserId), targetId.c_str(), _TRUNCATE);
            out.result = ProfileManager::Instance().AddLike(session->GetUserId(), targetId, out.likeCount) ? 0 : 1;
            session->Send(&out);
        }
        break;
    case PacketType::PKT_CS_PROFILE_BATTLE_STATS_SET:
        if (header->size == sizeof(CS_ProfileBattleStatsSet))
        {
            const auto* request = reinterpret_cast<const CS_ProfileBattleStatsSet*>(header);
            std::array<uint8_t, PROFILE_BATTLE_STAT_COUNT> battleStats{};
            std::copy(request->battleStatIds, request->battleStatIds + PROFILE_BATTLE_STAT_COUNT, battleStats.begin());
            SC_ProfileBattleStatsSetResult out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_PROFILE_BATTLE_STATS_SET_RESULT);
            out.result = ProfileManager::Instance().SetBattleStats(session->GetUserId(), battleStats) ? 0 : 1;
            std::copy(battleStats.begin(), battleStats.end(), out.battleStatIds);
            session->Send(&out);
        }
        break;
    case PacketType::PKT_CS_CHIPCRAFT_REQUEST:
        if (header->size == sizeof(CS_ChipCraftRequest))
        {
            const auto* request = reinterpret_cast<const CS_ChipCraftRequest*>(header);
            SendChipCraft(session, ReadFixed(request->targetUserId, USER_ID_LEN));
        }
        break;
    case PacketType::PKT_CS_CHIPCRAFT_SAVE:
        if (header->size == sizeof(CS_ChipCraftSave))
        {
            const auto* request = reinterpret_cast<const CS_ChipCraftSave*>(header);
            const ChipCraftPreset preset = ReadChipCraftPreset(request->preset);
            SC_ChipCraftSaveResult out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_CHIPCRAFT_SAVE_RESULT);
            out.result = ProfileManager::Instance().SaveChipCraft(session->GetUserId(), preset) ? 0 : 1;
            if (out.result == 0)
            {
                UserManager::Instance().SetPlayerIcon(session->GetUserId(), preset.cardId);
                WriteChipCraftPreset(preset, out.preset);
            }
            session->Send(&out);
        }
        break;
    case PacketType::PKT_CS_LEVEL_REWARD_REQUEST:
        if (header->size == sizeof(CS_LevelRewardRequest))
            SendLevelRewardState(session);
        break;
    case PacketType::PKT_CS_LEVEL_REWARD_CLAIM:
        if (header->size == sizeof(CS_LevelRewardClaim))
        {
            const auto* request = reinterpret_cast<const CS_LevelRewardClaim*>(header);
            PlayerData player{};
            CareerLevelRewardClaim claim{};
            if (UserManager::Instance().GetPlayerData(session->GetUserId(), player))
                claim = CareerManager::Instance().ClaimLevelRewards(
                    session->GetUserId(), player.level, request->targetLevel);
            SC_LevelRewardClaimResult out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_LEVEL_REWARD_CLAIM_RESULT);
            out.result = claim.succeeded ? 0 : 1;
            out.claimedLevel = claim.claimedLevel;
            out.freeCurrencyAwarded = claim.freeCurrencyAwarded;
            out.victoryCurrencyAwarded = claim.victoryCurrencyAwarded;
            out.freeCurrency = claim.totalFreeCurrency;
            out.victoryCurrency = claim.totalVictoryCurrency;
            session->Send(&out);
        }
        break;
    case PacketType::PKT_CS_HONOR_REQUEST:
        if (header->size == sizeof(CS_HonorRequest))
        {
            const auto* request = reinterpret_cast<const CS_HonorRequest*>(header);
            SendHonorState(session, request->page);
        }
        break;
    case PacketType::PKT_CS_HONOR_CLAIM:
        if (header->size == sizeof(CS_HonorClaim))
        {
            const auto* request = reinterpret_cast<const CS_HonorClaim*>(header);
            const auto claim = CareerManager::Instance().ClaimHonorRewards(
                session->GetUserId(), request->targetGrade);
            SC_HonorClaimResult out{};
            out.header.size = sizeof(out);
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_HONOR_CLAIM_RESULT);
            out.result = claim.succeeded ? 0 : 1;
            out.claimedGrade = claim.claimedGrade;
            out.freeCurrencyAwarded = claim.freeCurrencyAwarded;
            out.victoryCurrencyAwarded = claim.victoryCurrencyAwarded;
            out.freeCurrency = claim.totalFreeCurrency;
            out.victoryCurrency = claim.totalVictoryCurrency;
            session->Send(&out);
        }
        break;
    default:
        break;
    }
}
