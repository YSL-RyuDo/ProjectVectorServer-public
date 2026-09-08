#include "FriendPacketService.h"

#include "FriendManager.h"
#include "Packet.h"
#include "PacketType.h"
#include "Session.h"
#include "SessionManager.h"
#include "UserManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <random>
#include <unordered_set>

namespace
{
    std::string ToLower(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return value;
    }

    bool FillFriendUserInfo(
        const std::string& viewerUserId,
                const std::string& userId,
        FriendUserInfo& out)
    {
        PlayerData player{};
        if (!UserManager::Instance().GetPlayerData(userId, player))
            return false;

        out = {};
        strncpy_s(out.userId, sizeof(out.userId), player.id.c_str(), _TRUNCATE);
        strncpy_s(
            out.nickname,
            sizeof(out.nickname),
            player.nickname.c_str(),
            _TRUNCATE);
        out.level = player.level;
        out.playerIconId = player.playerIconId;
        out.rankImageId = player.rankImageId;
        std::string memo = FriendManager::Instance().GetMemo(
            viewerUserId,
            userId);
        strncpy_s(out.memo, sizeof(out.memo), memo.c_str(), _TRUNCATE);

        std::shared_ptr<Session> onlineSession =
            SessionManager::Instance().FindByUserId(userId);
        if (onlineSession == nullptr || onlineSession->IsDisconnected())
        {
            out.presenceStatus = 0;
            out.playerId = 0;
        }
        else
        {
            out.playerId = onlineSession->GetPlayerId();
            out.presenceStatus =
                FriendManager::Instance().GetPresenceMode(userId) ==
                    FriendPresenceMode::DO_NOT_DISTURB
                ? 2
                : 1;
        }

        return true;
    }

    void SendOverview(Session* session)
    {
        SC_FriendOverview out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_OVERVIEW);
        out.presenceMode = static_cast<uint8_t>(
            FriendManager::Instance().GetPresenceMode(session->GetUserId()));

        std::vector<std::string> friends =
            FriendManager::Instance().GetFriends(session->GetUserId());
        for (const std::string& friendUserId : friends)
        {
            if (out.friendCount >= FRIEND_MAX_CONTACT_COUNT)
                break;
            if (FillFriendUserInfo(
                    session->GetUserId(),
                    friendUserId,
                    out.friends[out.friendCount]))
            {
                ++out.friendCount;
            }
        }

        std::vector<std::string> blocked =
            FriendManager::Instance().GetBlocked(session->GetUserId());
        for (const std::string& blockedUserId : blocked)
        {
            if (out.blockedCount >= FRIEND_MAX_CONTACT_COUNT)
                break;
            if (FillFriendUserInfo(
                    session->GetUserId(),
                    blockedUserId,
                    out.blocked[out.blockedCount]))
            {
                ++out.blockedCount;
            }
        }

