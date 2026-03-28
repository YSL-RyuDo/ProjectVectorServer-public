#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

class Session;

struct User
{
    std::string id;
    std::string nickname;
	std::string salt;
    std::string passwordHash;
};

class UserManager
{
public:
    static UserManager& Instance();

    void Load();
    void Save();

    int Register(const std::string& id,const std::string& nick,const std::string& pw,const std::string& confirmPw);
    int Login(const std::string& id, const std::string& pw);

    void OnLogin(const std::string& id, Session* session);
    void OnLogout(Session* session);

private:

    void SaveUnlocked();
    std::unordered_map<std::string, User> users_;
    std::unordered_map<std::string, std::string> nicknameIndex_;
    std::unordered_map<std::string, Session*> loggedInUsers_;
	std::mutex userManagerMutex_;
};

std::string GenerateSalt(size_t byteSize = 16);
std::string HashPassword(const std::string& salt, const std::string& password);
bool VerifyPassword(const std::string& salt, const std::string& hash, const std::string& password);