#include "FriendManager.h"

#include <algorithm>
#include <fstream>

namespace
{
    constexpr char KEY_SEPARATOR = '\x1f';
    constexpr const char* PROFILES_FILE = "friend_profiles.csv";
    constexpr const char* FRIENDS_FILE = "friend_relations.csv";
    constexpr const char* REQUESTS_FILE = "friend_requests.csv";
    constexpr const char* BLOCKS_FILE = "friend_blocks.csv";
    constexpr const char* MEMOS_FILE = "friend_memos.csv";

    std::vector<std::string> ReadCsvRow(const std::string& line)
    {
        std::vector<std::string> fields;
        size_t start = 0;
        while (start <= line.size())
        {
            size_t comma = line.find(',', start);
            if (comma == std::string::npos)
            {
                fields.push_back(line.substr(start));
                break;
            }

            fields.push_back(line.substr(start, comma - start));
            start = comma + 1;
        }
        return fields;
    }

    std::vector<std::string> SortedKeys(
        const std::unordered_set<std::string>& values)
    {
        std::vector<std::string> result(values.begin(), values.end());
        std::sort(result.begin(), result.end());
        return result;
    }
}

FriendManager& FriendManager::Instance()
{
    static FriendManager instance;
    return instance;
}

void FriendManager::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    presenceModes_.clear();
    friendships_.clear();
    requests_.clear();
    blocks_.clear();
    memos_.clear();

    LoadProfilesUnlocked();
    LoadFriendsUnlocked();
    LoadRequestsUnlocked();
    LoadBlocksUnlocked();
    LoadMemosUnlocked();
}

void FriendManager::InitializeUser(const std::string& userId)
{
    if (userId.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (presenceModes_.count(userId) == 0)
    {
        presenceModes_[userId] = FriendPresenceMode::ONLINE;
        SaveProfilesUnlocked();
    }
}

FriendPresenceMode FriendManager::GetPresenceMode(const std::string& userId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = presenceModes_.find(userId);
    return it == presenceModes_.end()
        ? FriendPresenceMode::ONLINE
        : it->second;
}

bool FriendManager::SetPresenceMode(
    const std::string& userId,
    FriendPresenceMode mode)
{
    if (userId.empty())
        return false;

    if (mode != FriendPresenceMode::ONLINE &&
        mode != FriendPresenceMode::DO_NOT_DISTURB)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    presenceModes_[userId] = mode;
    SaveProfilesUnlocked();
    return true;
}

bool FriendManager::AreFriends(
    const std::string& firstUserId,
    const std::string& secondUserId)
{
    if (firstUserId.empty() || secondUserId.empty())
        return false;

    std::lock_guard<std::mutex> lock(mutex_);
    return friendships_.count(
        MakeFriendKey(firstUserId, secondUserId)) != 0;
}

bool FriendManager::IsBlockedEitherWay(
    const std::string& firstUserId,
    const std::string& secondUserId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.count(MakeDirectedKey(firstUserId, secondUserId)) != 0 ||
        blocks_.count(MakeDirectedKey(secondUserId, firstUserId)) != 0;
}

bool FriendManager::CanRecommend(
    const std::string& userId,
    const std::string& candidateUserId)
{
    if (userId.empty() || candidateUserId.empty() ||
        userId == candidateUserId)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (friendships_.count(MakeFriendKey(userId, candidateUserId)) != 0)
        return false;

    if (blocks_.count(MakeDirectedKey(userId, candidateUserId)) != 0 ||
        blocks_.count(MakeDirectedKey(candidateUserId, userId)) != 0)
    {
        return false;
    }

    return requests_.count(MakeDirectedKey(userId, candidateUserId)) == 0 &&
        requests_.count(MakeDirectedKey(candidateUserId, userId)) == 0;
}

std::vector<std::string> FriendManager::GetFriends(const std::string& userId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;

    for (const std::string& key : friendships_)
    {
        std::string first;
        std::string second;
        if (!SplitKey(key, first, second))
            continue;

        if (first == userId)
            result.push_back(second);
        else if (second == userId)
            result.push_back(first);
    }

    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> FriendManager::GetBlocked(const std::string& userId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;

    for (const std::string& key : blocks_)
    {
        std::string blocker;
        std::string target;
        if (SplitKey(key, blocker, target) && blocker == userId)
            result.push_back(target);
    }

    std::sort(result.begin(), result.end());
    return result;
}

std::vector<FriendRequestRecord> FriendManager::GetRequests(
    const std::string& userId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FriendRequestRecord> result;

    for (const std::string& key : requests_)
    {
        FriendRequestRecord request{};
        if (!SplitKey(key, request.senderUserId, request.receiverUserId))
            continue;

        if (request.senderUserId == userId || request.receiverUserId == userId)
            result.push_back(request);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const FriendRequestRecord& lhs, const FriendRequestRecord& rhs)
        {
            if (lhs.senderUserId != rhs.senderUserId)
                return lhs.senderUserId < rhs.senderUserId;
            return lhs.receiverUserId < rhs.receiverUserId;
        });
    return result;
}

FriendOperationResult FriendManager::SendRequest(
    const std::string& senderUserId,
    const std::string& receiverUserId)
{
    if (senderUserId.empty() || receiverUserId.empty())
        return FriendOperationResult::INVALID;
    if (senderUserId == receiverUserId)
        return FriendOperationResult::SELF;

    std::lock_guard<std::mutex> lock(mutex_);
    if (friendships_.count(MakeFriendKey(senderUserId, receiverUserId)) != 0)
        return FriendOperationResult::ALREADY_FRIEND;

    if (blocks_.count(MakeDirectedKey(senderUserId, receiverUserId)) != 0 ||
        blocks_.count(MakeDirectedKey(receiverUserId, senderUserId)) != 0)
    {
        return FriendOperationResult::BLOCKED;
    }

    if (requests_.count(MakeDirectedKey(senderUserId, receiverUserId)) != 0 ||
        requests_.count(MakeDirectedKey(receiverUserId, senderUserId)) != 0)
    {
        return FriendOperationResult::ALREADY_PENDING;
    }

    requests_.insert(MakeDirectedKey(senderUserId, receiverUserId));
    SaveRequestsUnlocked();
    return FriendOperationResult::SUCCESS;
}

FriendOperationResult FriendManager::CancelRequest(
    const std::string& senderUserId,
    const std::string& receiverUserId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t erased = requests_.erase(
        MakeDirectedKey(senderUserId, receiverUserId));
    if (erased == 0)
        return FriendOperationResult::NOT_PENDING;

    SaveRequestsUnlocked();
    return FriendOperationResult::SUCCESS;
}

FriendOperationResult FriendManager::RespondRequest(
    const std::string& receiverUserId,
    const std::string& senderUserId,
    bool accept)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string requestKey = MakeDirectedKey(senderUserId, receiverUserId);
    if (requests_.erase(requestKey) == 0)
        return FriendOperationResult::NOT_PENDING;

    if (accept)
    {
        if (blocks_.count(MakeDirectedKey(senderUserId, receiverUserId)) != 0 ||
            blocks_.count(MakeDirectedKey(receiverUserId, senderUserId)) != 0)
        {
            SaveRequestsUnlocked();
            return FriendOperationResult::BLOCKED;
        }

        friendships_.insert(MakeFriendKey(senderUserId, receiverUserId));
        SaveFriendsUnlocked();
    }

    SaveRequestsUnlocked();
    return FriendOperationResult::SUCCESS;
}

