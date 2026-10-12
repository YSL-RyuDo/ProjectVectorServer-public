#include "../ProjectVectorServer/ProjectVectorServer/HonorActivity.h"
#include <cassert>
#include <atomic>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

using namespace HonorActivity;
int main()
{
    Tracker idle;
    idle.Reset(1);
    for (int round = 1; round <= 3; ++round)
        for (int second = 0; second <= 100; ++second)
            idle.Sample(1, round, round * 200 + second, true, {});
    auto evidence = idle.Snapshot(1);
    assert(evidence.eligibleSeconds == 300 && evidence.longIdleSeconds == 300 && evidence.idleRounds == 3 && evidence.Inactive());
    assert(!idle.Snapshot(2).Inactive());

    Tracker oneRound; oneRound.Reset(1);
    for (int second = 0; second <= 400; ++second) oneRound.Sample(1, 1, second, true, {});
    assert(!oneRound.Snapshot(1).Inactive()); // One long tactical hold alone cannot penalize.

    Tracker active; active.Reset(1);
    for (int second = 0; second <= 600; ++second)
    {
        if (second % 20 == 0) active.Action(); // Successful fire/skill/objective, even zero kills.
        active.Sample(1, second / 100 + 1, second, true, {});
    }
    assert(active.Snapshot(1).longIdleSeconds == 0 && !active.Snapshot(1).Inactive());

    Tracker aim; aim.Reset(1);
    Tracker move; move.Reset(1);
    Tracker jitter; jitter.Reset(1);
    for (int second = 0; second <= 400; ++second)
    {
        const int round = second / 100 + 1;
        aim.Sample(1, round, second, true, {0, 0, 0, double(second / 10) * 5, 0});
        move.Sample(1, round, second, true, {double(second / 10), 0, 0, 0, 0});
        jitter.Sample(1, round, second, true, {.001 * (second % 2), 0, 0, .001 * (second % 2), 0});
    }
    assert(!aim.Snapshot(1).Inactive() && !move.Snapshot(1).Inactive());
    assert(jitter.Snapshot(1).Inactive()); // Repeated network poses aren't meaningful play.

    Tracker excluded; excluded.Reset(1);
    for (int second = 0; second < 600; ++second) excluded.Sample(1, second / 100 + 1, second, false, {});
    assert(excluded.Snapshot(1).eligibleSeconds == 0 && !excluded.Snapshot(1).Inactive());

    Tracker interruption; interruption.Reset(1);
    for (int second = 0; second <= 400; ++second)
    {
        if (second % 40 == 0) interruption.Pause(); // Death/respawn between samples.
        interruption.Sample(1, second / 100 + 1, second, true, {});
    }
    assert(interruption.Snapshot(1).longIdleSeconds == 0);

    Tracker stall; stall.Reset(1);
    for (int i = 0; i <= 100; ++i) stall.Sample(1, i / 10 + 1, i * 10, true, {});
    assert(stall.Snapshot(1).eligibleSeconds == 0 && !stall.Snapshot(1).Inactive());
    Tracker invalid; invalid.Reset(1);
    for (int second = 0; second <= 400; ++second)
        invalid.Sample(1, second / 100 + 1, second, true, {std::numeric_limits<double>::quiet_NaN(), 0, 0, 0, 0});
    assert(invalid.Snapshot(1).eligibleSeconds == 0);

    // Once the player returns to sustained participation, the ratio gate can clear suspicion.
    for (int second = 0; second <= 400; ++second)
    {
        idle.Action();
        idle.Sample(1, 4 + second / 100, 800 + second, true, {});
    }
    assert(!idle.Snapshot(1).Inactive());
    assert((Evidence{180, 120, 2}.Inactive()));
    assert(!(Evidence{179, 120, 2}.Inactive()));
    assert(!(Evidence{300, 120, 2}.Inactive()));

    Tracker gate; gate.Reset(1); std::atomic<int> claims{0};
    assert(!gate.ClaimOutcome(2));
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] { if (gate.ClaimOutcome(1)) ++claims; });
    for (auto& worker : workers) worker.join();
    assert(claims == 1); // Completion/disconnect racing cannot both settle honor.
    gate.Reset(2); assert(gate.ClaimOutcome(2));
    assert(gate.Snapshot(2).eligibleSeconds == 0 && !gate.Snapshot(2).Inactive());
    std::cout << "PASS: inactivity thresholds, tactical hold, zero-kill actions, movement/aim, jitter, excluded phases, death, stalled sampling, invalid data, recovery, concurrent settlement, reset\n";
}