        session->Send(&out);
    }

    void SendRequestList(Session* session)
    {
        SC_FriendRequestList out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_REQUEST_LIST);

        std::vector<FriendRequestRecord> requests =
            FriendManager::Instance().GetRequests(session->GetUserId());
        for (const FriendRequestRecord& request : requests)
        {
            if (out.count >= FRIEND_MAX_REQUEST_COUNT)
                break;

            bool outgoing = request.senderUserId == session->GetUserId();
            const std::string& peerUserId = outgoing
                ? request.receiverUserId
                : request.senderUserId;

            FriendRequestInfo& info = out.requests[out.count];
            info.direction = outgoing ? 0 : 1;
            if (FillFriendUserInfo(
                    session->GetUserId(), peerUserId, info.user))
                ++out.count;
        }

        session->Send(&out);
    }

    void SendRecent(Session* session)
    {
        SC_FriendRecent out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_RECENT);

        std::vector<std::string> recent = session->GetRecentPlayerUserIds();
        for (const std::string& userId : recent)
        {
            if (out.count >= FRIEND_MAX_RECENT_COUNT)
                break;
            if (FillFriendUserInfo(
                    session->GetUserId(), userId, out.users[out.count]))
                ++out.count;
        }

        session->Send(&out);
    }

    void SendOperationResult(
        Session* session,
        FriendOperation operation,
        FriendOperationResult result,
        const std::string& targetUserId)
    {
        SC_FriendOperationResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_OPERATION_RESULT);
        out.operation = static_cast<uint8_t>(operation);
        out.result = static_cast<uint8_t>(result);
        strncpy_s(
            out.targetUserId,
            sizeof(out.targetUserId),
            targetUserId.c_str(),
            _TRUNCATE);
        session->Send(&out);
    }

    void SendSearch(Session* session, const std::string& query)
    {
        SC_FriendSearch out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_SEARCH);

        std::string loweredQuery = ToLower(query);
        if (!loweredQuery.empty())
        {
            std::vector<PlayerData> players =
                UserManager::Instance().GetAllPlayerData();
            for (const PlayerData& player : players)
            {
                if (out.count >= FRIEND_MAX_SEARCH_COUNT)
                    break;
                if (player.id == session->GetUserId())
                    continue;

                std::string loweredId = ToLower(player.id);
                std::string loweredNickname = ToLower(player.nickname);
                if (loweredId.find(loweredQuery) == std::string::npos &&
                    loweredNickname.find(loweredQuery) == std::string::npos)
                {
                    continue;
                }

                if (!FriendManager::Instance().CanRecommend(
                        session->GetUserId(),
                        player.id))
                {
                    continue;
                }

                if (FillFriendUserInfo(
                        session->GetUserId(),
                        player.id,
                        out.users[out.count]))
                    ++out.count;
            }
        }

        std::cout << "[FRIEND SEARCH] requester="
            << session->GetUserId()
            << " query='" << query
            << "' resultCount=" << static_cast<int>(out.count)
            << " responseType="
            << static_cast<uint16_t>(PacketType::PKT_SC_FRIEND_SEARCH)
            << " responseBytes=" << sizeof(out)
            << std::endl;

        session->Send(&out);
    }

    void SendRecommendations(Session* session)
    {
        struct Candidate
        {
            PlayerData player;
            int score = 0;
        };

        SC_FriendRecommend out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_RECOMMEND);

        PlayerData self{};
        if (!UserManager::Instance().GetPlayerData(session->GetUserId(), self))
        {
            session->Send(&out);
            return;
        }

        std::vector<std::string> recent = session->GetRecentPlayerUserIds();
        std::unordered_set<std::string> recentSet(recent.begin(), recent.end());
        std::random_device randomDevice;
        std::mt19937 randomEngine(randomDevice());
        std::uniform_int_distribution<int> randomOffset(0, 60);

        std::vector<Candidate> candidates;
        for (const PlayerData& player :
            UserManager::Instance().GetAllPlayerData())
        {
            if (!FriendManager::Instance().CanRecommend(
                    self.id,
                    player.id))
            {
                continue;
            }

            Candidate candidate{};
            candidate.player = player;
            candidate.score = std::abs(self.level - player.level) * 20 +
                std::abs(self.rankImageId - player.rankImageId) * 30 +
                randomOffset(randomEngine);
            if (recentSet.count(player.id) != 0)
                candidate.score -= 120;
            candidates.push_back(candidate);
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs)
            {
                return lhs.score < rhs.score;
            });

        for (const Candidate& candidate : candidates)
        {
            if (out.count >= FRIEND_RECOMMEND_COUNT)
                break;
            if (FillFriendUserInfo(
                    session->GetUserId(),
                    candidate.player.id,
                    out.users[out.count]))
            {
                ++out.count;
            }
        }

        std::cout << "[FRIEND RECOMMEND] requester="
            << session->GetUserId()
            << " candidateCount=" << candidates.size()
            << " resultCount=" << static_cast<int>(out.count)
            << " responseType="
            << static_cast<uint16_t>(PacketType::PKT_SC_FRIEND_RECOMMEND)
            << " responseBytes=" << sizeof(out)
            << std::endl;

        session->Send(&out);
    }
}

