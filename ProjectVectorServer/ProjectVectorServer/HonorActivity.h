#pragma once
#include <cmath>
#include <cstdint>
#include <mutex>
#include <set>

// Server-only evidence. No client packet, score/KDA heuristic, or per-frame disk log.
namespace HonorActivity
{
constexpr double LongIdleSeconds = 60;
constexpr int MinimumObservedSeconds = 180;
constexpr int MinimumIdleSeconds = 120;
constexpr int MinimumIdleRounds = 2;
constexpr int IdlePercent = 60;
constexpr int Penalty = 50;
constexpr double MaximumSampleGap = 3;
struct Evidence
{
    int eligibleSeconds = 0;
    int longIdleSeconds = 0;
    int idleRounds = 0;
    bool Inactive() const
    {
        return eligibleSeconds >= MinimumObservedSeconds && longIdleSeconds >= MinimumIdleSeconds &&
            idleRounds >= MinimumIdleRounds && longIdleSeconds <= eligibleSeconds &&
            int64_t(longIdleSeconds) * 100 >= int64_t(eligibleSeconds) * IdlePercent;
    }
};
struct Pose { double x = 0, y = 0, z = 0, yaw = 0, pitch = 0; };
class Tracker
{
    mutable std::mutex mutex_;
    uint32_t room_ = 0;
    int round_ = 0;
    bool eligible_ = false, action_ = false, sampled_ = false, settled_ = false;
    double last_ = 0, eligibleTime_ = 0, idle_ = 0, longIdle_ = 0;
    Pose anchor_{};
    std::set<int> idleRounds_;
    static bool Finite(const Pose& p)
    { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.yaw) && std::isfinite(p.pitch); }
    bool Moved(const Pose& p)
    {
        const double dx = p.x - anchor_.x, dy = p.y - anchor_.y, dz = p.z - anchor_.z;
        const double distance = dx * dx + dy * dy + dz * dz;
        const double yaw = std::abs(std::remainder(p.yaw - anchor_.yaw, 360.0));
        const double pitch = std::abs(p.pitch - anchor_.pitch);
        // Small network jitter/identical pose packets do not prove activity.
        // Huge discontinuities are rebaselined, not treated as movement evidence.
        if (distance > 900) { anchor_ = p; return false; }
        const bool moved = distance >= .25 || yaw >= 3 || pitch >= 3;
        if (moved) anchor_ = p;
        return moved;
    }
public:
    void Reset(uint32_t room)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        room_ = room; round_ = 0; eligible_ = action_ = sampled_ = settled_ = false;
        last_ = eligibleTime_ = idle_ = longIdle_ = 0; anchor_ = {}; idleRounds_.clear();
    }
    void Action()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (eligible_) action_ = true;
    }
    bool ClaimOutcome(uint32_t room)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!room_ || room_ != room || settled_) return false;
        settled_ = true;
        return true;
    }
    // Used on death so a death/respawn entirely between periodic samples cannot bridge idle time.
    void Pause()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        eligible_ = action_ = false; idle_ = 0;
    }
    void Sample(uint32_t room, int round, double now, bool eligible, const Pose& pose)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!room_ || room != room_ || !std::isfinite(now)) return;
        const double dt = now - last_;
        const bool valid = eligible && Finite(pose);
        if (!sampled_ || round != round_ || !eligible_ || !valid || dt <= 0 || dt > MaximumSampleGap)
        {
            idle_ = 0; anchor_ = pose;
        }
        else
        {
            eligibleTime_ += dt;
            if (Moved(pose) || action_) idle_ = 0;
            else
            {
                const double previous = idle_;
                idle_ += dt;
                if (idle_ >= LongIdleSeconds)
                {
                    longIdle_ += previous < LongIdleSeconds ? idle_ : dt;
                    idleRounds_.insert(round);
                }
            }
        }
        last_ = now; round_ = round; eligible_ = valid; sampled_ = true; action_ = false;
    }
    Evidence Snapshot(uint32_t room) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (room != room_) return {};
        return {static_cast<int>(eligibleTime_), static_cast<int>(longIdle_), static_cast<int>(idleRounds_.size())};
    }
};
}
