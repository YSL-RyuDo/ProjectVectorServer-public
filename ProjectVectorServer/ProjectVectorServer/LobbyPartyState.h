#pragma once
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Transient session IDs only; guarded by the dispatcher's matchmaking mutex.
// Account/profile/wallet persistence remains owned by the existing managers.
class LobbyPartyState
{
public:
    struct Invite { uint32_t token, sender, receiver, leader; int64_t expires; };
    std::vector<uint32_t> Members(uint32_t player) const
    {
        auto found = leaders_.find(player);
        if (found == leaders_.end()) return { player };
        auto party = parties_.find(found->second);
        return party == parties_.end() ? std::vector<uint32_t>{player} : party->second;
    }
    uint32_t Leader(uint32_t player) const { return Members(player).front(); }
    bool IsSolo(uint32_t player) const { return Members(player).size() == 1; }
    bool Join(uint32_t leader, uint32_t player)
    {
        if (!leader || !player || leader == player || Leader(leader) != leader || !IsSolo(player)) return false;
        auto members = Members(leader);
        if (members.size() >= 3) return false;
        members.push_back(player);
        parties_[leader] = members;
        for (auto id : members) leaders_[id] = leader;
        Invalidate(player);
        return true;
    }
    std::vector<uint32_t> Leave(uint32_t player)
    {
        auto members = Members(player);
        parties_.erase(members.front());
        for (auto id : members) leaders_.erase(id);
        members.erase(std::remove(members.begin(), members.end(), player), members.end());
        if (members.size() > 1)
        {
            parties_[members.front()] = members;
            for (auto id : members) leaders_[id] = members.front();
        }
        Invalidate(player);
        return members;
    }
    uint32_t Offer(uint32_t sender, uint32_t receiver, int64_t now)
    {
        Prune(now);
        // One pending invitation per recipient; bounded even under repeated clicks.
        for (const auto& pair : invites_) if (pair.second.receiver == receiver) return 0;
        uint32_t token = ++nextToken_;
        if (!token) token = ++nextToken_;
        invites_[token] = {token, sender, receiver, Leader(sender), now + 30};
        return token;
    }
    bool Consume(uint32_t token, uint32_t receiver, int64_t now, Invite& invite)
    {
        auto it = invites_.find(token);
        if (it == invites_.end() || it->second.receiver != receiver) return false;
        invite = it->second;
        invites_.erase(it);
        return now < invite.expires && Leader(invite.sender) == invite.leader;
    }
    void Invalidate(uint32_t player)
    {
        for (auto it = invites_.begin(); it != invites_.end();)
            if (it->second.sender == player || it->second.receiver == player || it->second.leader == player) it = invites_.erase(it);
            else ++it;
    }
    void Prune(int64_t now)
    {
        for (auto it = invites_.begin(); it != invites_.end();)
            if (it->second.expires <= now) it = invites_.erase(it); else ++it;
    }
    std::vector<uint32_t> PartyPlayers() const
    {
        std::vector<uint32_t> result;
        for (const auto& pair : leaders_) result.push_back(pair.first);
        return result;
    }
private:
    uint32_t nextToken_ = 0;
    std::unordered_map<uint32_t, uint32_t> leaders_;
    std::unordered_map<uint32_t, std::vector<uint32_t>> parties_;
    std::unordered_map<uint32_t, Invite> invites_;
};
