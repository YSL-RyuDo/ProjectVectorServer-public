// Included inside PacketDispatcher's anonymous namespace. All state mutations
// share g_matchmakingMutex with queue selection and room-entry dispatch.
int64_t SocialNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SocialResult(Session* session, uint8_t result, uint32_t target = 0)
{
    if (!session || session->IsDisconnected()) return;
    SC_SocialResult out{};
    out.header = {sizeof(out), static_cast<uint16_t>(PacketType::PKT_SC_SOCIAL_RESULT)};
    out.result = result; out.targetPlayerId = target;
    session->Send(&out);
}

bool LobbyAvailable(const std::shared_ptr<Session>& session)
{
    return session && !session->IsDisconnected() && session->IsAuthenticated() &&
        session->IsInLobby() && session->GetCurrentRoomId() == 0;
}

void SendPartyAvailability(Session* session)
{
    if (!session || !session->IsInLobby() || session->IsDisconnected()) return;
    const auto members = g_lobbyParties.Members(session->GetPlayerId());
    SC_PartyAvailability out{};
    out.header = {sizeof(out), static_cast<uint16_t>(PacketType::PKT_SC_PARTY_AVAILABILITY)};
    out.count = static_cast<uint8_t>(members.size());
    for (size_t i = 0; i < members.size(); ++i)
        if (LobbyAvailable(SessionManager::Instance().Find(members[i]))) out.lobbyMask |= 1u << i;
    session->Send(&out);
}

void SendParty(Session* session, bool sendCards = true)
{
    if (!session || session->IsDisconnected()) return;
    const auto members = g_lobbyParties.Members(session->GetPlayerId());
    SC_PartyState state{};
    state.header = {sizeof(state), static_cast<uint16_t>(PacketType::PKT_SC_PARTY_STATE)};
    state.leader = members.front(); state.count = static_cast<uint8_t>(members.size());
    for (size_t i = 0; i < members.size(); ++i) state.players[i] = members[i];
    session->Send(&state);
    SendPartyAvailability(session);
    if (!sendCards || !session->IsInLobby()) return;
    SC_TempGameRoomEnter out{};
    out.header = {sizeof(out), static_cast<uint16_t>(PacketType::PKT_SC_TEMP_GAME_ROOM_ENTER)};
    out.playerCount = state.count;
    for (size_t i = 0; i < members.size(); ++i)
    {
        auto peer = SessionManager::Instance().Find(members[i]);
        PlayerData data{};
        if (!peer || !UserManager::Instance().GetPlayerData(peer->GetUserId(), data)) continue;
        auto& slot = out.players[i]; slot.slotIndex = static_cast<uint8_t>(i);
        strncpy_s(slot.nickname, data.nickname.c_str(), _TRUNCATE);
        slot.playerIconId = data.playerIconId; slot.logoBorderId = data.logoBorderId; slot.rankImageId = data.rankImageId;
        ChipCraftRecord craft{};
        if (!ProfileManager::Instance().GetChipCraft(peer->GetUserId(), craft)) continue;
        slot.preset.cardId = craft.preset.cardId; slot.preset.caseId = craft.preset.caseId;
        slot.preset.medalCount = craft.preset.medalCount;
        for (size_t j = 0; j < craft.preset.medals.size(); ++j)
        {
            const auto& from = craft.preset.medals[j]; auto& to = slot.preset.medals[j];
            to.medalId = from.id; to.x = from.x; to.y = from.y;
            to.rotation = from.rotation; to.scale = from.scale; to.flipped = from.flipped ? 1 : 0;
        }
    }
    session->Send(&out);
}

void BroadcastParty(uint32_t player)
{
    for (auto id : g_lobbyParties.Members(player)) SendParty(SessionManager::Instance().Find(id).get());
}

void SendReady(const ReadyMatch& match, uint8_t phase)
{
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(match.deadline - std::chrono::steady_clock::now()).count();
    for (const auto& entry : match.group)
    {
        auto session = SessionManager::Instance().Find(entry.playerId);
        if (!session || session->IsDisconnected()) continue;
        SC_MatchReady out{};
        out.header = {sizeof(out), static_cast<uint16_t>(PacketType::PKT_SC_MATCH_READY)};
        out.token = match.token; out.phase = phase;
        out.seconds = static_cast<uint16_t>(std::max<int64_t>(0, (left + 999) / 1000));
        out.total = static_cast<uint8_t>(match.group.size());
        out.accepted = static_cast<uint8_t>(match.accepted.size());
        out.selfAccepted = match.accepted.count(entry.playerId) ? 1 : 0;
        session->Send(&out);
    }
}

