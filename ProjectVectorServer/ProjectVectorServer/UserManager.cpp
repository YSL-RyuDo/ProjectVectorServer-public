#include "UserManager.h"
#include "CareerManager.h"
#include "FriendManager.h"
#include "ProfileManager.h"
#include "Common.h"
#include "Session.h"
#include "WeaponSkinManager.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <iomanip>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace
{
    constexpr ULONG PBKDF2_ITERATION = 100000;
    constexpr ULONG DERIVED_KEY_LENGTH = 32;

    constexpr const char* PLAYER_DATA_HEADER =
        "id,nickname,level,currentExp,playerIconId,logoBorderId,rankImageId,lobbyCharacterModelId,lobbyBackgroundId";

    constexpr const char* LEVEL_EXP_TABLE_HEADER =
        "level,requiredExp";

    int GetDefaultRequiredExpForLevel(int level)
    {
        if (level < 1)
            level = 1;

        return 100 + (level - 1) * 50;
    }

    std::vector<std::string> SplitCsvLine(const std::string& line)
    {
        std::vector<std::string> tokens;
        std::stringstream ss(line);
        std::string token;

        while (std::getline(ss, token, ','))
        {
            tokens.push_back(token);
        }

        return tokens;
    }

    std::string BytesToHex(const std::vector<uint8_t>& bytes)
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');

        for (uint8_t b : bytes)
        {
            oss << std::setw(2) << static_cast<int>(b);
        }

        return oss.str();
    }

    std::vector<uint8_t> HexToBytes(const std::string& hex)
    {
        std::vector<uint8_t> out;

        if (hex.size() % 2 != 0)
            return out;

        for (size_t i = 0; i < hex.size(); i += 2)
        {
            std::string byteStr = hex.substr(i, 2);
            uint8_t value = static_cast<uint8_t>(strtoul(byteStr.c_str(), nullptr, 16));
            out.push_back(value);
        }

        return out;
    }

    bool IsValidId(const std::string& id)
    {
        static const std::regex pattern("^[a-zA-Z0-9_]{4,20}$");
        return std::regex_match(id, pattern);
    }

    bool IsValidNickname(const std::string& nick)
    {
        return !nick.empty() && nick.size() >= 2 && nick.size() <= 16;
    }

    bool IsValidPassword(const std::string& pw)
    {
        if (pw.size() < 8 || pw.size() > 64)
            return false;

        bool hasAlpha = false;
        bool hasDigit = false;

        for (char c : pw)
        {
            if (std::isalpha(static_cast<unsigned char>(c)))
                hasAlpha = true;
            if (std::isdigit(static_cast<unsigned char>(c)))
                hasDigit = true;
        }

        return hasAlpha && hasDigit;
    }
}

UserManager& UserManager::Instance()
{
    static UserManager inst;
    return inst;
}

std::string GenerateSalt(size_t byteSize)
{
	std::vector<uint8_t> salt(byteSize);

	NTSTATUS status = BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (status < 0)
        return "";

	return BytesToHex(salt);
}

std::string HashPassword(const std::string& salt, const std::string& password)
{
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	std::vector<uint8_t> derived(DERIVED_KEY_LENGTH);

    std::vector<uint8_t> saltBytes = HexToBytes(salt);
	if (saltBytes.empty())
		return "";

	NTSTATUS status = BCryptOpenAlgorithmProvider(
        &hAlg,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        BCRYPT_ALG_HANDLE_HMAC_FLAG);

	if (status < 0)
		return "";

    status = BCryptDeriveKeyPBKDF2(
        hAlg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        saltBytes.data(),
        static_cast<ULONG>(saltBytes.size()),
        PBKDF2_ITERATION,
        derived.data(),
        static_cast<ULONG>(derived.size()),
        0);

	BCryptCloseAlgorithmProvider(hAlg, 0);

	if (status < 0)
		return "";

	return BytesToHex(derived);
}

bool VerifyPassword(const std::string& salt, const std::string& hash, const std::string& password)
{
	std::string newHash = HashPassword(salt, password);

	if (newHash.empty() || hash.empty() || newHash.size() != hash.size())
		return false;

    unsigned char diff = 0;
    for (size_t i = 0; i < hash.size(); ++i)
        diff |= static_cast<unsigned char>(newHash[i] ^ hash[i]);

	return diff == 0;
}

