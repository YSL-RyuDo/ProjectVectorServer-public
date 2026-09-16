#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ProfileSettings
{
    std::string userId;
    bool privacyEnabled = false;
    int likeCount = 0;
    std::string joinDate = "2026-09-15";
    std::array<uint8_t, 7> battleStatIds{ 0, 1, 2, 3, 4, 5, 6 };
};
struct ProfileVisitor { std::string userId; int64_t visitedAtUnix = 0; };

class ProfileManager
{
public:
    static ProfileManager& Instance();
    void Load();
    void InitializeUser(const std::string& userId);
    bool GetSettings(const std::string& userId, ProfileSettings& outSettings);
    bool SetPrivacy(const std::string& userId, bool enabled);
    bool SetBattleStats(const std::string& userId, const std::array<uint8_t, 7>& battleStatIds);
    bool AddLike(const std::string& likerId, const std::string& targetUserId, int& outLikeCount);
    void RecordVisit(const std::string& ownerUserId, const std::string& visitorUserId);
    std::vector<ProfileVisitor> GetRecentVisitors(const std::string& ownerUserId, size_t limit);
private:
    void InitializeUserUnlocked(const std::string& userId);
    void SaveSettingsUnlocked();
    void SaveLikesUnlocked();
    void SaveVisitorsUnlocked();
    std::unordered_map<std::string, ProfileSettings> settings_;
    std::unordered_set<std::string> likes_;
    std::unordered_map<std::string, std::vector<ProfileVisitor>> visitors_;
    std::mutex mutex_;
};
