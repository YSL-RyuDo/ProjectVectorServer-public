#include "WeaponSkinManager.h"

#include <algorithm>
#include <cerrno>
#include <direct.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tuple>
#include <vector>

namespace
{
    constexpr const char* DEFINITION_HEADER =
        "weaponId,skinId,skinName,isDefault";
    constexpr const char* LEGACY_USER_WEAPON_HEADER =
        "userId,weaponId,skinId";
    constexpr const char* LEGACY_COMPACT_OWNERSHIP_HEADER =
        "userId,P92,P1911,R1895,UZI,UMP,S1897,AA12,Dragunov,M416,AK47,M249,Kar98,AWM,Knife";
    constexpr const char* LEGACY_ATOMIC_OWNERSHIP_HEADER =
        "userId,P92_Skin0Owned,P92_Skin1Owned,P1911_Skin0Owned,P1911_Skin1Owned,"
        "R1895_Skin0Owned,R1895_Skin1Owned,UZI_Skin0Owned,UZI_Skin1Owned,"
        "UMP_Skin0Owned,UMP_Skin1Owned,S1897_Skin0Owned,S1897_Skin1Owned,"
        "AA12_Skin0Owned,AA12_Skin1Owned,Dragunov_Skin0Owned,Dragunov_Skin1Owned,"
        "M416_Skin0Owned,M416_Skin1Owned,AK47_Skin0Owned,AK47_Skin1Owned,"
        "M249_Skin0Owned,M249_Skin1Owned,Kar98_Skin0Owned,Kar98_Skin1Owned,"
        "AWM_Skin0Owned,AWM_Skin1Owned,Knife_Skin0Owned";
    constexpr const char* LEGACY_SPLIT_OWNERSHIP_HEADER = "userId,skinId";
    constexpr const char* OWNERSHIP_DIRECTORY = "user_weapon_skins";
    constexpr const char* LOADOUT_HEADER =
        "userId,P92,P1911,R1895,UZI,UMP,S1897,AA12,Dragunov,M416,AK47,M249,Kar98,AWM,Knife";
    constexpr const char* LOADOUT_DIRECTORY = "user_weapon_skin_loadouts";
    constexpr const char* LEGACY_LOADOUT_FILE = "user_weapon_skin_loadouts.csv";

    constexpr const char* OWNERSHIP_FILE_NAMES[WEAPON_SKIN_WEAPON_COUNT] =
    {
        "00_P92.csv", "01_P1911.csv", "02_R1895.csv", "03_UZI.csv",
        "04_UMP.csv", "05_S1897.csv", "06_AA12.csv", "07_Dragunov.csv",
        "08_M416.csv", "09_AK47.csv", "10_M249.csv", "11_Kar98.csv",
        "12_AWM.csv", "13_Knife.csv"
    };

    constexpr const char* LOADOUT_FILE_NAMES[WEAPON_SKIN_CHARACTER_COUNT] =
    {
        "00_Initiator.csv", "01_Controller.csv",
        "02_Sentinel.csv", "03_Entry.csv"
    };

    std::string GetOwnershipFilePath(int weaponId)
    {
        return std::string(OWNERSHIP_DIRECTORY) + "\\" +
            OWNERSHIP_FILE_NAMES[weaponId];
    }

    std::string GetLoadoutFilePath(int characterId)
    {
        return std::string(LOADOUT_DIRECTORY) + "\\" +
            LOADOUT_FILE_NAMES[characterId];
    }

    int64_t MakeKey(int weaponId, int skinId)
    {
        return (static_cast<int64_t>(static_cast<uint32_t>(weaponId)) << 32) |
            static_cast<uint32_t>(skinId);
    }

    std::vector<std::string> SplitCsvLine(const std::string& line)
    {
        std::vector<std::string> tokens;
        std::stringstream stream(line);
        std::string token;

        while (std::getline(stream, token, ','))
            tokens.push_back(token);

        return tokens;
    }

