#include "UserManager.h"
#include "Common.h"
#include <fstream>
#include <sstream>

UserManager& UserManager::Instance()
{
    static UserManager inst;
    return inst;
}

uint64_t SimpleHash(const std::string& input)
{
    uint64_t hash = 1469598103934665603ULL;

    for (char c : input)
    {
        hash ^= (uint64_t)c;
        hash *= 1099511628211ULL;
    }

    return hash;
}

std::string HashPassword(const std::string& id, const std::string& password)
{
    std::string salt = id; // deterministic salt

    std::string combined = password + salt;

    uint64_t hash = 0;

    for (int i = 0; i < 10000; i++)
    {
        combined = std::to_string(SimpleHash(combined));
    }

    return combined;
}

bool VerifyPassword(const std::string& id, const std::string& hash, const std::string& password)
{
    return HashPassword(id, password) == hash;
}


void UserManager::Load()
{
    std::ifstream file("users.csv");

    std::string line;
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string id, nick, hash;

        std::getline(ss, id, ',');
        std::getline(ss, nick, ',');
        std::getline(ss, hash, ',');

        User user{ id, nick, hash };
        users_[id] = user;
        nicknameIndex_[nick] = id;
    }
}

void UserManager::Save()
{
    std::ofstream file("users.csv", std::ios::trunc);

    for (auto& pair : users_)
    {
        file << pair.second.id << ","
            << pair.second.nickname << ","
            << pair.second.passwordHash << "\n";
    }
}

int UserManager::Register(const std::string& id,const std::string& nick,const std::string& pw,const std::string& confirmPw)
{
    if (id.empty() || nick.empty() || pw.empty())
        return (int)RegisterResult::INVALID_INPUT;

    if (users_.count(id))
        return (int)RegisterResult::ID_DUPLICATE;

    if (nicknameIndex_.count(nick))
        return (int)RegisterResult::NICK_DUPLICATE;

    if (pw != confirmPw)
        return (int)RegisterResult::PASSWORD_MISMATCH;

    std::string hash = HashPassword(id, pw);

    User user{ id, nick, hash };
    users_[id] = user;
    nicknameIndex_[nick] = id;

    Save();

    return (int)RegisterResult::SUCCESS;
}

int UserManager::Login(const std::string& id, const std::string& pw)
{
    if (id.empty() || pw.empty())
        return (int)LoginResult::INVALID_INPUT;

    if (!users_.count(id))
        return (int)LoginResult::NO_ID;

    if (!VerifyPassword(id, users_[id].passwordHash, pw))
        return (int)LoginResult::WRONG_PASSWORD;

    return (int)LoginResult::SUCCESS;
}