void UserManager::Load()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    users_.clear();
    nicknameIndex_.clear();

    std::ifstream file("users.csv");
    if (!file.is_open())
        return;

    std::string line;

    if (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line == "id,nickname,salt,passwordHash")
        {
            // 헤더 라인, 무시
        }
        else
        {
            std::stringstream ss(line);
            std::string id, nick, salt, hash;

            std::getline(ss, id, ',');
            std::getline(ss, nick, ',');
            std::getline(ss, salt, ',');
            std::getline(ss, hash, ',');

            if (!id.empty() && !nick.empty() && !salt.empty() && !hash.empty())
            {
                User user{ id, nick, salt, hash };
                users_[id] = user;
                nicknameIndex_[nick] = id;
            }
        }
    }

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::stringstream ss(line);
        std::string id, nick, salt, hash;

        std::getline(ss, id, ',');
        std::getline(ss, nick, ',');
        std::getline(ss, salt, ',');
        std::getline(ss, hash, ',');

        if (id.empty() || nick.empty() || salt.empty() || hash.empty())
            continue;

        User user{ id, nick, salt, hash };
        users_[id] = user;
        nicknameIndex_[nick] = id;
    }
}

void UserManager::Save()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    SaveUnlocked();
}

void UserManager::SaveUnlocked()
{
    std::ofstream file("users.csv", std::ios::trunc);
    if (!file.is_open())
    {
        std::cout << "[SAVE FAIL] users.csv open failed" << std::endl;
        return;
    }

    file << "id,nickname,salt,passwordHash\n";

    for (auto& pair : users_)
    {
        file << pair.second.id << ","
            << pair.second.nickname << ","
            << pair.second.salt << ","
            << pair.second.passwordHash << "\n";
    }
}

int UserManager::Register(const std::string& id,const std::string& nick,const std::string& pw,const std::string& confirmPw)
{
	std::unique_lock<std::mutex> lock(userManagerMutex_);

    if (!IsValidId(id) || !IsValidNickname(nick) || !IsValidPassword(pw))
        return (int)RegisterResult::INVALID_INPUT;

    if (users_.count(id))
        return (int)RegisterResult::ID_DUPLICATE;

    if (nicknameIndex_.count(nick))
        return (int)RegisterResult::NICK_DUPLICATE;

    if (pw != confirmPw)
        return (int)RegisterResult::PASSWORD_MISMATCH;

    std::string salt = GenerateSalt();
    if(salt.empty())
		return (int)RegisterResult::INVALID_INPUT;

    std::string hash = HashPassword(salt, pw);
    if (hash.empty())
        return (int)RegisterResult::INVALID_INPUT;

    User user{ id, nick, salt, hash };
    users_[id] = user;
    nicknameIndex_[nick] = id;

    PlayerData data;
    data.id = id;
    data.nickname = nick;
    data.level = 1;
    data.currentExp = 0;
    data.playerIconId = 0;
    data.logoBorderId = 0;
    data.rankImageId = 0;
    data.lobbyCharacterModelId = 0;
    data.lobbyBackgroundId = 0;
    playerData_[id] = data;

    CharacterOwnedData ownedData;
    ownedData.id = id;
    ownedData.c0 = 1; // 기본 지급 캐릭터
    ownedData.c1 = 1;
    ownedData.c2 = 1;
    ownedData.c3 = 1;

    characterOwnedData_[id] = ownedData;

    CharacterAffinityData affinityData;
    affinityData.id = id;
    affinityData.c0 = 0;
    affinityData.c1 = 0;
    affinityData.c2 = 0;
    affinityData.c3 = 0;

    characterAffinityData_[id] = affinityData;
    SaveUnlocked();
    SavePlayerDataUnlocked();
    SaveCharacterOwnedDataUnlocked();
    SaveCharacterAffinityDataUnlocked();
    // Profile initialization reads the newly saved PlayerData through GetPlayerData.
    // Do not hold the non-recursive user lock across other managers (Profile -> User).
    lock.unlock();
    WeaponSkinManager::Instance().InitializeUser(id);
    CareerManager::Instance().InitializeUser(id, nick);
    FriendManager::Instance().InitializeUser(id);
    ProfileManager::Instance().InitializeUser(id);

    return (int)RegisterResult::SUCCESS;
}

