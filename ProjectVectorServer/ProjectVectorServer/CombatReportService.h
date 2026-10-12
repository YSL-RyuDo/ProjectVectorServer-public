#pragma once
#include "Session.h"
#include "RoomManager.h"
#include <cstring>

namespace CombatReport
{
    inline Entry MakeEntry(const RoomMember& peer, Source source, int32_t sourceId, Direction direction,
        uint32_t hpDamage, uint32_t barrierDamage)
    {
        Entry out{};
        out.opponentId = peer.playerId;
        out.characterId = peer.lockedCharacterId;
        strncpy_s(out.nickname, sizeof(out.nickname), peer.nickname.c_str(), _TRUNCATE);
        out.sourceKind = static_cast<uint8_t>(source);
        out.sourceId = sourceId;
        out.direction = static_cast<uint8_t>(direction);
        out.hpDamage = hpDamage;
        out.barrierDamage = barrierDamage;
        return out;
    }

    // Call only from already validated damage paths. Keep the original Damage implementation,
    // return value, assist records, HP broadcasts and kill handling unchanged.
    inline int ApplyDamage(Session* attacker, Session* target, int amount, Source source,
        int32_t sourceId, Region region, uint32_t fallbackAttackerId = 0)
    {
        const int barrierBefore = std::max(0, target->GetBarrier());
        const int applied = target->Damage(amount);
        const uint32_t roomId = target->GetCurrentRoomId();
        const uint32_t attackerId = attacker ? attacker->GetPlayerId() : fallbackAttackerId;
        if (applied > 0 && roomId && attackerId && attackerId != target->GetPlayerId())
        {
            RoomMember owner{}, victim{};
            const bool ownerPresent = RoomManager::Instance().GetMember(roomId, attackerId, owner);
            // A validated persistent skill can outlive its owner's room membership.
            // Retain damage with an unknown identity rather than silently dropping it.
            if (!ownerPresent)
            {
                owner.playerId = attackerId;
                owner.lockedCharacterId = -1;
                owner.nickname.clear();
            }
            if ((ownerPresent || (source == Source::Skill && fallbackAttackerId != 0)) &&
                RoomManager::Instance().GetMember(roomId, target->GetPlayerId(), victim) &&
                (!ownerPresent || (owner.team != victim.team && owner.team != RoomTeam::OBSERVER)) &&
                victim.team != RoomTeam::OBSERVER)
            {
                const uint32_t barrier = static_cast<uint32_t>(std::min(barrierBefore, applied));
                const uint32_t hp = static_cast<uint32_t>(applied) - barrier;
                if (source == Source::Skill) region = Region::None;
                target->GetCombatReport().Record(roomId,
                    MakeEntry(owner, source, sourceId, Direction::Received, hp, barrier), region);
                if (attacker && attacker->GetCurrentRoomId() == roomId)
                    attacker->GetCombatReport().Record(roomId,
                        MakeEntry(victim, source, sourceId, Direction::Dealt, hp, barrier), region);
            }
        }
        // Freeze after recording the fatal event, before any death notification can be sent.
        if (target->IsDead()) target->GetCombatReport().Freeze(roomId, target->GetLastDamager());
        return applied;
    }

    inline void Dispatch(Session* session, PacketHeader* header)
    {
        if (!session || !header || header->size != sizeof(Request)) return;
        Request request{};
        std::memcpy(&request, header, sizeof(request));
        RoomMember member{};
        const bool eligible = session->IsAuthenticated() && session->IsInGame() && session->IsDead() &&
            session->GetCurrentRoomId() == request.roomId &&
            RoomManager::Instance().GetMember(request.roomId, session->GetPlayerId(), member) &&
            member.team != RoomTeam::OBSERVER;
        auto response = session->GetCombatReport().Page(request, eligible);
        session->Send(&response); // Owner only; never broadcast or push to old clients.
    }
}
