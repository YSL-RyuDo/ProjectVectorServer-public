#pragma once
#include "CombatReportPackets.h"
#include <array>
#include <algorithm>
#include <limits>
#include <mutex>

namespace CombatReport
{
    // One bounded, in-memory report per session. No account files, match stats or rewards.
    class Tracker
    {
    public:
        void Reset(uint32_t roomId)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            roomId_ = roomId;
            if (++lifeId_ == 0) ++lifeId_;
            count_ = 0;
            killerId_ = 0;
            frozen_ = truncated_ = false;
            entries_ = {};
        }

        void Record(uint32_t roomId, const Entry& event, Region region)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!roomId || roomId != roomId_ || frozen_ || !event.opponentId ||
                (!event.hpDamage && !event.barrierDamage) || static_cast<uint8_t>(region) > 3)
                return;
            size_t index = 0;
            for (; index < count_; ++index)
            {
                const auto& row = entries_[index];
                if (row.opponentId == event.opponentId && row.sourceKind == event.sourceKind &&
                    row.sourceId == event.sourceId && row.direction == event.direction) break;
            }
            if (index == count_)
            {
                if (count_ == MaxEntries) { truncated_ = true; return; }
                entries_[count_++] = event;
                auto& row = entries_[index];
                row.hpDamage = row.barrierDamage = 0;
                std::fill(std::begin(row.hits), std::end(row.hits), 0u);
            }
            auto& row = entries_[index];
            Add(row.hpDamage, event.hpDamage);
            Add(row.barrierDamage, event.barrierDamage);
            Add(row.hits[static_cast<uint8_t>(region)], 1);
        }

        void Freeze(uint32_t roomId, uint32_t killerId)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (roomId && roomId == roomId_ && !frozen_)
            {
                frozen_ = true;
                killerId_ = killerId;
            }
        }

        Response Page(const Request& request, bool eligible) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Response out{};
            out.header = { sizeof(Response), ResponseType };
            out.requestId = request.requestId;
            out.version = 1;
            out.pageIndex = request.pageIndex;
            if (!eligible || !frozen_ || !roomId_)
            {
                out.status = static_cast<uint8_t>(Status::Unavailable);
                return out;
            }
            if (request.roomId != roomId_ || (request.lifeId && request.lifeId != lifeId_))
            {
                out.status = static_cast<uint8_t>(Status::Stale);
                return out;
            }
            const auto pages = static_cast<uint16_t>(std::max<size_t>(1, (count_ + PageSize - 1) / PageSize));
            if (request.pageIndex >= pages || (!request.lifeId && request.pageIndex != 0))
            {
                out.status = static_cast<uint8_t>(Status::InvalidPage);
                return out;
            }
            out.flags = static_cast<uint8_t>(1 | (truncated_ ? 2 : 0));
            out.roomId = roomId_;
            out.lifeId = lifeId_;
            out.killerId = killerId_;
            out.pageCount = pages;
            out.totalEntries = static_cast<uint16_t>(count_);
            const size_t start = request.pageIndex * PageSize;
            out.entryCount = static_cast<uint8_t>(std::min(PageSize, count_ - start));
            for (size_t i = 0; i < out.entryCount; ++i) out.entries[i] = entries_[start + i];
            return out;
        }

    private:
        void Add(uint32_t& value, uint32_t amount)
        {
            const auto max = (std::numeric_limits<uint32_t>::max)();
            if (amount > max - value) { value = max; truncated_ = true; }
            else value += amount;
        }
        mutable std::mutex mutex_;
        std::array<Entry, MaxEntries> entries_{};
        size_t count_ = 0;
        uint32_t roomId_ = 0, lifeId_ = 0, killerId_ = 0;
        bool frozen_ = false, truncated_ = false;
    };
}