bool BeginReadyMatch(const std::vector<MatchmakingEntry>& group)
{
    std::lock_guard<std::recursive_mutex> lock(g_matchmakingMutex);
    for (const auto& entry : group)
        if (!LobbyAvailable(SessionManager::Instance().Find(entry.playerId))) return false;
    ReadyMatch match{};
    match.token = ++g_nextReadyToken; match.group = group;
    match.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    g_readyMatches.emplace(match.token, match);
    SendReady(match, 0);
    return true;
}

void CancelReady(const ReadyMatch& match)
{
    SendReady(match, 2);
    for (const auto& entry : match.group)
        SendMatchmakingState(SessionManager::Instance().Find(entry.playerId).get(), 0, 2, entry.mode, 0, GetInitialMatchmakingRange(entry.mode), 0);
}

void TickSocialLobby()
{
    std::lock_guard<std::recursive_mutex> lock(g_matchmakingMutex);
    g_lobbyParties.Prune(SocialNow());
    for (auto it = g_readyMatches.begin(); it != g_readyMatches.end();)
    {
        bool valid = std::chrono::steady_clock::now() < it->second.deadline;
        for (const auto& entry : it->second.group) valid &= LobbyAvailable(SessionManager::Instance().Find(entry.playerId));
        if (!valid) { CancelReady(it->second); it = g_readyMatches.erase(it); }
        else { SendReady(it->second, 0); ++it; }
    }
    // Cancel the entire queued party if any member disconnects or leaves the lobby.
    std::unordered_set<uint64_t> invalidParties;
    for (const auto& entry : g_matchmakingQueue)
        if (!LobbyAvailable(SessionManager::Instance().Find(entry.playerId))) invalidParties.insert(entry.partyId);
    g_matchmakingQueue.erase(std::remove_if(g_matchmakingQueue.begin(), g_matchmakingQueue.end(), [&](const MatchmakingEntry& entry)
    {
        if (!invalidParties.count(entry.partyId)) return false;
        SendMatchmakingState(SessionManager::Instance().Find(entry.playerId).get(), 0, 2, entry.mode, 0, GetInitialMatchmakingRange(entry.mode), 0);
        return true;
    }), g_matchmakingQueue.end());
    for (auto id : g_lobbyParties.PartyPlayers())
    {
        auto session = SessionManager::Instance().Find(id);
        if (!session || session->IsDisconnected())
        {
            const auto remaining = g_lobbyParties.Leave(id);
            if (!remaining.empty()) BroadcastParty(remaining.front());
        }
        else SendPartyAvailability(session.get());
    }
}

