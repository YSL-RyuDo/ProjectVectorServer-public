#define NOMINMAX
#include <Windows.h>
#include "../ProjectVectorServer/ProjectVectorServer/CareerManager.h"
#include <cassert>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    assert(!std::filesystem::exists(root));
    std::filesystem::create_directories(root);
    std::filesystem::current_path(root);
    std::ofstream("users.csv") << "userId,nickname\nhonor-test,Test\nother,Other\nlegacy,Legacy\n";
    std::ofstream("career_user_honor.csv") << "userId,score,dailyEarned,earnedDayUtc,history\nlegacy,601,0,0,123:0:1:42;\n";
    std::ofstream("career_seasons.csv") << "seasonId,seasonName,startUnix,endUnix,isActive\n1,Test,0,4102444800,1\n";
    auto& career = CareerManager::Instance();
    career.Load();
    CareerHonorState legacy;
    assert(career.GetHonorState("legacy", legacy) && legacy.score == 601 && legacy.history.size() == 1);
    assert(legacy.history.front().matchId == 42 && legacy.history.front().activity.eligibleSeconds == 0);
    const auto state = [&]() { CareerHonorState s; assert(career.GetHonorState("honor-test", s)); return s; };
    CareerMatchPlayer player{}; player.userId = "honor-test"; player.team = 0; player.characterId = 0;
    const auto complete = [&](int duration, bool matched = true) {
        return career.RecordMatch("HonorTest", std::time(nullptr), duration, 5, 2, 0, {player}, 0, matched);
    };
    assert(state().score == 601 && state().history.empty());
    complete(600, false); complete(179);
    assert(state().score == 601 && state().history.empty());
    const auto match = complete(180);
    assert(match && state().score == 609 && state().history.front().delta == 8 && state().history.front().matchId == match);
    complete(600); complete(600); complete(600);
    assert(state().score == 661 && state().dailyEarned == 60 && state().history.front().delta == 12);
    const auto cappedMatch = complete(600);
    assert(state().score == 661 && state().history.size() == 5);
    assert(state().history.front().delta == 0 && state().history.front().matchId == cappedMatch);
    assert(career.ApplyHonorViolation("honor-test", HonorChangeReason::ConfirmationDeclined));
    assert(state().score == 611 && state().history.front().delta == -50);
    assert(career.ApplyHonorViolation("honor-test", HonorChangeReason::ConfirmationTimedOut));
    assert(state().score == 561 && state().history.front().reason == HonorChangeReason::ConfirmationTimedOut);
    career.Load();
    assert(state().score == 561 && state().history.size() == 7 && state().history.front().reason == HonorChangeReason::ConfirmationTimedOut);
    assert(career.ApplyHonorViolation("honor-test", HonorChangeReason::MatchAbandoned));
    assert(state().score == 461);
    player.honorActivity = {200, 180, 2};
    const auto inactiveMatch = complete(600);
    assert(state().score == 411 && state().history.front().delta == -50 && state().dailyEarned == 60);
    assert(state().history.front().reason == HonorChangeReason::ProlongedInactivity);
    assert(state().history.front().matchId == inactiveMatch);
    career.Load();
    assert(state().score == 411 && state().history.front().activity.eligibleSeconds == 200);
    assert(state().history.front().activity.longIdleSeconds == 180 && state().history.front().activity.idleRounds == 2);
    const auto settledHistory = state().history.size();
    player.honorEligible = false;
    complete(600); // An already-settled abandonment cannot also get an AFK outcome.
    assert(state().history.size() == settledHistory && state().score == 411);
    CareerHonorState other; assert(career.GetHonorState("other", other) && other.score == 601 && other.history.empty());
    const auto before = state();
    std::filesystem::create_directory("career_user_honor.csv.tmp");
    assert(!career.ApplyHonorViolation("honor-test", HonorChangeReason::ConfirmationTimedOut));
    assert(state().score == before.score && state().history.size() == before.history.size());
    std::filesystem::remove("career_user_honor.csv.tmp"); // Only our newly created empty failure fixture.
    for (int i = 0; i < 55; ++i) assert(career.ApplyHonorViolation("honor-test", HonorChangeReason::ConfirmationTimedOut));
    assert(state().score == 0 && state().history.size() == 50 && state().history.front().delta == 0);
    career.Load();
    assert(state().score == 0 && state().history.size() == 50 && state().history.front().reason == HonorChangeReason::ConfirmationTimedOut);
    assert(career.GetHonorState("legacy", legacy) && legacy.history.size() == 1 && legacy.history.front().matchId == 42);
    std::cout << "PASS: completion, exclusions, daily cap, legacy history, decline/timeout, AFK penalty/evidence persistence, settlement exclusion, restart, isolation, rollback, bounded history\n";
}