int UserManager::Login(const std::string& id, const std::string& pw)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    if (!IsValidId(id) || pw.empty())
        return (int)LoginResult::INVALID_INPUT;

    auto it = users_.find(id);
    if (it == users_.end())
        return (int)LoginResult::NO_ID;

    const User& user = it->second;

    if (!VerifyPassword(user.salt, user.passwordHash, pw))
        return (int)LoginResult::WRONG_PASSWORD;

    auto loginIt = loggedInUsers_.find(id);
    if (loginIt != loggedInUsers_.end() && loginIt->second != nullptr)
    {
        std::cout << "[LOGIN BLOCK] already logged in id=" << id << std::endl;
        return (int)LoginResult::ALREADY_LOGIN;
    }

    return (int)LoginResult::SUCCESS;
}


bool UserManager::OnLogin(const std::string& id, Session* session)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = loggedInUsers_.find(id);
    if (it != loggedInUsers_.end() && it->second != nullptr && it->second != session)
    {
        std::cout << "[LOGIN BLOCK ONLOGIN] already logged in id=" << id << std::endl;
        return false;
    }

    loggedInUsers_[id] = session;
    session->SetUserId(id);

    return true;
}

void UserManager::OnLogout(Session* session)
{
	std::lock_guard<std::mutex> lock(userManagerMutex_);

    std::string id = session->GetUserId();

    if (id.empty())
        return;

	auto it = loggedInUsers_.find(id);
	if (it != loggedInUsers_.end() && it->second == session)
	{
		loggedInUsers_.erase(it);
		std::cout << "[LOGOUT] 세션 종료 id=" << id << std::endl;
	}
}

void UserManager::LoadPlayerData()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    playerData_.clear();

    std::ifstream file("player_data.csv");
    if (!file.is_open())
        return;

    std::string line;
    bool firstLine = true;

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
            continue;

        if (firstLine)
        {
            firstLine = false;

            if (line != PLAYER_DATA_HEADER)
            {
                std::cout << "[LOAD PLAYER DATA FAIL] invalid header: " << line << std::endl;
                return;
            }

            continue;
        }

        std::vector<std::string> tokens = SplitCsvLine(line);
        if (tokens.size() < 9)
        {
            std::cout << "[LOAD PLAYER DATA SKIP] invalid column count line=" << line << std::endl;
            continue;
        }

        PlayerData data;
        data.id = tokens[0];
        data.nickname = tokens[1];
        data.level = std::stoi(tokens[2]);
        data.currentExp = std::stoi(tokens[3]);
        data.playerIconId = std::stoi(tokens[4]);
        data.logoBorderId = std::stoi(tokens[5]);
        data.rankImageId = std::stoi(tokens[6]);
        data.lobbyCharacterModelId = std::stoi(tokens[7]);
        data.lobbyBackgroundId = std::stoi(tokens[8]);

        if (!data.id.empty())
            playerData_[data.id] = data;
    }
}

void UserManager::SavePlayerDataUnlocked()
{
    std::ofstream file("player_data.csv", std::ios::trunc);
    if (!file.is_open())
        return;

    file << PLAYER_DATA_HEADER << "\n";

    for (auto& pair : playerData_)
    {
        file << pair.second.id << ","
            << pair.second.nickname << ","
            << pair.second.level << ","
            << pair.second.currentExp << ","
            << pair.second.playerIconId << ","
            << pair.second.logoBorderId << ","
            << pair.second.rankImageId << ","
            << pair.second.lobbyCharacterModelId << ","
            << pair.second.lobbyBackgroundId << "\n";
    }
}

void UserManager::SavePlayerData()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    SavePlayerDataUnlocked();
}