    void RemoveCarriageReturn(std::string& line)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
    }

    std::vector<int> SplitSkinIds(const std::string& value)
    {
        std::vector<int> skinIds;
        std::stringstream stream(value);
        std::string token;

        while (std::getline(stream, token, '|'))
        {
            if (!token.empty())
                skinIds.push_back(std::stoi(token));
        }

        return skinIds;
    }

    bool TryParseOwnedSkinColumn(const std::string& columnName, int& skinId)
    {
        constexpr const char* PREFIX = "skin";
        constexpr const char* SUFFIX = "Owned";
        const size_t prefixLength = 4;
        const size_t suffixLength = 5;

        if (columnName.size() <= prefixLength + suffixLength ||
            columnName.compare(0, prefixLength, PREFIX) != 0 ||
            columnName.compare(
                columnName.size() - suffixLength,
                suffixLength,
                SUFFIX) != 0)
        {
            return false;
        }

        try
        {
            std::string idText = columnName.substr(
                prefixLength,
                columnName.size() - prefixLength - suffixLength);
            size_t parsedLength = 0;
            skinId = std::stoi(idText, &parsedLength);
            return parsedLength == idText.size() && skinId >= 0;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
}

WeaponSkinManager& WeaponSkinManager::Instance()
{
    static WeaponSkinManager instance;
    return instance;
}

void WeaponSkinManager::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);

    definitions_.clear();
    ownership_.clear();
    loadouts_.clear();

    knownUsers_ = LoadUserIdsUnlocked();
    LoadDefinitionsUnlocked();
    LoadOwnershipUnlocked(knownUsers_);
    LoadLoadoutsUnlocked(knownUsers_);

    // Migration policy for the current test phase:
    // every existing account owns every registered skin and defaults to skin 0.
    // Saving below also converts legacy ownership formats into per-weapon files.
    for (const std::string& userId : knownUsers_)
        InitializeUserUnlocked(userId);

    SaveDefinitionsUnlocked();
    SaveOwnershipUnlocked();
    SaveLoadoutsUnlocked();

    std::cout << "[WEAPON SKIN DATA] definitions=" << definitions_.size()
        << " users=" << knownUsers_.size() << std::endl;
}

void WeaponSkinManager::InitializeUser(const std::string& userId)
{
    if (userId.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex_);

    if (definitions_.empty())
    {
        SeedDefinitionsUnlocked();
        SaveDefinitionsUnlocked();
    }

    knownUsers_.insert(userId);
    InitializeUserUnlocked(userId);
    SaveOwnershipUnlocked();
    SaveLoadoutsUnlocked();
}

bool WeaponSkinManager::GetEquippedSkins(
    const std::string& userId,
    int characterId,
    std::array<int, WEAPON_SKIN_WEAPON_COUNT>& outSkinIds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    outSkinIds.fill(0);

    if (characterId < 0 || characterId >= WEAPON_SKIN_CHARACTER_COUNT)
        return false;

    auto userIt = loadouts_.find(userId);
    if (userIt == loadouts_.end())
        return false;

    auto characterIt = userIt->second.find(characterId);
    if (characterIt == userIt->second.end())
        return false;

    for (const auto& pair : characterIt->second)
    {
        if (pair.first >= 0 && pair.first < WEAPON_SKIN_WEAPON_COUNT)
            outSkinIds[pair.first] = pair.second;
    }

    return true;
}

bool WeaponSkinManager::SetEquippedSkin(
    const std::string& userId,
    int characterId,
    int weaponId,
    int skinId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (knownUsers_.count(userId) == 0 ||
        characterId < 0 || characterId >= WEAPON_SKIN_CHARACTER_COUNT ||
        weaponId < 0 || weaponId >= WEAPON_SKIN_WEAPON_COUNT)
    {
        return false;
    }

    int64_t key = MakeKey(weaponId, skinId);
    auto ownedIt = ownership_.find(userId);

    if (definitions_.count(key) == 0 ||
        ownedIt == ownership_.end() ||
        ownedIt->second.count(key) == 0)
    {
        return false;
    }

    loadouts_[userId][characterId][weaponId] = skinId;
    SaveLoadoutsUnlocked();
    return true;
}

bool WeaponSkinManager::IsOwned(
    const std::string& userId,
    int weaponId,
    int skinId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto userIt = ownership_.find(userId);
    return userIt != ownership_.end() &&
        userIt->second.count(MakeKey(weaponId, skinId)) != 0;
}