void HandleSocialPacket(Session* session, PacketHeader* header)
{
    std::lock_guard<std::recursive_mutex> lock(g_matchmakingMutex);
    const auto id = session->GetPlayerId();
    const auto type = static_cast<PacketType>(header->type);
    if (type == PacketType::PKT_CS_MATCH_READY)
    {
        if (header->size != sizeof(CS_MatchReady)) return;
        const auto& request = *reinterpret_cast<CS_MatchReady*>(header);
        auto it = g_readyMatches.find(request.token);
        if (it == g_readyMatches.end() || request.accept > 1) { SocialResult(session, 7); return; }
        bool belongs = false;
        for (const auto& entry : it->second.group) belongs |= entry.playerId == id;
        if (!belongs) { SocialResult(session, 3); return; }
        if (!request.accept || std::chrono::steady_clock::now() >= it->second.deadline)
        { CancelReady(it->second); g_readyMatches.erase(it); return; }
        it->second.accepted.insert(id);
        if (it->second.accepted.size() != it->second.group.size()) { SendReady(it->second, 0); return; }
        const auto match = it->second;
        // Hold the same lock through room creation: a confirmed member cannot
        // enter another room or queue between validation and the transition.
        bool valid = true;
        for (const auto& entry : match.group) valid &= LobbyAvailable(SessionManager::Instance().Find(entry.playerId));
        if (valid)
        {
            SendReady(match, 1);
            if (!CreateMatchmakingRoom(match.group)) CancelReady(match);
        }
        else CancelReady(match);
        g_readyMatches.erase(it);
        return;
    }
    if (!session->IsInLobby() || session->GetCurrentRoomId() != 0) { SocialResult(session, 4); return; }
    if (type == PacketType::PKT_CS_PARTY_STATE)
    { if (header->size == sizeof(PacketHeader)) SendParty(session, false); return; }
    if (type == PacketType::PKT_CS_PARTY_LEAVE)
    {
        if (header->size != sizeof(PacketHeader)) return;
        if (SocialBusy(id)) { SocialResult(session, 4); return; }
        const auto remaining = g_lobbyParties.Leave(id);
        SendParty(session);
        if (!remaining.empty()) BroadcastParty(remaining.front());
        return;
    }
    if (type == PacketType::PKT_CS_PARTY_INVITE)
    {
        if (header->size != sizeof(CS_PartyInvite)) return;
        const auto targetId = reinterpret_cast<CS_PartyInvite*>(header)->targetPlayerId;
        auto target = SessionManager::Instance().Find(targetId);
        if (!targetId || targetId == id) { SocialResult(session, 3); return; }
        if (g_lobbyParties.Leader(id) != id) { SocialResult(session, 9); return; }
        if (g_lobbyParties.Members(id).size() >= 3) { SocialResult(session, 8); return; }
        if (!LobbyAvailable(target)) { SocialResult(session, 6); return; }
        if (SocialBusy(id) || SocialBusy(targetId) || !g_lobbyParties.IsSolo(targetId)) { SocialResult(session, 4); return; }
        auto& friends = FriendManager::Instance();
        if (!friends.AreFriends(session->GetUserId(), target->GetUserId()) || friends.IsBlockedEitherWay(session->GetUserId(), target->GetUserId()))
        { SocialResult(session, 5); return; }
        if (friends.GetPresenceMode(target->GetUserId()) == FriendPresenceMode::DO_NOT_DISTURB) { SocialResult(session, 6); return; }
        const auto token = g_lobbyParties.Offer(id, targetId, SocialNow());
        if (!token) { SocialResult(session, 4); return; }
        PlayerData player{}; UserManager::Instance().GetPlayerData(session->GetUserId(), player);
        SC_PartyInvite out{}; out.header = {sizeof(out), static_cast<uint16_t>(PacketType::PKT_SC_PARTY_INVITE)};
        out.token = token; out.senderPlayerId = id; out.seconds = 30;
        strncpy_s(out.nickname, player.nickname.c_str(), _TRUNCATE);
        target->Send(&out); SocialResult(session, 0, targetId);
        return;
    }
    if (type == PacketType::PKT_CS_PARTY_INVITE_REPLY)
    {
        if (header->size != sizeof(CS_PartyInviteReply)) return;
        const auto& request = *reinterpret_cast<CS_PartyInviteReply*>(header);
        if (request.accept > 1) { SocialResult(session, 3); return; }
        LobbyPartyState::Invite invite{};
        if (!g_lobbyParties.Consume(request.token, id, SocialNow(), invite)) { SocialResult(session, 7); return; }
        auto sender = SessionManager::Instance().Find(invite.sender);
        if (!request.accept) { SocialResult(sender.get(), 2, id); return; }
        if (!LobbyAvailable(sender) || SocialBusy(id) || SocialBusy(invite.sender)) { SocialResult(session, 4); return; }
        auto& friends = FriendManager::Instance();
        if (!friends.AreFriends(session->GetUserId(), sender->GetUserId()) || friends.IsBlockedEitherWay(session->GetUserId(), sender->GetUserId()))
        { SocialResult(session, 5); return; }
        if (!g_lobbyParties.Join(invite.leader, id)) { SocialResult(session, 8); return; }
        BroadcastParty(id); SocialResult(sender.get(), 1, id); SocialResult(session, 1, invite.sender);
    }
}

void QueueSocialParty(Session* session, uint8_t mode)
{
    std::lock_guard<std::recursive_mutex> lock(g_matchmakingMutex);
    const auto id = session->GetPlayerId();
    if (g_lobbyParties.Leader(id) != id) { SocialResult(session, 9); return; }
    const auto members = g_lobbyParties.Members(id);
    std::vector<MatchmakingEntry> entries;
    for (auto player : members)
    {
        auto peer = SessionManager::Instance().Find(player);
        CareerOverview overview{}; CareerMatchmakingRating rating{};
        if (!LobbyAvailable(peer) || SocialBusy(player) ||
            !CareerManager::Instance().GetOverview(peer->GetUserId(), overview) ||
            !CareerManager::Instance().GetMatchmakingRating(peer->GetUserId(), mode, rating))
        { SocialResult(session, 4); return; }
        MatchmakingEntry entry{};
        entry.playerId = player; entry.mode = mode; entry.matchmakingRating = rating.matchmakingRating;
        entry.visibleTierId = overview.tier.tierId; entry.partyId = id;
        entry.partySize = static_cast<uint8_t>(members.size()); entry.queuedAt = std::chrono::steady_clock::now();
        entries.push_back(entry);
    }
    for (const auto& entry : entries)
    {
        g_lobbyParties.Invalidate(entry.playerId);
        g_matchmakingQueue.push_back(entry);
        SendMatchmakingState(SessionManager::Instance().Find(entry.playerId).get(), 0, 0, mode, 0, GetInitialMatchmakingRange(mode), 0);
    }
}