void UserManager::LoadLevelExpTable()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    levelExpTable_.clear();

    for (int level = 1; level <= 100; ++level)
    {
        levelExpTable_[level] = GetDefaultRequiredExpForLevel(level);
    }

    std::ifstream file("level_exp_table.csv");
    if (!file.is_open())
    {
        SaveDefaultLevelExpTableUnlocked();
        return;
    }

    std::string line;
    bool firstLine = true;

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
            continue;

        if (firstLine)
        {
            firstLine = false;

            if (line != LEVEL_EXP_TABLE_HEADER)
            {
                std::cout << "[LOAD LEVEL EXP TABLE FAIL] invalid header: " << line << std::endl;
                return;
            }

            continue;
        }

        std::vector<std::string> tokens = SplitCsvLine(line);
        if (tokens.size() < 2)
            continue;

        int level = std::stoi(tokens[0]);
        int requiredExp = std::stoi(tokens[1]);

        if (level >= 1 && requiredExp > 0)
            levelExpTable_[level] = requiredExp;
    }
}

bool UserManager::GetPlayerData(const std::string& id, PlayerData& outData)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = playerData_.find(id);
    if (it == playerData_.end())
        return false;

    outData = it->second;
    return true;
}

std::vector<PlayerData> UserManager::GetAllPlayerData()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    std::vector<PlayerData> result;
    result.reserve(playerData_.size());
    for (const auto& pair : playerData_)
        result.push_back(pair.second);
    return result;
}

bool UserManager::SetLobbyCharacterModel(const std::string& id, int modelId)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = playerData_.find(id);
    if (it == playerData_.end())
        return false;

    it->second.lobbyCharacterModelId = modelId;
    SavePlayerDataUnlocked();
    return true;
}

bool UserManager::SetLobbyBackground(const std::string& id, int backgroundId)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = playerData_.find(id);
    if (it == playerData_.end())
        return false;

    it->second.lobbyBackgroundId = backgroundId;
    SavePlayerDataUnlocked();
    return true;
}

bool UserManager::SetPlayerIcon(const std::string& id, int iconId)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = playerData_.find(id);
    if (it == playerData_.end())
        return false;

    it->second.playerIconId = iconId;
    SavePlayerDataUnlocked();
    return true;
}

bool UserManager::SetLogoBorder(const std::string& id, int borderId)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = playerData_.find(id);
    if (it == playerData_.end())
        return false;

    it->second.logoBorderId = borderId;
    SavePlayerDataUnlocked();
    return true;
}

bool UserManager::SetRankImage(const std::string& id, int rankImageId)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = playerData_.find(id);
    if (it == playerData_.end())
        return false;

    it->second.rankImageId = rankImageId;
    SavePlayerDataUnlocked();
    return true;
}

void UserManager::SetRankImages(
    const std::vector<std::pair<std::string, int>>& rankImages)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    bool changed = false;
    for (const auto& rankImage : rankImages)
    {
        auto it = playerData_.find(rankImage.first);
        if (it == playerData_.end() ||
            it->second.rankImageId == rankImage.second)
        {
            continue;
        }

        it->second.rankImageId = rankImage.second;
        changed = true;
    }

    if (changed)
        SavePlayerDataUnlocked();
}

int UserManager::GetRequiredExpForLevel(int level)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = levelExpTable_.find(level);
    if (it != levelExpTable_.end())
        return it->second;

    return GetDefaultRequiredExpForLevel(level);
}

bool UserManager::AddExp(const std::string& id, int amount, int& outLevel, int& outCurrentExp, int& outRequiredExp)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = playerData_.find(id);
    if (it == playerData_.end())
        return false;

    if (amount < 0)
        return false;

    PlayerData& data = it->second;
    data.currentExp += amount;

    while (true)
    {
        int requiredExp = GetDefaultRequiredExpForLevel(data.level);

        auto expIt = levelExpTable_.find(data.level);
        if (expIt != levelExpTable_.end())
            requiredExp = expIt->second;

        if (data.currentExp < requiredExp)
        {
            outLevel = data.level;
            outCurrentExp = data.currentExp;
            outRequiredExp = requiredExp;
            break;
        }

        data.currentExp -= requiredExp;
        data.level += 1;
    }

    SavePlayerDataUnlocked();
    return true;
}