void WeaponSkinManager::LoadDefinitionsUnlocked()
{
    std::ifstream file("weapon_skins.csv");
    std::string line;

    if (!file.is_open() || !std::getline(file, line))
    {
        SeedDefinitionsUnlocked();
        return;
    }

    RemoveCarriageReturn(line);
    if (line != DEFINITION_HEADER)
    {
        std::cout << "[WEAPON SKIN DATA] invalid weapon_skins.csv header; reseeding"
            << std::endl;
        SeedDefinitionsUnlocked();
        return;
    }

    while (std::getline(file, line))
    {
        RemoveCarriageReturn(line);
        if (line.empty())
            continue;

        std::vector<std::string> tokens = SplitCsvLine(line);
        if (tokens.size() < 4)
            continue;

        try
        {
            WeaponSkinDefinitionData data{};
            data.weaponId = std::stoi(tokens[0]);
            data.skinId = std::stoi(tokens[1]);
            data.skinName = tokens[2];
            data.isDefault = std::stoi(tokens[3]) != 0;

            if (data.weaponId < 0 || data.weaponId >= WEAPON_SKIN_WEAPON_COUNT ||
                data.skinId < 0 || data.skinName.empty())
            {
                continue;
            }

            definitions_[MakeKey(data.weaponId, data.skinId)] = data;
        }
        catch (const std::exception&)
        {
            std::cout << "[WEAPON SKIN DEFINITION SKIP] " << line << std::endl;
        }
    }

    if (definitions_.empty())
        SeedDefinitionsUnlocked();
}

void WeaponSkinManager::LoadOwnershipUnlocked(
    const std::unordered_set<std::string>& validUsers)
{
    bool foundSplitFile = false;

    for (int weaponId = 0; weaponId < WEAPON_SKIN_WEAPON_COUNT; ++weaponId)
    {
        std::string path = GetOwnershipFilePath(weaponId);
        std::ifstream file(path);
        std::string line;

        if (!file.is_open() || !std::getline(file, line))
            continue;

        RemoveCarriageReturn(line);
        std::vector<std::string> headerTokens = SplitCsvLine(line);
        const bool isLegacySplitFormat =
            line == LEGACY_SPLIT_OWNERSHIP_HEADER;

        if (!isLegacySplitFormat &&
            (headerTokens.empty() || headerTokens[0] != "userId"))
            continue;

        foundSplitFile = true;

        std::vector<int> columnSkinIds;
        if (!isLegacySplitFormat)
        {
            for (size_t column = 1; column < headerTokens.size(); ++column)
            {
                int skinId = -1;
                columnSkinIds.push_back(
                    TryParseOwnedSkinColumn(headerTokens[column], skinId)
                    ? skinId
                    : -1);
            }
        }

        while (std::getline(file, line))
        {
            RemoveCarriageReturn(line);
            std::vector<std::string> tokens = SplitCsvLine(line);

            if (tokens.empty() || validUsers.count(tokens[0]) == 0)
                continue;

            try
            {
                if (isLegacySplitFormat)
                {
                    if (tokens.size() < 2)
                        continue;

                    int skinId = std::stoi(tokens[1]);
                    int64_t key = MakeKey(weaponId, skinId);

                    if (definitions_.count(key) != 0)
                        ownership_[tokens[0]].insert(key);
                }
                else
                {
                    for (size_t column = 0;
                        column < columnSkinIds.size() && column + 1 < tokens.size();
                        ++column)
                    {
                        int skinId = columnSkinIds[column];
                        if (skinId < 0 || std::stoi(tokens[column + 1]) == 0)
                            continue;

                        int64_t key = MakeKey(weaponId, skinId);
                        if (definitions_.count(key) != 0)
                            ownership_[tokens[0]].insert(key);
                    }
                }
            }
            catch (const std::exception&)
            {
                std::cout << "[USER WEAPON SKIN SKIP] "
                    << path << ": " << line << std::endl;
            }
        }
    }

    if (foundSplitFile)
        return;

    // One-time migration path for the earlier consolidated CSV layouts.
    std::ifstream file("user_weapon_skins.csv");
    std::string line;

    if (!file.is_open() || !std::getline(file, line))
        return;

    RemoveCarriageReturn(line);
    const bool isLegacyFormat = line == LEGACY_USER_WEAPON_HEADER;
    const bool isLegacyCompactFormat =
        line == LEGACY_COMPACT_OWNERSHIP_HEADER;
    if (!isLegacyFormat && !isLegacyCompactFormat &&
        line != LEGACY_ATOMIC_OWNERSHIP_HEADER)
        return;

    std::vector<WeaponSkinDefinitionData> skinColumns;
    skinColumns.reserve(definitions_.size());

    for (const auto& definitionPair : definitions_)
        skinColumns.push_back(definitionPair.second);

    std::sort(skinColumns.begin(), skinColumns.end(), [](const auto& left, const auto& right)
    {
        return std::tie(left.weaponId, left.skinId) <
            std::tie(right.weaponId, right.skinId);
    });

    while (std::getline(file, line))
    {
        RemoveCarriageReturn(line);
        std::vector<std::string> tokens = SplitCsvLine(line);

        if (tokens.empty() || validUsers.count(tokens[0]) == 0)
            continue;

        try
        {
            if (isLegacyFormat)
            {
                if (tokens.size() < 3)
                    continue;

                int weaponId = std::stoi(tokens[1]);
                int skinId = std::stoi(tokens[2]);
                int64_t key = MakeKey(weaponId, skinId);

                if (definitions_.count(key) != 0)
                    ownership_[tokens[0]].insert(key);
            }
            else if (isLegacyCompactFormat)
            {
                if (tokens.size() < WEAPON_SKIN_WEAPON_COUNT + 1)
                    continue;

                for (int weaponId = 0;
                    weaponId < WEAPON_SKIN_WEAPON_COUNT;
                    ++weaponId)
                {
                    for (int skinId : SplitSkinIds(tokens[weaponId + 1]))
                    {
                        int64_t key = MakeKey(weaponId, skinId);
                        if (definitions_.count(key) != 0)
                            ownership_[tokens[0]].insert(key);
                    }
                }
            }
            else
            {
                if (tokens.size() < skinColumns.size() + 1)
                    continue;

                for (size_t column = 0; column < skinColumns.size(); ++column)
                {
                    if (std::stoi(tokens[column + 1]) == 0)
                        continue;

                    const WeaponSkinDefinitionData& skin = skinColumns[column];
                    ownership_[tokens[0]].insert(
                        MakeKey(skin.weaponId, skin.skinId));
                }
            }
        }
        catch (const std::exception&)
        {
            std::cout << "[USER WEAPON SKIN SKIP] " << line << std::endl;
        }
    }
}

