#include "ProfileManager.h"
#include "UserManager.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <filesystem>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace
{
constexpr const char* SETTINGS_FILE = "profile_settings.csv";
constexpr const char* LIKES_FILE = "profile_likes.csv";
constexpr const char* VISITORS_FILE = "profile_visitors.csv";
constexpr const char* CHIPCRAFT_FILE = "chipcraft_presets.csv";
constexpr const char* DEFAULT_JOIN_DATE = "2026-09-15";
std::string LikeKey(const std::string& likerId, const std::string& targetId) { return likerId + '\n' + targetId; }
int64_t NowUnix() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string TodayDate()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d");
    return stream.str();
}

bool IsValidBattleStats(const std::array<uint8_t, 7>& values)
{
    std::array<bool, 15> used{};
    for (uint8_t value : values)
    {
        if (value >= used.size() || used[value]) return false;
        used[value] = true;
    }
    return true;
}

bool IsValidChipCraft(const ChipCraftRecord& owned, const ChipCraftPreset& preset)
{
    if (preset.cardId >= 3 || preset.caseId >= 3 ||
        (owned.ownedCardMask & (1u << preset.cardId)) == 0 ||
        (owned.ownedCaseMask & (1u << preset.caseId)) == 0 ||
        preset.medalCount > static_cast<uint8_t>(preset.caseId + 1)) return false;
    for (size_t i = 0; i < preset.medalCount; ++i)
    {
        const ChipCraftMedal& medal = preset.medals[i];
        if (medal.id >= 6 || (owned.ownedMedalMask & (1u << medal.id)) == 0 ||
            !std::isfinite(medal.x) || !std::isfinite(medal.y) ||
            !std::isfinite(medal.rotation) || !std::isfinite(medal.scale) ||
            medal.x < -.5f || medal.x > .5f || medal.y < -.5f || medal.y > .5f ||
            medal.rotation < -360.f || medal.rotation > 360.f ||
            medal.scale < .5f || medal.scale > 2.5f) return false;
    }
    return true;
}
}

ProfileManager& ProfileManager::Instance() { static ProfileManager instance; return instance; }

