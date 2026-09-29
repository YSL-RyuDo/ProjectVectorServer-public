#pragma once
#include "Packet.h"
#include "Session.h"
#include "ShopService.h"
#include "CareerManager.h"

class ShopPacketService
{
    template<class T> static void Append(std::vector<char>& bytes, T value)
    {
        const auto* p = reinterpret_cast<const char*>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    template<class T> static T Read(const char* bytes, size_t offset)
    { T result{}; memcpy(&result, bytes + offset, sizeof(T)); return result; }
    static void Record(std::vector<char>& out, const VectorShop::Record& r)
    { Append<int64_t>(out, r.time); Append<int32_t>(out, r.item); Append<uint8_t>(out, static_cast<uint8_t>(r.duplicate)); Append<int32_t>(out, r.refund); }
public:
    static void Dispatch(Session* session, PacketHeader* header)
    {
        if (header->size != 24 || session->GetUserId().empty()) return;
        static VectorShop::Engine engine;
        const char* bytes = reinterpret_cast<const char*>(header);
        auto request = Read<uint64_t>(bytes, 12);
        int page = (std::max)(0, (std::min)(Read<int32_t>(bytes, 20), 9999));
        auto result = CareerManager::Instance().ExecuteShop(engine, session->GetUserId(), bytes[4], bytes[5], Read<uint16_t>(bytes, 6), Read<int32_t>(bytes, 8), request);
        std::vector<char> out(4);
        Append<uint64_t>(out, request);
        Append<uint8_t>(out, static_cast<uint8_t>(result.status));
        Append<int32_t>(out, result.account.coin0); Append<int32_t>(out, result.account.coin1);
        Append<uint16_t>(out, static_cast<uint16_t>(engine.Items().size()));
        Append<uint16_t>(out, static_cast<uint16_t>(result.account.owned.size()));
        Append<uint16_t>(out, static_cast<uint16_t>(result.rewards.size()));
        const int total = static_cast<int>(result.account.history.size());
        const int pages = (std::max)(1, (total + 9) / 10);
        page = (std::min)(page, pages - 1);
        const int historyCount = (std::max)(0, (std::min)(10, total - page * 10));
        Append<uint16_t>(out, static_cast<uint16_t>(historyCount));
        Append<int32_t>(out, page); Append<int32_t>(out, pages);
        for (const auto& i : engine.Items())
        {
            Append<int32_t>(out, i.id); Append<uint8_t>(out, static_cast<uint8_t>(i.kind)); Append<uint8_t>(out, static_cast<uint8_t>(i.grade));
            Append<int32_t>(out, i.model); Append<int32_t>(out, i.skin); Append<int32_t>(out, i.price);
            const auto start = out.size(); out.resize(start + 64, 0); memcpy(out.data() + start, i.name.data(), i.name.size());
        }
        for (const auto& entry : result.account.owned) Append<int32_t>(out, entry.first);
        for (const auto& r : result.rewards) Record(out, r);
        for (int i = 0; i < historyCount; ++i) Record(out, result.account.history[total - 1 - page * 10 - i]);
        auto* response = reinterpret_cast<PacketHeader*>(out.data());
        response->size = static_cast<uint16_t>(out.size()); response->type = 341;
        session->Send(response);
    }
};
