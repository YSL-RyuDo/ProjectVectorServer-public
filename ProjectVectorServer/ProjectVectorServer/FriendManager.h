#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class FriendPresenceMode : uint8_t
{
    ONLINE = 0,
    DO_NOT_DISTURB = 1
};

enum class FriendOperationResult : uint8_t
{
    SUCCESS = 0,
    INVALID = 1,
    SELF = 2,
    ALREADY_FRIEND = 3,
    ALREADY_PENDING = 4,
    BLOCKED = 5,
    NOT_PENDING = 6,
    NOT_FRIEND = 7
};

struct FriendRequestRecord
{
    std::string senderUserId;
    std::string receiverUserId;
};

class FriendManager
{
public:
    static FriendManager& Instance();

    void Load();
    void InitializeUser(const std::string& userId);

    FriendPresenceMode GetPresenceMode(const std::string& userId);
    bool SetPresenceMode(
        const std::string& userId,
        FriendPresenceMode mode);

    bool AreFriends(
        const std::string& firstUserId,
        const std::string& secondUserId);
    bool IsBlockedEitherWay(
        const std::string& firstUserId,
        const std::string& secondUserId);
    bool CanRecommend(
        const std::string& userId,
        const std::string& candidateUserId);

    std::vector<std::string> GetFriends(const std::string& userId);
    std::vector<std::string> GetBlocked(const std::string& userId);
    std::vector<FriendRequestRecord> GetRequests(const std::string& userId);

    FriendOperationResult SendRequest(
        const std::string& senderUserId,
        const std::string& receiverUserId);
    FriendOperationResult CancelRequest(
        const std::string& senderUserId,
        const std::string& receiverUserId);
    FriendOperationResult RespondRequest(
        const std::string& receiverUserId,
        const std::string& senderUserId,
        bool accept);
    FriendOperationResult SetBlocked(
        const std::string& blockerUserId,
        const std::string& targetUserId,
        bool blocked);
    FriendOperationResult RemoveFriend(
        const std::string& userId,
        const std::string& targetUserId);

    std::string GetMemo(
        const std::string& ownerUserId,
        const std::string& targetUserId);
    bool SetMemo(
        const std::string& ownerUserId,
        const std::string& targetUserId,
        const std::string& memo);

private:
    static std::string MakeDirectedKey(
        const std::string& first,
        const std::string& second);
    static std::string MakeFriendKey(
        const std::string& first,
        const std::string& second);
    static bool SplitKey(
        const std::string& key,
        std::string& first,
        std::string& second);

    void LoadProfilesUnlocked();
    void LoadFriendsUnlocked();
    void LoadRequestsUnlocked();
    void LoadBlocksUnlocked();
    void LoadMemosUnlocked();

    void SaveProfilesUnlocked();
    void SaveFriendsUnlocked();
    void SaveRequestsUnlocked();
    void SaveBlocksUnlocked();
    void SaveMemosUnlocked();

    std::mutex mutex_;
    std::unordered_map<std::string, FriendPresenceMode> presenceModes_;
    std::unordered_set<std::string> friendships_;
    std::unordered_set<std::string> requests_;
    std::unordered_set<std::string> blocks_;
    std::unordered_map<std::string, std::string> memos_;
};