void WeaponSkinManager::LoadLoadoutsUnlocked(
    const std::unordered_set<std::string>& validUsers)
{
    bool foundCharacterFile = false;

    for (int characterId = 0;
        characterId < WEAPON_SKIN_CHARACTER_COUNT;
        ++characterId)
    {
        std::ifstream file(GetLoadoutFilePath(characterId));
        std::string line;

        if (!file.is_open() || !std::getline(file, line))
            continue;

        RemoveCarriageReturn(line);
        if (line != LOADOUT_HEADER)
            continue;

        foundCharacterFile = true;

        while (std::getline(file, line))
        {
            RemoveCarriageReturn(line);
            std::vector<std::string> tokens = SplitCsvLine(line);

            if (tokens.size() < WEAPON_SKIN_WEAPON_COUNT + 1 ||
                validUsers.count(tokens[0]) == 0)
            {
                continue;
            }

            try
            {
                for (int weaponId = 0;
                    weaponId < WEAPON_SKIN_WEAPON_COUNT;
                    ++weaponId)
                {
                    int skinId = std::stoi(tokens[weaponId + 1]);
                    int64_t key = MakeKey(weaponId, skinId);

                    if (definitions_.count(key) != 0)
                        loadouts_[tokens[0]][characterId][weaponId] = skinId;
                }
            }
            catch (const std::exception&)
            {
                std::cout << "[WEAPON SKIN LOADOUT SKIP] "
                    << GetLoadoutFilePath(characterId) << ": "
                    << line << std::endl;
            }
        }
    }

    if (foundCharacterFile)
        return;

    // One-time migration from the former user-wide loadout. The same selection
    // is copied to every character so no existing account loses its choices.
    std::ifstream legacyFile(LEGACY_LOADOUT_FILE);
    std::string line;

    if (!legacyFile.is_open() || !std::getline(legacyFile, line))
        return;

    RemoveCarriageReturn(line);
    const bool isAtomicLegacy = line == LEGACY_USER_WEAPON_HEADER;
    if (!isAtomicLegacy && line != LOADOUT_HEADER)
        return;

    while (std::getline(legacyFile, line))
    {
        RemoveCarriageReturn(line);
        std::vector<std::string> tokens = SplitCsvLine(line);

        if (tokens.empty() || validUsers.count(tokens[0]) == 0)
            continue;

        try
        {
            if (isAtomicLegacy)
            {
                if (tokens.size() < 3)
                    continue;

                int weaponId = std::stoi(tokens[1]);
                int skinId = std::stoi(tokens[2]);

                if (definitions_.count(MakeKey(weaponId, skinId)) == 0)
                    continue;

                for (int characterId = 0;
                    characterId < WEAPON_SKIN_CHARACTER_COUNT;
                    ++characterId)
                {
                    loadouts_[tokens[0]][characterId][weaponId] = skinId;
                }
            }
            else
            {
                if (tokens.size() < WEAPON_SKIN_WEAPON_COUNT + 1)
                    continue;

                for (int weaponId = 0;
                    weaponId < WEAPON_SKIN_WEAPON_COUNT;
                    ++weaponId)
                {
                    int skinId = std::stoi(tokens[weaponId + 1]);
                    if (definitions_.count(MakeKey(weaponId, skinId)) == 0)
                        continue;

                    for (int characterId = 0;
                        characterId < WEAPON_SKIN_CHARACTER_COUNT;
                        ++characterId)
                    {
                        loadouts_[tokens[0]][characterId][weaponId] = skinId;
                    }
                }
            }
        }
        catch (const std::exception&)
        {
            std::cout << "[WEAPON SKIN LOADOUT MIGRATION SKIP] "
                << line << std::endl;
        }
    }
}