void ProfileManager::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.clear(); likes_.clear(); visitors_.clear(); chipCraft_.clear();
    LoadBirthdaysUnlocked();
    std::ifstream settingsFile(SETTINGS_FILE); std::string line; std::getline(settingsFile, line);
    while (std::getline(settingsFile, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::stringstream stream(line); ProfileSettings value{}; std::string privacy, likes;
        std::getline(stream, value.userId, ','); std::getline(stream, privacy, ','); std::getline(stream, likes, ','); std::getline(stream, value.joinDate, ',');
        if (value.userId.empty()) continue;
        value.privacyEnabled = privacy == "1"; try { value.likeCount = std::max(0, std::stoi(likes)); } catch (...) { value.likeCount = 0; }
        std::array<uint8_t, 7> loadedStats = value.battleStatIds;
        bool hasAllStats = true;
        for (size_t i = 0; i < loadedStats.size(); ++i)
        {
            std::string stat;
            if (!std::getline(stream, stat, ',') || stat.empty()) { hasAllStats = false; break; }
            try
            {
                int parsed = std::stoi(stat);
                if (parsed < 0 || parsed > 14) { hasAllStats = false; break; }
                loadedStats[i] = static_cast<uint8_t>(parsed);
            }
            catch (...) { hasAllStats = false; break; }
        }
        if (hasAllStats && IsValidBattleStats(loadedStats)) value.battleStatIds = loadedStats;
        if (value.joinDate.empty()) value.joinDate = DEFAULT_JOIN_DATE; settings_[value.userId] = value;
    }
    std::ifstream likesFile(LIKES_FILE); std::getline(likesFile, line);
    while (std::getline(likesFile, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::stringstream stream(line); std::string liker, target; std::getline(stream, liker, ','); std::getline(stream, target, ',');
        if (!liker.empty() && !target.empty()) likes_.insert(LikeKey(liker, target));
    }
    std::ifstream visitorsFile(VISITORS_FILE); std::getline(visitorsFile, line);
    while (std::getline(visitorsFile, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::stringstream stream(line); std::string owner, visitedAt; ProfileVisitor visitor{};
        std::getline(stream, owner, ','); std::getline(stream, visitor.userId, ','); std::getline(stream, visitedAt, ',');
        try { visitor.visitedAtUnix = std::stoll(visitedAt); } catch (...) { visitor.visitedAtUnix = 0; }
        if (!owner.empty() && !visitor.userId.empty()) visitors_[owner].push_back(visitor);
    }
    std::ifstream chipCraftFile(CHIPCRAFT_FILE); std::getline(chipCraftFile, line);
    while (std::getline(chipCraftFile, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::stringstream stream(line); std::string field; std::string userId;
        std::getline(stream, userId, ',');
        if (userId.empty()) continue;
        ChipCraftRecord record{};
        try
        {
            auto nextInt = [&]() { if (!std::getline(stream, field, ',')) throw std::invalid_argument("missing field"); return std::stoi(field); };
            auto nextFloat = [&]() { if (!std::getline(stream, field, ',')) throw std::invalid_argument("missing field"); return std::stof(field); };
            record.ownedCardMask = static_cast<uint8_t>(nextInt());
            record.ownedCaseMask = static_cast<uint8_t>(nextInt());
            record.ownedMedalMask = static_cast<uint8_t>(nextInt());
            record.preset.cardId = static_cast<uint8_t>(nextInt());
            record.preset.caseId = static_cast<uint8_t>(nextInt());
            record.preset.medalCount = static_cast<uint8_t>(nextInt());
            for (ChipCraftMedal& medal : record.preset.medals)
            {
                medal.id = static_cast<uint8_t>(nextInt()); medal.x = nextFloat(); medal.y = nextFloat();
                medal.rotation = nextFloat(); medal.scale = nextFloat(); medal.flipped = nextInt() != 0;
            }
            record.ownedCardMask |= 0x07; // launch cards remain owned after migration
            record.ownedCaseMask |= 0x01;
            record.ownedMedalMask |= 0x01;
            if (IsValidChipCraft(record, record.preset)) chipCraft_[userId] = record;
        }
        catch (...) { /* Ignore only the malformed record; preserve other users. */ }
    }
    for (const PlayerData& player : UserManager::Instance().GetAllPlayerData()) InitializeUserUnlocked(player.id);
    SaveSettingsUnlocked();
    SaveChipCraftUnlocked();
}

void ProfileManager::InitializeUser(const std::string& userId) { std::lock_guard<std::mutex> lock(mutex_); InitializeUserUnlocked(userId); SaveSettingsUnlocked(); }
void ProfileManager::InitializeUserUnlocked(const std::string& userId)
{
    if (userId.empty()) return;
    if (settings_.count(userId) == 0)
    {
        ProfileSettings settings{}; settings.userId = userId; settings.joinDate = TodayDate(); settings_[userId] = settings;
    }
    if (chipCraft_.count(userId) == 0)
    {
        ChipCraftRecord record{};
        PlayerData player{};
        if (UserManager::Instance().GetPlayerData(userId, player) && player.playerIconId >= 0 && player.playerIconId < 3)
            record.preset.cardId = static_cast<uint8_t>(player.playerIconId);
        chipCraft_[userId] = record;
    }
}

bool ProfileManager::GetChipCraft(const std::string& userId, ChipCraftRecord& outRecord)
{
    std::lock_guard<std::mutex> lock(mutex_);
    InitializeUserUnlocked(userId);
    auto it = chipCraft_.find(userId);
    if (it == chipCraft_.end()) return false;
    outRecord = it->second;
    return true;
}

bool ProfileManager::SaveChipCraft(const std::string& userId, const ChipCraftPreset& preset)
{
    std::lock_guard<std::mutex> lock(mutex_);
    InitializeUserUnlocked(userId);
    auto it = chipCraft_.find(userId);
    if (it == chipCraft_.end() || !IsValidChipCraft(it->second, preset)) return false;
    it->second.preset = preset;
    SaveChipCraftUnlocked();
    return true;
}
bool ProfileManager::GetSettings(const std::string& userId, ProfileSettings& outSettings)
{
    std::lock_guard<std::mutex> lock(mutex_); InitializeUserUnlocked(userId); auto it = settings_.find(userId); if (it == settings_.end()) return false;
    outSettings = it->second;
    auto birthday = birthdays_.find(userId);
    outSettings.birthday = birthday == birthdays_.end() ? std::string{} : birthday->second;
    return true;
}
bool ProfileManager::IsValidBirthday(int month, int day)
{
    // No year collected: February 29 remains a valid recurring birthday.
    constexpr int days[] = {31,29,31,30,31,30,31,31,30,31,30,31};
    return month >= 1 && month <= 12 && day >= 1 && day <= days[month - 1];
}
bool ProfileManager::SetBirthday(const std::string& userId, int month, int day)
{
    if (!IsValidBirthday(month, day) || userId.empty() || userId.find_first_of(",\r\n") != std::string::npos) return false;
    PlayerData player{};
    if (!UserManager::Instance().GetPlayerData(userId, player)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!birthdaysWritable_) return false;
    std::ostringstream date;
    date << std::setfill('0') << std::setw(2) << month << '.' << std::setw(2) << day;
    auto existing = birthdays_.find(userId);
    const bool hadValue = existing != birthdays_.end();
    const std::string previous = hadValue ? existing->second : std::string{};
    birthdays_[userId] = date.str();
    if (SaveBirthdaysUnlocked()) return true;
    if (hadValue) birthdays_[userId] = previous; else birthdays_.erase(userId);
    return false;
}
void ProfileManager::LoadBirthdaysUnlocked()
{
    birthdays_.clear(); birthdaysWritable_ = true;
    std::error_code error;
    const bool exists = std::filesystem::exists("profile_birthdays.csv", error);
    if (error) { birthdaysWritable_ = false; return; }
    if (!exists) return;
    std::ifstream file("profile_birthdays.csv"); std::string line;
    if (!file || !std::getline(file, line)) { birthdaysWritable_ = false; return; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "userId,birthday") { birthdaysWritable_ = false; return; }
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto comma = line.find(',');
        const std::string date = comma == std::string::npos ? "" : line.substr(comma + 1);
        bool valid = comma != std::string::npos && comma > 0 && date.size() == 5 && date[2] == '.';
        if (valid) for (int i : {0,1,3,4}) if (date[i] < '0' || date[i] > '9') valid = false;
        if (valid) valid = IsValidBirthday((date[0]-'0')*10+date[1]-'0', (date[3]-'0')*10+date[4]-'0');
        if (!valid) { birthdaysWritable_ = false; continue; }
        if (!birthdays_.emplace(line.substr(0, comma), date).second) birthdaysWritable_ = false;
    }
    if (file.bad()) birthdaysWritable_ = false;
}
bool ProfileManager::SaveBirthdaysUnlocked()
{
    // Acknowledge only after atomic replacement; never rewrite legacy account files.
    std::ofstream file("profile_birthdays.csv.tmp", std::ios::trunc | std::ios::binary);
    if (!file) return false;
    file << "userId,birthday\n";
    for (const auto& entry : birthdays_) file << entry.first << ',' << entry.second << '\n';
    file.flush(); if (!file.good()) return false;
    file.close(); if (file.fail()) return false;
    return MoveFileExW(L"profile_birthdays.csv.tmp", L"profile_birthdays.csv", MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
bool ProfileManager::SetPrivacy(const std::string& userId, bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_); InitializeUserUnlocked(userId); auto it = settings_.find(userId); if (it == settings_.end()) return false; it->second.privacyEnabled = enabled; SaveSettingsUnlocked(); return true;
}
bool ProfileManager::SetBattleStats(const std::string& userId, const std::array<uint8_t, 7>& battleStatIds)
{
    if (!IsValidBattleStats(battleStatIds)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    InitializeUserUnlocked(userId);
    auto it = settings_.find(userId);
    if (it == settings_.end()) return false;
    it->second.battleStatIds = battleStatIds;
    SaveSettingsUnlocked();
    return true;
}
bool ProfileManager::AddLike(const std::string& likerId, const std::string& targetUserId, int& outLikeCount)
{
    std::lock_guard<std::mutex> lock(mutex_); InitializeUserUnlocked(targetUserId); auto target = settings_.find(targetUserId);
    if (likerId.empty() || target == settings_.end() || likerId == targetUserId) { outLikeCount = target == settings_.end() ? 0 : target->second.likeCount; return false; }
    if (!likes_.insert(LikeKey(likerId, targetUserId)).second) { outLikeCount = target->second.likeCount; return false; }
    outLikeCount = ++target->second.likeCount; SaveLikesUnlocked(); SaveSettingsUnlocked(); return true;
}
void ProfileManager::RecordVisit(const std::string& ownerUserId, const std::string& visitorUserId)
{
    if (ownerUserId.empty() || visitorUserId.empty() || ownerUserId == visitorUserId) return;
    std::lock_guard<std::mutex> lock(mutex_); auto& list = visitors_[ownerUserId];
    list.erase(std::remove_if(list.begin(), list.end(), [&](const ProfileVisitor& value) { return value.userId == visitorUserId; }), list.end());
    list.insert(list.begin(), ProfileVisitor{ visitorUserId, NowUnix() }); if (list.size() > 20) list.resize(20); SaveVisitorsUnlocked();
}
std::vector<ProfileVisitor> ProfileManager::GetRecentVisitors(const std::string& ownerUserId, size_t limit)
{
    std::lock_guard<std::mutex> lock(mutex_); auto it = visitors_.find(ownerUserId); if (it == visitors_.end()) return {}; size_t count = std::min(limit, it->second.size()); return { it->second.begin(), it->second.begin() + count };
}
void ProfileManager::SaveSettingsUnlocked()
{
    std::ofstream file(SETTINGS_FILE, std::ios::trunc);
    file << "userId,privacyEnabled,likeCount,joinDate,battleStat0,battleStat1,battleStat2,battleStat3,battleStat4,battleStat5,battleStat6\n";
    for (const auto& pair : settings_)
    {
        file << pair.first << ',' << (pair.second.privacyEnabled ? 1 : 0) << ',' << pair.second.likeCount << ',' << pair.second.joinDate;
        for (uint8_t statId : pair.second.battleStatIds) file << ',' << static_cast<int>(statId);
        file << '\n';
    }
}
void ProfileManager::SaveLikesUnlocked()
{
    std::ofstream file(LIKES_FILE, std::ios::trunc); file << "likerUserId,targetUserId\n";
    for (const std::string& key : likes_) { size_t separator = key.find('\n'); if (separator != std::string::npos) file << key.substr(0, separator) << ',' << key.substr(separator + 1) << '\n'; }
}
void ProfileManager::SaveVisitorsUnlocked()
{
    std::ofstream file(VISITORS_FILE, std::ios::trunc); file << "ownerUserId,visitorUserId,visitedAtUnix\n";
    for (const auto& pair : visitors_) for (const ProfileVisitor& visitor : pair.second) file << pair.first << ',' << visitor.userId << ',' << visitor.visitedAtUnix << '\n';
}

void ProfileManager::SaveChipCraftUnlocked()
{
    std::ofstream file(CHIPCRAFT_FILE, std::ios::trunc);
    file << "userId,ownedCardMask,ownedCaseMask,ownedMedalMask,cardId,caseId,medalCount";
    for (int i = 0; i < 3; ++i)
        file << ",medal" << i << "Id,medal" << i << "X,medal" << i << "Y,medal" << i
             << "Rotation,medal" << i << "Scale,medal" << i << "Flipped";
    file << '\n' << std::setprecision(6);
    for (const auto& pair : chipCraft_)
    {
        const ChipCraftRecord& record = pair.second;
        file << pair.first << ',' << static_cast<int>(record.ownedCardMask) << ','
             << static_cast<int>(record.ownedCaseMask) << ',' << static_cast<int>(record.ownedMedalMask)
             << ',' << static_cast<int>(record.preset.cardId) << ',' << static_cast<int>(record.preset.caseId)
             << ',' << static_cast<int>(record.preset.medalCount);
        for (const ChipCraftMedal& medal : record.preset.medals)
            file << ',' << static_cast<int>(medal.id) << ',' << medal.x << ',' << medal.y << ','
                 << medal.rotation << ',' << medal.scale << ',' << (medal.flipped ? 1 : 0);
        file << '\n';
    }
}
