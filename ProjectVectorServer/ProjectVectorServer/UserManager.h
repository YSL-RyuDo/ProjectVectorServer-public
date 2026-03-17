#pragma once
#include <string>
#include <unordered_map>

struct User
{
    std::string id;
    std::string nickname;
    std::string passwordHash;
};

class UserManager
{
public:
    static UserManager& Instance();

    void Load();
    void Save();

    int Register(const std::string& id,
        const std::string& nick,
        const std::string& pw,
        const std::string& confirmPw);

    int Login(const std::string& id, const std::string& pw);

private:
    std::unordered_map<std::string, User> users_;
    std::unordered_map<std::string, std::string> nicknameIndex_;
};

std::string HashPassword(const std::string& id, const std::string& password);
bool VerifyPassword(const std::string& id, const std::string& hash, const std::string& password);