std::unordered_set<std::string> WeaponSkinManager::LoadUserIdsUnlocked()
{
    std::unordered_set<std::string> result;
    std::ifstream file("users.csv");
    std::string line;
    bool firstLine = true;

    while (std::getline(file, line))
    {
        RemoveCarriageReturn(line);

        if (firstLine)
        {
            firstLine = false;
            if (line == "id,nickname,salt,passwordHash")
                continue;
        }

        std::vector<std::string> tokens = SplitCsvLine(line);
        if (!tokens.empty() && !tokens[0].empty())
            result.insert(tokens[0]);
    }

    return result;
}

void WeaponSkinManager::SeedDefinitionsUnlocked()
{
    definitions_.clear();

    static const char* weaponNames[WEAPON_SKIN_WEAPON_COUNT] =
    {
        "P92", "P1911", "R1895", "UZI", "UMP", "S1897", "AA-12",
        "Dragunov", "M416", "AK47", "M249", "Kar98", "AWM", "Knife"
    };

    static const char* additionalSkinNames[WEAPON_SKIN_WEAPON_COUNT - 1][3] =
    {
        { "Orange", "Dark", "Silver" },
        { "White", "Purple", "Yellow" },
        { "Yellow", "Winter", "Red" },
        { "Silver", "Toxic", "Yellow" },
        { "White", "Orange", "Purple" },
        { "Purple", "White", "Orange" },
        { "Yellow", "Cargo", "Red" },
        { "Gold", "Silver", "Red" },
        { "Cargo", "Winter", "Red" },
        { "Red", "Cargo", "Toxic" },
        { "Purple", "White", "Red" },
        { "Red", "White", "Cargo" },
        { "Toxic", "Purple", "Red" }
    };

    for (int weaponId = 0; weaponId < WEAPON_SKIN_WEAPON_COUNT; ++weaponId)
    {
        WeaponSkinDefinitionData baseSkin{};
        baseSkin.weaponId = weaponId;
        baseSkin.skinId = 0;
        baseSkin.skinName = std::string(weaponNames[weaponId]) + " Default";
        baseSkin.isDefault = true;
        definitions_[MakeKey(weaponId, 0)] = baseSkin;

        if (weaponId < WEAPON_SKIN_WEAPON_COUNT - 1)
        {
            for (int skinId = 1; skinId <= 3; ++skinId)
            {
                WeaponSkinDefinitionData skin{};
                skin.weaponId = weaponId;
                skin.skinId = skinId;
                skin.skinName = additionalSkinNames[weaponId][skinId - 1];
                skin.isDefault = false;
                definitions_[MakeKey(weaponId, skinId)] = skin;
            }
        }
    }
}

