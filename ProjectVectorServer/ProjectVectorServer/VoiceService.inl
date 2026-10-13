// Included in PacketDispatcher's anonymous namespace after LobbySocial.inl.
// All voice policy/epoch/rate state shares the matchmaking mutex.
struct VoiceLease
{
    std::vector<uint32_t> members;
    std::string channel;
    int64_t touched = 0;
};
std::unordered_map<std::string, VoiceLease> g_voiceLeases;
struct VoiceRate { int64_t second = 0; unsigned count = 0; };
std::unordered_map<uint32_t, VoiceRate> g_voiceRates;

void InvalidateVoiceParty(uint32_t player)
{
    for (auto it = g_voiceLeases.begin(); it != g_voiceLeases.end(); )
        if (it->first.starts_with("party:") && std::find(it->second.members.begin(), it->second.members.end(), player) != it->second.members.end())
            it = g_voiceLeases.erase(it);
        else ++it;
}

std::pair<uint8_t, std::string> VoiceContext(Session* session)
{
    std::string key; std::vector<uint32_t> members; uint8_t scope = 0;
    const auto player = session->GetPlayerId();
    Room room{};
    if (session->GetCurrentRoomId() != 0 && (session->IsInRoom() || session->IsInGame()) &&
        RoomManager::Instance().GetRoom(session->GetCurrentRoomId(), room) && room.isMatchmade && !room.matchResultReady)
    {
        auto self = std::find_if(room.members.begin(), room.members.end(), [player](const RoomMember& m) { return m.playerId == player; });
        if (self == room.members.end() || self->team == RoomTeam::OBSERVER) return {};
        // Living/dead teammates share team comms; private rooms and observers have no channel.
        for (const auto& member : room.members) {
            auto peer = SessionManager::Instance().Find(member.playerId);
            if (member.team == self->team && peer && peer->IsAuthenticated() && !peer->IsDisconnected() && peer->GetCurrentRoomId() == room.roomId)
                members.push_back(member.playerId);
        }
        scope = 2; key = "team:" + std::to_string(room.roomId) + ":" + std::to_string(static_cast<int>(self->team));
    }
    else if (session->IsInLobby() && session->GetCurrentRoomId() == 0)
    {
        for (auto member : g_lobbyParties.Members(player))
            if (LobbyAvailable(SessionManager::Instance().Find(member))) members.push_back(member);
        if (members.size() < 2) return {};
        scope = 1; key = "party:" + std::to_string(g_lobbyParties.Leader(player));
    }
    if (scope == 0 || members.empty() || members.size() > 3) return {};
    std::sort(members.begin(), members.end());
    auto& lease = g_voiceLeases[key];
    if (lease.members != members || lease.channel.empty() || SocialNow() - lease.touched > 10) {
        lease.members = members; lease.channel = "vector_" + VectorVoice::Nonce();
    }
    lease.touched = SocialNow();
    return {scope, lease.channel};
}

void HandleVoiceRequest(Session* session, PacketHeader* header)
{
    if (header->size != sizeof(CS_VoiceRequest) || session->IsDisconnected()) return;
    std::lock_guard<std::recursive_mutex> lock(g_matchmakingMutex);
    const auto& request = *reinterpret_cast<const CS_VoiceRequest*>(header);
    SC_VoiceResponse response{};
    response.header = {sizeof(response), static_cast<uint16_t>(PacketType::PKT_SC_VOICE_RESPONSE)};
    response.requestId = request.requestId;
    try {
        const auto now = SocialNow();
        for (auto it = g_voiceRates.begin(); it != g_voiceRates.end(); )
            if (now - it->second.second > 60) it = g_voiceRates.erase(it); else ++it;
        for (auto it = g_voiceLeases.begin(); it != g_voiceLeases.end(); )
            if (now - it->second.touched > 60) it = g_voiceLeases.erase(it); else ++it;
        auto& rate = g_voiceRates[session->GetPlayerId()];
        if (rate.second != now) { rate.second = now; rate.count = 0; }
        if (++rate.count > 8) { response.status = 4; session->Send(&response); return; }
        const auto& config = VectorVoice::Config();
        if (!config.Ready()) { response.status = 1; session->Send(&response); return; }
        if (request.action > 2 || !memchr(request.channel, '\0', sizeof(request.channel))) {
            response.status = 2; session->Send(&response); return;
        }
        const auto [scope, channel] = VoiceContext(session);
        response.scope = scope;
        const auto account = VectorVoice::Account(config, session->GetUserId());
        strncpy_s(response.server, config.server.c_str(), _TRUNCATE);
        strncpy_s(response.domain, config.domain.c_str(), _TRUNCATE);
        strncpy_s(response.issuer, config.issuer.c_str(), _TRUNCATE);
        strncpy_s(response.account, account.c_str(), _TRUNCATE);
        strncpy_s(response.channel, channel.c_str(), _TRUNCATE);
        PlayerData data{};
        if (UserManager::Instance().GetPlayerData(session->GetUserId(), data)) strncpy_s(response.nickname, data.nickname.c_str(), _TRUNCATE);
        if (request.action == 2 && (channel.empty() || channel != request.channel)) response.status = 2;
        else if (request.action != 0) {
            const auto token = VectorVoice::Token(config, account, request.action == 2 ? channel : "");
            if (token.size() >= sizeof(response.token)) throw std::runtime_error("Voice token size");
            strncpy_s(response.token, token.c_str(), _TRUNCATE);
        }
    } catch (...) { response.status = 3; memset(response.token, 0, sizeof(response.token)); }
    session->Send(&response);
}
