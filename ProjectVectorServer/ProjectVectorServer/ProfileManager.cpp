#include "ProfileManager.h"
#include "UserManager.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
constexpr const char* SETTINGS_FILE = "profile_settings.csv";
constexpr const char* LIKES_FILE = "profile_likes.csv";
constexpr const char* VISITORS_FILE = "profile_visitors.csv";
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
}

ProfileManager& ProfileManager::Instance() { static ProfileManager instance; return instance; }

void ProfileManager::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.clear(); likes_.clear(); visitors_.clear();
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
    for (const PlayerData& player : UserManager::Instance().GetAllPlayerData()) InitializeUserUnlocked(player.id);
    SaveSettingsUnlocked();
}

void ProfileManager::InitializeUser(const std::string& userId) { std::lock_guard<std::mutex> lock(mutex_); InitializeUserUnlocked(userId); SaveSettingsUnlocked(); }
void ProfileManager::InitializeUserUnlocked(const std::string& userId)
{
    if (userId.empty() || settings_.count(userId) != 0) return;
    ProfileSettings settings{}; settings.userId = userId; settings.joinDate = TodayDate(); settings_[userId] = settings;
}
bool ProfileManager::GetSettings(const std::string& userId, ProfileSettings& outSettings)
{
    std::lock_guard<std::mutex> lock(mutex_); InitializeUserUnlocked(userId); auto it = settings_.find(userId); if (it == settings_.end()) return false; outSettings = it->second; return true;
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
