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

struct ChipCraftMedal
{
    uint8_t id = 0;
    float x = 0;
    float y = 0;
    float rotation = 0;
    float scale = 1;
    bool flipped = false;
};
struct ChipCraftPreset
{
    uint8_t cardId = 0;
    uint8_t caseId = 0;
    uint8_t medalCount = 0;
    std::array<ChipCraftMedal, 3> medals{};
};
struct ChipCraftRecord
{
    ChipCraftPreset preset{};
    uint8_t ownedCardMask = 0x07; // All three launch cards are owned by default.
    uint8_t ownedCaseMask = 0x01;
    uint8_t ownedMedalMask = 0x01; // Existing rank art is temporary medal art.
};

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
    bool GetChipCraft(const std::string& userId, ChipCraftRecord& outRecord);
    bool SaveChipCraft(const std::string& userId, const ChipCraftPreset& preset);
private:
    void InitializeUserUnlocked(const std::string& userId);
    void SaveSettingsUnlocked();
    void SaveLikesUnlocked();
    void SaveVisitorsUnlocked();
    void SaveChipCraftUnlocked();
    std::unordered_map<std::string, ProfileSettings> settings_;
    std::unordered_set<std::string> likes_;
    std::unordered_map<std::string, std::vector<ProfileVisitor>> visitors_;
    std::unordered_map<std::string, ChipCraftRecord> chipCraft_;
    std::mutex mutex_;
};
