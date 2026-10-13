// Included after gameplay helpers: restoration reuses packet formats, not reset
// commands. No rewards, timers, inventories or other players are restarted here.
namespace
{
#pragma pack(push,1)
    struct ReconnectOffer { PacketHeader header{44,390}; uint32_t playerId; uint32_t roomId; char map[32]; };
    struct ReconnectPhase { PacketHeader header{6,392}; uint8_t stage; uint8_t phase; };
    struct ReconnectPose { uint32_t playerId; float x,y,z,yaw,pitch; };
    struct ReconnectPoses { PacketHeader header; uint8_t count; ReconnectPose members[6]; };
    struct ReconnectCommit { PacketHeader header{5,394}; uint8_t phase; };
#pragma pack(pop)

    void OfferReconnect(Session* session)
    {
        Room room{};
        if(!RoomManager::Instance().GetRoom(session->GetCurrentRoomId(),room)) return;
        ReconnectOffer offer{}; offer.playerId=session->GetPlayerId(); offer.roomId=room.roomId;
        strncpy_s(offer.map,room.mapName.c_str(),_TRUNCATE);
        session->SendRestore(&offer);
    }

    bool HandleReconnect(Session* session, PacketHeader* header)
    {
        if(header->type==395 && header->size==4 && session->IsRestoring() && session->RestoreStage()==2)
        {
            std::lock_guard<std::recursive_mutex> transaction(session->RestoreMutex());
            session->FinishRestore();
            PacketHeader done{4,396}; session->SendRestore(&done);
            std::cout << "[RECONNECT COMMITTED] playerId=" << session->GetPlayerId() << std::endl;
            return true;
        }
        if(header->type!=391) return false;
        if(header->size!=5 || !session->IsRestoring()) return true;
        uint8_t stage=*(reinterpret_cast<uint8_t*>(header)+4);
        if(stage!=1 && stage!=2) return true;
        if ((stage==1 && session->RestoreStage()!=0) || (stage==2 && session->RestoreStage()!=1)) return true;
        Room room{};
        if(!RoomManager::Instance().GetRoom(session->GetCurrentRoomId(),room)) return true;
        std::lock_guard<std::recursive_mutex> transaction(session->RestoreMutex());
        MatchRuntimeState state{};
        { std::lock_guard<std::mutex> lock(g_matchStateMutex);
          auto it=g_matchStates.find(room.roomId); if(it!=g_matchStates.end()) state=it->second; }
        uint8_t phase=!session->IsInGame()?0:state.matchEnded?5:state.roundResolving?4:state.spikePlanted?3:state.buyPhase?1:2;
        ReconnectPhase begin{}; begin.stage=stage; begin.phase=phase; session->SendRestore(&begin);
        if(phase==0)
        {
            RoomMember me{};
            if(RoomManager::Instance().GetMember(room.roomId,session->GetPlayerId(),me))
            { SC_PickState pick{}; FillPickStatePacket(pick,room,me,me.playerId); session->SendRestore(&pick); }
            session->SetRestoreStreamReady(true);
            session->SetRestoreStage(2);
            ReconnectCommit commit{}; commit.phase=phase; session->SendRestore(&commit);
            return true;
        }
        if(stage==1)
        {
            session->SetRestoreStage(1);
            SC_GameAllReady ready{};
            ready.header={sizeof(ready),49}; ready.roomId=room.roomId;
            strncpy_s(ready.mapName,room.mapName.c_str(),_TRUNCATE);
            for(const auto& member:room.members)
                if(member.team!=RoomTeam::OBSERVER && ready.memberCount<6)
                    FillGameReadyMemberInfo(ready.members[ready.memberCount++],member);
            session->SendRestore(&ready);
            return true;
        }
        SC_RoundReset snapshot{};
        snapshot.header={sizeof(snapshot),131}; snapshot.currentRound=state.currentRound;
        snapshot.scoreA=state.scoreA; snapshot.scoreB=state.scoreB;
        ReconnectPoses poses{}; poses.header={sizeof(poses),393};
        SC_ScoreboardUpdate board{}; board.header={sizeof(board),137}; board.roomId=room.roomId;
        for(const auto& member:room.members)
        {
            if(member.team==RoomTeam::OBSERVER || snapshot.memberCount>=6) continue;
            auto actor=SessionManager::Instance().Find(member.playerId); if(!actor) continue;
            auto& out=snapshot.members[snapshot.memberCount++];
            out.playerId=member.playerId; out.actualTeam=static_cast<uint8_t>(member.team);
            out.spawnTeam=static_cast<uint8_t>(GetSpawnRoleTeam(member.team,state.currentRound)); out.slotIndex=member.slotIndex;
            out.hp=actor->GetHP(); out.maxHp=actor->GetMaxHP(); out.barrier=actor->GetBarrier(); out.maxBarrier=actor->GetMaxBarrier();
            out.currentWeaponId=actor->GetCurrentWeaponId();
            if(auto weapon=actor->GetWeaponState(out.currentWeaponId)) { out.magAmmo=weapon->magAmmo; out.reserveAmmo=weapon->reserveAmmo; }
            FillRoundResetWeaponSlots(out,actor.get());
            out.purchasedSkillMask=actor->GetPurchasedSkillMask(); out.usedSkillMask=actor->GetUsedSkillMask();
            // A disconnected actor may have missed a round change. In that case
            // keep the map's spawn from SC_RoundReset, never its previous-round position.
            if(actor->HasCurrentRoundPosition()) {
                auto pos=actor->GetPosition(); poses.members[poses.count++]={member.playerId,pos.x,pos.y,pos.z,actor->GetYaw(),actor->GetPitch()};
            }
            auto& score=board.members[board.memberCount++]; score.playerId=member.playerId;
            score.team=out.actualTeam; score.slotIndex=out.slotIndex; score.characterId=actor->GetActiveCharacterId();
            strncpy_s(score.nickname,member.nickname.c_str(),_TRUNCATE);
            auto& stats=actor->GetMatchStats(); score.kills=stats.kills; score.deaths=stats.deaths; score.assists=stats.assists; score.credits=actor->GetCredits();
            if(member.playerId==session->GetPlayerId()) board.myTeam=out.actualTeam;
        }
        session->SendRestore(&snapshot); session->SendRestore(&poses);
        if(phase!=1) { SC_GamePrepareTimer timer{}; timer.header={sizeof(timer),50}; session->SendRestore(&timer); }
        std::vector<char> result;
        for(auto& packet:session->RestoreJournal())
        {
            auto* h=reinterpret_cast<PacketHeader*>(packet.data());
            if(h->type==133) {result=packet;continue;}
            if(h->type==137 || (h->type==130 && phase<4)) continue;
            session->SendRestore(h);
        }
        session->SendRestore(&board);
        session->SetRestoreStreamReady(true);
        session->SetRestoreStage(2);
        ReconnectCommit commit{}; commit.phase=phase; session->SendRestore(&commit);
        if(!result.empty()) session->SendRestore(result.data());
        std::cout << "[RECONNECT SNAPSHOT] playerId=" << session->GetPlayerId() << " phase=" << int(phase)
            << " members=" << int(snapshot.memberCount) << " round=" << int(state.currentRound) << std::endl;
        return true;
    }
}