FriendOperationResult FriendManager::SetBlocked(
    const std::string& blockerUserId,
    const std::string& targetUserId,
    bool blocked)
{
    if (blockerUserId.empty() || targetUserId.empty())
        return FriendOperationResult::INVALID;
    if (blockerUserId == targetUserId)
        return FriendOperationResult::SELF;

    std::lock_guard<std::mutex> lock(mutex_);
    std::string blockKey = MakeDirectedKey(blockerUserId, targetUserId);

    if (blocked)
    {
        blocks_.insert(blockKey);
        friendships_.erase(MakeFriendKey(blockerUserId, targetUserId));
        memos_.erase(MakeDirectedKey(blockerUserId, targetUserId));
        memos_.erase(MakeDirectedKey(targetUserId, blockerUserId));
        requests_.erase(MakeDirectedKey(blockerUserId, targetUserId));
        requests_.erase(MakeDirectedKey(targetUserId, blockerUserId));
        SaveFriendsUnlocked();
        SaveRequestsUnlocked();
        SaveMemosUnlocked();
    }
    else
    {
        blocks_.erase(blockKey);
    }

    SaveBlocksUnlocked();
    return FriendOperationResult::SUCCESS;
}

FriendOperationResult FriendManager::RemoveFriend(
    const std::string& userId,
    const std::string& targetUserId)
{
    if (userId.empty() || targetUserId.empty())
        return FriendOperationResult::INVALID;
    if (userId == targetUserId)
        return FriendOperationResult::SELF;

    std::lock_guard<std::mutex> lock(mutex_);
    if (friendships_.erase(MakeFriendKey(userId, targetUserId)) == 0)
        return FriendOperationResult::NOT_FRIEND;

    memos_.erase(MakeDirectedKey(userId, targetUserId));
    memos_.erase(MakeDirectedKey(targetUserId, userId));
    SaveFriendsUnlocked();
    SaveMemosUnlocked();
    return FriendOperationResult::SUCCESS;
}

std::string FriendManager::GetMemo(
    const std::string& ownerUserId,
    const std::string& targetUserId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memos_.find(MakeDirectedKey(ownerUserId, targetUserId));
    return it == memos_.end() ? std::string() : it->second;
}

