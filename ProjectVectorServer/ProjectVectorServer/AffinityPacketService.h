#pragma once
#include "AffinityPackets.h"
#include "AffinityService.h"
#include "PacketType.h"
#include "Session.h"
#include "UserManager.h"
#include <cstring>
#include <iostream>

class AffinityPacketService
{
    static VectorAffinity::Store& Store() { static VectorAffinity::Store instance; return instance; }
    static bool Legacy(const std::string& user, std::array<int, 4>& legacy)
    {
        PlayerData player{};
        if (user.empty() || !UserManager::Instance().GetPlayerData(user, player)) return false;
        CharacterAffinityData old{};
        if (UserManager::Instance().GetCharacterAffinityData(user, old)) legacy = {old.c0, old.c1, old.c2, old.c3};
        return true;
    }
    static void SendState(Session* session, PacketType type, uint32_t requestId, int character,
        const VectorAffinity::Result& result)
    {
        AffinityStateHeader out{};
        out.header.type = static_cast<uint16_t>(type); out.requestId = requestId; out.result = result.status;
        out.revision = result.account.revision; out.characterId = character;
        const bool validCharacter = character >= 0 && character < 4;
        if (validCharacter)
        {
            const auto& state = result.account.characters[character];
            out.level = state.level; out.points = state.points; out.nextLevelPoints = Store().NextPoints(state);
        }
        std::vector<AffinityDialogueEntry> entries;
        if (validCharacter && result.status != VectorAffinity::Unauthorized)
            for (const auto& [key, choice] : result.account.dialogues)
                if (key.first == character)
                {
                    AffinityDialogueEntry entry{};
                    std::memcpy(entry.dialogueId, key.second.data(), key.second.size());
                    entry.choice = static_cast<uint8_t>(choice); entries.push_back(entry);
                }
        out.dialogueCount = static_cast<uint16_t>(entries.size());
        out.header.size = static_cast<uint16_t>(sizeof(out) + entries.size() * sizeof(AffinityDialogueEntry));
        std::vector<char> bytes(out.header.size);
        std::memcpy(bytes.data(), &out, sizeof(out));
        if (!entries.empty()) std::memcpy(bytes.data() + sizeof(out), entries.data(), entries.size() * sizeof(AffinityDialogueEntry));
        session->Send(bytes.data());
    }
public:
    static void Dispatch(Session* session, PacketHeader* header)
    {
        if (!session || !header) return;
        const auto type = static_cast<PacketType>(header->type);
        std::array<int, 4> legacy{};
        if (type == PacketType::PKT_CS_FAVOR_LEVEL_REQUEST)
        {
            if (header->size != sizeof(CS_FavorLevelRequest)) return;
            SC_FavorLevels out{};
            out.header.type = static_cast<uint16_t>(PacketType::PKT_SC_FAVOR_LEVEL_RESPONSE);
            if (session->IsAuthenticated() && Legacy(session->GetUserId(), legacy))
            {
                const auto result = Store().Get(session->GetUserId(), legacy);
                if (result.status == VectorAffinity::Ok)
                {
                    out.count = 4;
                    for (int i = 0; i < 4; ++i) out.characters[i] = {i, result.account.characters[i].level};
                }
            }
            out.header.size = static_cast<uint16_t>(5 + out.count * sizeof(FavorLevelEntry));
            session->Send(&out);
            return;
        }
        if (type == PacketType::PKT_CS_AFFINITY_STATE)
        {
            if (header->size != sizeof(CS_AffinityStateRequest)) return;
            CS_AffinityStateRequest request{}; std::memcpy(&request, header, sizeof(request));
            VectorAffinity::Result result;
            if (!session->IsAuthenticated() || !Legacy(session->GetUserId(), legacy)) result.status = VectorAffinity::Unauthorized;
            else if (request.characterId < 0 || request.characterId >= 4) result.status = VectorAffinity::Invalid;
            else result = Store().Get(session->GetUserId(), legacy);
            SendState(session, PacketType::PKT_SC_AFFINITY_STATE, request.requestId, request.characterId, result);
            return;
        }
        if (type == PacketType::PKT_CS_AFFINITY_DIALOGUE_COMPLETE)
        {
            if (header->size != sizeof(CS_AffinityDialogueComplete)) return;
            CS_AffinityDialogueComplete request{}; std::memcpy(&request, header, sizeof(request));
            VectorAffinity::Result result;
            if (!session->IsAuthenticated() || !Legacy(session->GetUserId(), legacy)) result.status = VectorAffinity::Unauthorized;
            else if (!std::memchr(request.dialogueId, 0, sizeof(request.dialogueId))) result.status = VectorAffinity::Invalid;
            else result = Store().CompleteDialogue(session->GetUserId(), legacy, request.expectedRevision,
                request.characterId, request.dialogueId, request.choice);
            SendState(session, PacketType::PKT_SC_AFFINITY_DIALOGUE_RESULT, request.requestId, request.characterId, result);
        }
    }
    static void AwardCompletion(const std::string& user, uint64_t matchId, int character,
        bool matchmade, int matchType, int seconds, bool won)
    {
        std::array<int, 4> legacy{};
        if (!Legacy(user, legacy)) return;
        const auto result = Store().AwardMatch(user, legacy, matchId, character, matchmade, matchType, seconds, true, won);
        if (result.status != VectorAffinity::Ok)
            std::cerr << "[AFFINITY SETTLEMENT FAILED] matchId=" << matchId << " status=" << int(result.status) << std::endl;
    }
};