void UserManager::LoadCharacterOwnedData()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    characterOwnedData_.clear();

    std::ifstream file("character_owned.csv");
    if (!file.is_open())
        return;

    std::string line;

    if (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
    }

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::stringstream ss(line);
        std::string id, c0Str, c1Str, c2Str, c3Str;

        std::getline(ss, id, ',');
        std::getline(ss, c0Str, ',');
        std::getline(ss, c1Str, ',');
        std::getline(ss, c2Str, ',');
        std::getline(ss, c3Str, ',');

        if (id.empty())
            continue;

        CharacterOwnedData data;
        data.id = id;
        data.c0 = std::stoi(c0Str);
        data.c1 = std::stoi(c1Str);
        data.c2 = std::stoi(c2Str);
        data.c3 = std::stoi(c3Str);

        characterOwnedData_[id] = data;
    }
}

void UserManager::SaveCharacterOwnedData()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    SaveCharacterOwnedDataUnlocked();
}

void UserManager::SaveCharacterOwnedDataUnlocked()
{
    std::ofstream file("character_owned.csv", std::ios::trunc);
    if (!file.is_open())
        return;

    file << "id,c0,c1,c2,c3\n";

    for (auto& pair : characterOwnedData_)
    {
        file << pair.second.id << ","
            << pair.second.c0 << ","
            << pair.second.c1 << ","
            << pair.second.c2 << ","
            << pair.second.c3 << "\n";
    }
}

void UserManager::LoadCharacterAffinityData()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    characterAffinityData_.clear();

    std::ifstream file("character_affinity.csv");
    if (!file.is_open())
        return;

    std::string line;

    if (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
    }

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::stringstream ss(line);
        std::string id, c0Str, c1Str, c2Str, c3Str;

        std::getline(ss, id, ',');
        std::getline(ss, c0Str, ',');
        std::getline(ss, c1Str, ',');
        std::getline(ss, c2Str, ',');
        std::getline(ss, c3Str, ',');

        if (id.empty())
            continue;

        CharacterAffinityData data;
        data.id = id;
        data.c0 = std::stoi(c0Str);
        data.c1 = std::stoi(c1Str);
        data.c2 = std::stoi(c2Str);
        data.c3 = std::stoi(c3Str);

        characterAffinityData_[id] = data;
    }
}

void UserManager::SaveCharacterAffinityData()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    SaveCharacterAffinityDataUnlocked();
}

void UserManager::SaveCharacterAffinityDataUnlocked()
{
    std::ofstream file("character_affinity.csv", std::ios::trunc);
    if (!file.is_open())
        return;

    file << "id,c0,c1,c2,c3\n";

    for (auto& pair : characterAffinityData_)
    {
        file << pair.second.id << ","
            << pair.second.c0 << ","
            << pair.second.c1 << ","
            << pair.second.c2 << ","
            << pair.second.c3 << "\n";
    }
}

void UserManager::SaveDefaultLevelExpTableUnlocked()
{
    std::ofstream file("level_exp_table.csv", std::ios::trunc);
    if (!file.is_open())
        return;

    file << LEVEL_EXP_TABLE_HEADER << "\n";

    for (int level = 1; level <= 100; ++level)
    {
        file << level << "," << GetDefaultRequiredExpForLevel(level) << "\n";
    }
}

bool UserManager::GetCharacterOwnedData(const std::string& id, CharacterOwnedData& outData)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = characterOwnedData_.find(id);
    if (it == characterOwnedData_.end())
        return false;

    outData = it->second;
    return true;
}

bool UserManager::GetCharacterAffinityData(const std::string& id, CharacterAffinityData& outData)
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);

    auto it = characterAffinityData_.find(id);
    if (it == characterAffinityData_.end())
        return false;

    outData = it->second;
    return true;
}

bool UserManager::IsCharacterOwned(const std::string& id, int characterId)
{
	std::lock_guard<std::mutex> lock(userManagerMutex_);

	auto it = characterOwnedData_.find(id);
	if (it == characterOwnedData_.end())
		return false;

	const CharacterOwnedData& data = it->second;
	switch (characterId)
	{
	case 0: return data.c0 != 0;
	case 1: return data.c1 != 0;
	case 2: return data.c2 != 0;
	case 3: return data.c3 != 0;
	default:
		return false;
	}
}