void WeaponSkinManager::InitializeUserUnlocked(const std::string& userId)
{
    std::unordered_set<int64_t>& owned = ownership_[userId];

    for (const auto& definitionPair : definitions_)
        owned.insert(definitionPair.first);

    for (int characterId = 0;
        characterId < WEAPON_SKIN_CHARACTER_COUNT;
        ++characterId)
    {
        std::unordered_map<int, int>& equipped =
            loadouts_[userId][characterId];

        for (int weaponId = 0;
            weaponId < WEAPON_SKIN_WEAPON_COUNT;
            ++weaponId)
        {
            auto selectedIt = equipped.find(weaponId);
            int skinId = selectedIt != equipped.end() ? selectedIt->second : 0;
            int64_t selectedKey = MakeKey(weaponId, skinId);

            if (definitions_.count(selectedKey) == 0 ||
                owned.count(selectedKey) == 0)
            {
                skinId = 0;
            }

            equipped[weaponId] = skinId;
        }
    }
}

void WeaponSkinManager::SaveDefinitionsUnlocked()
{
    std::ofstream file("weapon_skins.csv", std::ios::trunc);
    if (!file.is_open())
        return;

    file << DEFINITION_HEADER << "\n";

    std::vector<WeaponSkinDefinitionData> rows;
    rows.reserve(definitions_.size());

    for (const auto& pair : definitions_)
        rows.push_back(pair.second);

    std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right)
    {
        return std::tie(left.weaponId, left.skinId) <
            std::tie(right.weaponId, right.skinId);
    });

    for (const WeaponSkinDefinitionData& row : rows)
    {
        file << row.weaponId << "," << row.skinId << ","
            << row.skinName << "," << (row.isDefault ? 1 : 0) << "\n";
    }
}

void WeaponSkinManager::SaveOwnershipUnlocked()
{
    if (_mkdir(OWNERSHIP_DIRECTORY) != 0 && errno != EEXIST)
    {
        std::cout << "[USER WEAPON SKIN SAVE FAIL] "
            << "cannot create " << OWNERSHIP_DIRECTORY
            << " error=" << errno << std::endl;
        return;
    }

    std::vector<std::string> userIds(knownUsers_.begin(), knownUsers_.end());
    std::sort(userIds.begin(), userIds.end());

    for (int weaponId = 0; weaponId < WEAPON_SKIN_WEAPON_COUNT; ++weaponId)
    {
        std::string path = GetOwnershipFilePath(weaponId);
        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
            continue;

        std::vector<int> skinIds;

        for (const auto& definitionPair : definitions_)
        {
            if (definitionPair.second.weaponId == weaponId)
                skinIds.push_back(definitionPair.second.skinId);
        }

        std::sort(skinIds.begin(), skinIds.end());

        file << "userId";
        for (int skinId : skinIds)
            file << ",skin" << skinId << "Owned";
        file << "\n";

        for (const std::string& userId : userIds)
        {
            const auto ownedIt = ownership_.find(userId);
            file << userId;

            for (int skinId : skinIds)
            {
                const bool owned = ownedIt != ownership_.end() &&
                    ownedIt->second.count(MakeKey(weaponId, skinId)) != 0;
                file << "," << (owned ? 1 : 0);
            }

            file << "\n";
        }
    }
}

void WeaponSkinManager::SaveLoadoutsUnlocked()
{
    if (_mkdir(LOADOUT_DIRECTORY) != 0 && errno != EEXIST)
    {
        std::cout << "[WEAPON SKIN LOADOUT SAVE FAIL] cannot create "
            << LOADOUT_DIRECTORY << " error=" << errno << std::endl;
        return;
    }

    std::vector<std::string> userIds(knownUsers_.begin(), knownUsers_.end());
    std::sort(userIds.begin(), userIds.end());

    for (int characterId = 0;
        characterId < WEAPON_SKIN_CHARACTER_COUNT;
        ++characterId)
    {
        std::ofstream file(
            GetLoadoutFilePath(characterId),
            std::ios::trunc);

        if (!file.is_open())
            continue;

        file << LOADOUT_HEADER << "\n";

        for (const std::string& userId : userIds)
        {
            file << userId;
            const auto userIt = loadouts_.find(userId);

            for (int weaponId = 0;
                weaponId < WEAPON_SKIN_WEAPON_COUNT;
                ++weaponId)
            {
                int skinId = 0;

                if (userIt != loadouts_.end())
                {
                    auto characterIt = userIt->second.find(characterId);

                    if (characterIt != userIt->second.end())
                    {
                        auto weaponIt = characterIt->second.find(weaponId);
                        if (weaponIt != characterIt->second.end())
                            skinId = weaponIt->second;
                    }
                }

                file << "," << skinId;
            }

            file << "\n";
        }
    }
}
