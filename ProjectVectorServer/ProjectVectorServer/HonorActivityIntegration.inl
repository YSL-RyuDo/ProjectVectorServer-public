// Included after match, drone, and stun helpers. Never creates threads or sends new packets.
void TickHonorActivity()
{
    std::vector<std::pair<uint32_t, MatchRuntimeState>> matches;
    {
        std::lock_guard<std::mutex> lock(g_matchStateMutex);
        for (const auto& entry : g_matchStates) matches.push_back(entry);
    }
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (const auto& entry : matches)
    {
        Room room{};
        if (!RoomManager::Instance().GetRoom(entry.first, room) || !room.isMatchmade) continue;
        const auto& match = entry.second;
        const bool playing = room.state == RoomState::PLAYING && !room.matchResultReady &&
            !match.buyPhase && !match.roundResolving && !match.matchEnded;
        for (const auto& member : room.members)
        {
            auto peer = SessionManager::Instance().Find(member.playerId);
            if (!peer) continue;
            bool eligible = playing && member.team != RoomTeam::OBSERVER && peer->IsInGame() &&
                !peer->IsDisconnected() && peer->GetCurrentRoomId() == room.roomId &&
                !peer->IsDead() && !IsPlayerStunned(member.playerId);
            const auto position = peer->GetPosition();
            HonorActivity::Pose pose{position.x, position.y, position.z, peer->GetYaw(), peer->GetPitch()};
            // Drone operators remain active through their drone pose, not their stationary body.
            {
                std::lock_guard<std::mutex> lock(g_droneMutex);
                const auto owner = g_ownerDroneIds.find(member.playerId);
                if (owner != g_ownerDroneIds.end())
                {
                    const auto drone = g_drones.find(owner->second);
                    if (drone == g_drones.end() || drone->second.roomId != room.roomId) eligible = false;
                    else
                    {
                        const auto& d = drone->second;
                        pose = {d.x, d.y, d.z, d.yaw, d.pitch};
                    }
                }
            }
            peer->GetHonorActivity().Sample(room.roomId, match.currentRound, now, eligible, pose);
        }
    }
}