void FriendPacketService::Dispatch(Session* session, PacketHeader* header)
{
    if (session == nullptr || header == nullptr)
        return;

    switch (static_cast<PacketType>(header->type))
    {
    case PacketType::PKT_CS_FRIEND_OVERVIEW_REQUEST:
        if (header->size == sizeof(CS_FriendOverviewRequest))
            SendOverview(session);
        break;

    case PacketType::PKT_CS_FRIEND_PRESENCE_SET:
    {
        if (header->size != sizeof(CS_FriendPresenceSet))
            break;
        auto* packet = reinterpret_cast<CS_FriendPresenceSet*>(header);
        FriendPresenceMode mode = packet->presenceMode == 1
            ? FriendPresenceMode::DO_NOT_DISTURB
            : FriendPresenceMode::ONLINE;
        bool success = FriendManager::Instance().SetPresenceMode(
            session->GetUserId(),
            mode);

        SC_FriendPresenceSetResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_PRESENCE_SET_RESULT);
        out.result = success ? 0 : 1;
        out.presenceMode = static_cast<uint8_t>(mode);
        session->Send(&out);
        break;
    }

    case PacketType::PKT_CS_FRIEND_SEARCH:
    {
        if (header->size != sizeof(CS_FriendSearch))
            break;
        auto* packet = reinterpret_cast<CS_FriendSearch*>(header);
        size_t length = strnlen_s(packet->query, FRIEND_SEARCH_QUERY_LEN);
        SendSearch(session, std::string(packet->query, length));
        break;
    }

    case PacketType::PKT_CS_FRIEND_RECOMMEND_REQUEST:
        if (header->size == sizeof(CS_FriendRecommendRequest))
            SendRecommendations(session);
        break;

    case PacketType::PKT_CS_FRIEND_OPERATION:
    {
        if (header->size != sizeof(CS_FriendOperation))
            break;

        auto* packet = reinterpret_cast<CS_FriendOperation*>(header);
        size_t length = strnlen_s(packet->targetUserId, FRIEND_USER_ID_LEN);
        std::string targetUserId(packet->targetUserId, length);
        FriendOperation operation = static_cast<FriendOperation>(
            packet->operation);
        FriendOperationResult result = FriendOperationResult::INVALID;

        PlayerData targetData{};
        bool targetExists = UserManager::Instance().GetPlayerData(
            targetUserId,
            targetData);
        if (targetExists)
        {
            switch (operation)
            {
            case FriendOperation::SEND_REQUEST:
                result = FriendManager::Instance().SendRequest(
                    session->GetUserId(), targetUserId);
                break;
            case FriendOperation::CANCEL_REQUEST:
                result = FriendManager::Instance().CancelRequest(
                    session->GetUserId(), targetUserId);
                break;
            case FriendOperation::ACCEPT_REQUEST:
                result = FriendManager::Instance().RespondRequest(
                    session->GetUserId(), targetUserId, true);
                break;
            case FriendOperation::REJECT_REQUEST:
                result = FriendManager::Instance().RespondRequest(
                    session->GetUserId(), targetUserId, false);
                break;
            case FriendOperation::BLOCK:
                result = FriendManager::Instance().SetBlocked(
                    session->GetUserId(), targetUserId, true);
                break;
            case FriendOperation::UNBLOCK:
                result = FriendManager::Instance().SetBlocked(
                    session->GetUserId(), targetUserId, false);
                break;
            case FriendOperation::DELETE_FRIEND:
                result = FriendManager::Instance().RemoveFriend(
                    session->GetUserId(), targetUserId);
                break;
            default:
                break;
            }
        }

        SendOperationResult(session, operation, result, targetUserId);
        break;
    }

    case PacketType::PKT_CS_FRIEND_REQUEST_LIST_REQUEST:
        if (header->size == sizeof(CS_FriendRequestListRequest))
            SendRequestList(session);
        break;

    case PacketType::PKT_CS_FRIEND_RECENT_REQUEST:
        if (header->size == sizeof(CS_FriendRecentRequest))
            SendRecent(session);
        break;

    case PacketType::PKT_CS_FRIEND_MEMO_SET:
    {
        if (header->size != sizeof(CS_FriendMemoSet))
            break;

        auto* packet = reinterpret_cast<CS_FriendMemoSet*>(header);
        size_t targetLength = strnlen_s(
            packet->targetUserId,
            FRIEND_USER_ID_LEN);
        size_t memoLength = strnlen_s(packet->memo, FRIEND_MEMO_LEN);
        std::string targetUserId(packet->targetUserId, targetLength);
        std::string memo(packet->memo, memoLength);

        bool success = FriendManager::Instance().SetMemo(
            session->GetUserId(),
            targetUserId,
            memo);

        SC_FriendMemoSetResult out{};
        out.header.size = sizeof(out);
        out.header.type = static_cast<uint16_t>(
            PacketType::PKT_SC_FRIEND_MEMO_SET_RESULT);
        out.result = success ? 0 : 1;
        strncpy_s(
            out.targetUserId,
            sizeof(out.targetUserId),
            targetUserId.c_str(),
            _TRUNCATE);
        strncpy_s(out.memo, sizeof(out.memo), memo.c_str(), _TRUNCATE);
        session->Send(&out);
        break;
    }

    default:
        break;
    }
}
