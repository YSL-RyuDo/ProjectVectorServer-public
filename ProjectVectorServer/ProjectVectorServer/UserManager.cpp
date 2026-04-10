#include "UserManager.h"
#include "Common.h"
#include "Session.h"

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
            if(std::isalpha(static_cast<unsigned char>(c)))
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
	std::lock_guard<std::mutex> lock(userManagerMutex_);

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
    data.playerIconId = 0;
    data.lobbyCharacterModelId = 0;
    data.lobbyBackgroundId = 0;
	playerData_[id] = data;

    SaveUnlocked();
    SavePlayerDataUnlocked();

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

    return (int)LoginResult::SUCCESS;
}


void UserManager::OnLogin(const std::string& id, Session* session)
{
	std::lock_guard<std::mutex> lock(userManagerMutex_);

    if (loggedInUsers_.count(id))
    {
        Session* oldSession = loggedInUsers_[id];

        if (oldSession && oldSession != session)
        {
            std::cout << "[DUP LOGIN] 기존 세션 종료 id=" << id << std::endl;
            oldSession->Disconnect();
        }
    }

    loggedInUsers_[id] = session;
    session->SetUserId(id);
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
    
    if (std::getline(file, line))
    {
        if (line != "id,nickname,level,lobbyCharacterModelId")
        {
            std::stringstream ss(line);
            std::string id, nick, levelStr, iconStr, modelStr,backgroundStr ;

            std::getline(ss, id, ',');
            std::getline(ss, nick, ',');
            std::getline(ss, levelStr, ',');
            std::getline(ss, iconStr, ',');
			std::getline(ss, modelStr, ',');
            std::getline(ss, backgroundStr, ',');

            PlayerData data;
            data.id = id;
            data.nickname = nick;
            data.level = std::stoi(levelStr);
			data.playerIconId = std::stoi(iconStr);
            data.lobbyCharacterModelId = std::stoi(modelStr);
            data.lobbyBackgroundId = std::stoi(backgroundStr);

            playerData_[id] = data;
        }
    }

    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string id, nick, levelStr, modelStr;

        std::getline(ss, id, ',');
        std::getline(ss, nick, ',');
        std::getline(ss, levelStr, ',');
        std::getline(ss, modelStr, ',');

        if (id.empty()) continue;

        PlayerData data;
        data.id = id;
        data.nickname = nick;
        data.level = std::stoi(levelStr);
        data.lobbyCharacterModelId = std::stoi(modelStr);

        playerData_[id] = data;
    }
}

void UserManager::SavePlayerDataUnlocked()
{
    std::ofstream file("player_data.csv", std::ios::trunc);
    if (!file.is_open())
        return;

    file << "id,nickname,level,playerIconId,lobbyCharacterModelId,lobbyBackgroundId\n";

    for (auto& pair : playerData_)
    {
        file << pair.second.id << ","
            << pair.second.nickname << ","
            << pair.second.level << ","
            << pair.second.playerIconId << ","
            << pair.second.lobbyCharacterModelId << ","
            << pair.second.lobbyBackgroundId << "\n";
    }
}

void UserManager::SavePlayerData()
{
    std::lock_guard<std::mutex> lock(userManagerMutex_);
    SavePlayerDataUnlocked();
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