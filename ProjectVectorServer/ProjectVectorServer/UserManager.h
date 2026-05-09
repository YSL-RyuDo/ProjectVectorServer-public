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

struct PlayerData
{
    std::string id;
    std::string nickname;

    int level;
    int currentExp;

    int playerIconId;
    int logoBorderId;
    int rankImageId;

    int lobbyCharacterModelId;
    int lobbyBackgroundId;
};

struct CharacterOwnedData
{
    std::string id;
    int c0;
    int c1;
    int c2;
    int c3;
};

struct CharacterAffinityData
{
    std::string id;
    int c0;
    int c1;
    int c2;
    int c3;
};

class UserManager
{
public:
    static UserManager& Instance();

    void Load();
    void Save();

    int Register(const std::string& id,const std::string& nick,const std::string& pw,const std::string& confirmPw);
    int Login(const std::string& id, const std::string& pw);

    bool OnLogin(const std::string& id, Session* session);
    void OnLogout(Session* session);

    void LoadPlayerData();
    void SavePlayerData();

    void LoadLevelExpTable();

    bool GetPlayerData(const std::string& id, PlayerData& outData);

    bool SetLobbyCharacterModel(const std::string& id, int modelId);
    bool SetLobbyBackground(const std::string& id, int backgroundId);

    bool SetPlayerIcon(const std::string& id, int iconId);
    bool SetLogoBorder(const std::string& id, int borderId);
    bool SetRankImage(const std::string& id, int rankImageId);

    int GetRequiredExpForLevel(int level);
    bool AddExp(const std::string& id, int amount, int& outLevel, int& outCurrentExp, int& outRequiredExp);

    void LoadCharacterOwnedData();
    void SaveCharacterOwnedData();

    void LoadCharacterAffinityData();
    void SaveCharacterAffinityData();

    bool GetCharacterOwnedData(const std::string& id, CharacterOwnedData& outData);
    bool GetCharacterAffinityData(const std::string& id, CharacterAffinityData& outData);

	bool IsCharacterOwned(const std::string& id, int characterId);

private:

    void SaveUnlocked();
    void SavePlayerDataUnlocked();
    void SaveCharacterOwnedDataUnlocked();
    void SaveCharacterAffinityDataUnlocked();
    void SaveDefaultLevelExpTableUnlocked();

    std::unordered_map<std::string, User> users_;
    std::unordered_map<std::string, std::string> nicknameIndex_;
    std::unordered_map<std::string, Session*> loggedInUsers_;
	std::mutex userManagerMutex_;

    std::unordered_map<std::string, PlayerData> playerData_;
    std::unordered_map<std::string, CharacterOwnedData> characterOwnedData_;
    std::unordered_map<std::string, CharacterAffinityData> characterAffinityData_;
    std::unordered_map<int, int> levelExpTable_;
};

std::string GenerateSalt(size_t byteSize = 16);
std::string HashPassword(const std::string& salt, const std::string& password);
bool VerifyPassword(const std::string& salt, const std::string& hash, const std::string& password);