bool FriendManager::SetMemo(
    const std::string& ownerUserId,
    const std::string& targetUserId,
    const std::string& memo)
{
    if (ownerUserId.empty() || targetUserId.empty() ||
        memo.size() >= 32 || memo.find_first_of(",\r\n") != std::string::npos)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (friendships_.count(MakeFriendKey(ownerUserId, targetUserId)) == 0)
        return false;

    std::string key = MakeDirectedKey(ownerUserId, targetUserId);
    if (memo.empty())
        memos_.erase(key);
    else
        memos_[key] = memo;

    SaveMemosUnlocked();
    return true;
}

std::string FriendManager::MakeDirectedKey(
    const std::string& first,
    const std::string& second)
{
    return first + KEY_SEPARATOR + second;
}

std::string FriendManager::MakeFriendKey(
    const std::string& first,
    const std::string& second)
{
    return first < second
        ? MakeDirectedKey(first, second)
        : MakeDirectedKey(second, first);
}

bool FriendManager::SplitKey(
    const std::string& key,
    std::string& first,
    std::string& second)
{
    size_t separator = key.find(KEY_SEPARATOR);
    if (separator == std::string::npos)
        return false;

    first = key.substr(0, separator);
    second = key.substr(separator + 1);
    return !first.empty() && !second.empty();
}

void FriendManager::LoadProfilesUnlocked()
{
    std::ifstream file(PROFILES_FILE);
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ReadCsvRow(line);
        if (fields.size() != 2 || fields[0].empty())
            continue;

        presenceModes_[fields[0]] = fields[1] == "1"
            ? FriendPresenceMode::DO_NOT_DISTURB
            : FriendPresenceMode::ONLINE;
    }
}

void FriendManager::LoadFriendsUnlocked()
{
    std::ifstream file(FRIENDS_FILE);
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ReadCsvRow(line);
        if (fields.size() == 2 && fields[0] != fields[1])
            friendships_.insert(MakeFriendKey(fields[0], fields[1]));
    }
}

void FriendManager::LoadRequestsUnlocked()
{
    std::ifstream file(REQUESTS_FILE);
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ReadCsvRow(line);
        if (fields.size() == 2 && fields[0] != fields[1])
            requests_.insert(MakeDirectedKey(fields[0], fields[1]));
    }
}

void FriendManager::LoadBlocksUnlocked()
{
    std::ifstream file(BLOCKS_FILE);
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ReadCsvRow(line);
        if (fields.size() == 2 && fields[0] != fields[1])
            blocks_.insert(MakeDirectedKey(fields[0], fields[1]));
    }
}

void FriendManager::LoadMemosUnlocked()
{
    std::ifstream file(MEMOS_FILE);
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields = ReadCsvRow(line);
        if (fields.size() == 3 && !fields[0].empty() &&
            !fields[1].empty() && !fields[2].empty())
        {
            memos_[MakeDirectedKey(fields[0], fields[1])] = fields[2];
        }
    }
}

void FriendManager::SaveProfilesUnlocked()
{
    std::ofstream file(PROFILES_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << "userId,presenceMode\n";
    std::vector<std::string> userIds;
    for (const auto& pair : presenceModes_)
        userIds.push_back(pair.first);
    std::sort(userIds.begin(), userIds.end());

    for (const std::string& userId : userIds)
    {
        file << userId << ','
            << static_cast<int>(presenceModes_[userId]) << '\n';
    }
}

void FriendManager::SaveFriendsUnlocked()
{
    std::ofstream file(FRIENDS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << "firstUserId,secondUserId\n";
    for (const std::string& key : SortedKeys(friendships_))
    {
        std::string first;
        std::string second;
        if (SplitKey(key, first, second))
            file << first << ',' << second << '\n';
    }
}

void FriendManager::SaveRequestsUnlocked()
{
    std::ofstream file(REQUESTS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << "senderUserId,receiverUserId\n";
    for (const std::string& key : SortedKeys(requests_))
    {
        std::string sender;
        std::string receiver;
        if (SplitKey(key, sender, receiver))
            file << sender << ',' << receiver << '\n';
    }
}

void FriendManager::SaveBlocksUnlocked()
{
    std::ofstream file(BLOCKS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << "blockerUserId,blockedUserId\n";
    for (const std::string& key : SortedKeys(blocks_))
    {
        std::string blocker;
        std::string target;
        if (SplitKey(key, blocker, target))
            file << blocker << ',' << target << '\n';
    }
}

void FriendManager::SaveMemosUnlocked()
{
    std::ofstream file(MEMOS_FILE, std::ios::trunc);
    if (!file.is_open())
        return;

    file << "ownerUserId,targetUserId,memo\n";
    std::vector<std::string> keys;
    keys.reserve(memos_.size());
    for (const auto& pair : memos_)
        keys.push_back(pair.first);
    std::sort(keys.begin(), keys.end());

    for (const std::string& key : keys)
    {
        std::string owner;
        std::string target;
        if (SplitKey(key, owner, target))
            file << owner << ',' << target << ',' << memos_[key] << '\n';
    }
}
