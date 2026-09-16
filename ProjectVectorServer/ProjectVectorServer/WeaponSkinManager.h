#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

constexpr int WEAPON_SKIN_WEAPON_COUNT = 14;
constexpr int WEAPON_SKIN_CHARACTER_COUNT = 4;

struct WeaponSkinDefinitionData
{
    int weaponId = 0;
    int skinId = 0;
    std::string skinName;
    bool isDefault = false;
};

class WeaponSkinManager
{
public:
    static WeaponSkinManager& Instance();

    // Loads the skin catalog, one-row-per-user weapon files, and user loadouts.
    // Legacy consolidated ownership formats migrate into the split files on save.
    void Load();

    // Called from registration so a new account receives ownership/loadout data immediately.
    void InitializeUser(const std::string& userId);

    bool GetEquippedSkins(
        const std::string& userId,
        int characterId,
        std::array<int, WEAPON_SKIN_WEAPON_COUNT>& outSkinIds);

    bool SetEquippedSkin(
        const std::string& userId,
        int characterId,
        int weaponId,
        int skinId);
    bool IsOwned(const std::string& userId, int weaponId, int skinId);
    int GetOwnedSkinCount(const std::string& userId);
    int GetDefinitionCount();

private:
    void LoadDefinitionsUnlocked();
    void LoadOwnershipUnlocked(const std::unordered_set<std::string>& validUsers);
    void LoadLoadoutsUnlocked(const std::unordered_set<std::string>& validUsers);
    std::unordered_set<std::string> LoadUserIdsUnlocked();

    void SeedDefinitionsUnlocked();
    void InitializeUserUnlocked(const std::string& userId);

    void SaveDefinitionsUnlocked();
    void SaveOwnershipUnlocked();
    void SaveLoadoutsUnlocked();

    std::unordered_map<int64_t, WeaponSkinDefinitionData> definitions_;
    std::unordered_map<std::string, std::unordered_set<int64_t>> ownership_;
    std::unordered_map<
        std::string,
        std::unordered_map<int, std::unordered_map<int, int>>> loadouts_;
    std::unordered_set<std::string> knownUsers_;
    std::mutex mutex_;
};
