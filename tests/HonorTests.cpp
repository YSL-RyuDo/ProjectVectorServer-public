#include "../ProjectVectorServer/ProjectVectorServer/CareerManager.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    const auto fixture = std::filesystem::temp_directory_path() /
        ("vector-honor-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(fixture);
    std::filesystem::current_path(fixture);
    try
    {
        std::ofstream("users.csv") << "userId,nickname\nhonor-test,Honor Test\n";
        auto& manager = CareerManager::Instance();
        manager.Load();
        CareerHonorState state{};
        Check(manager.GetHonorState("honor-test", state) && state.score == 601 &&
            CareerManager::HonorGradeForScore(state.score) == 3,
            "New accounts must start at Normal grade");
        Check(!manager.ClaimHonorRewards("honor-test", 5).succeeded,
            "Normal grade must not claim an Excellent reward");
        Check(CareerManager::HonorGradeForScore(0) == 1 &&
            CareerManager::HonorGradeForScore(301) == 2 &&
            CareerManager::HonorGradeForScore(901) == 4 &&
            CareerManager::HonorGradeForScore(1201) == 5 &&
            CareerManager::HonorGradeForScore(1601) == 6,
            "Honor grade boundaries are wrong");

        CareerMatchPlayer player{};
        player.userId = "honor-test";
        player.team = 0;
        manager.RecordMatch("Private", 1, 700, 5, 1, 0, { player }, 0, false, nullptr);
        manager.GetHonorState("honor-test", state);
        Check(state.score == 601 && state.history.empty(), "Private matches must not farm honor");
        for (int index = 0; index < 4; ++index)
            Check(manager.RecordMatch("Test", 1, 700, 5, 1, 0,
                { player }, 0, true, nullptr) != 0, "Match failed");
        Check(manager.GetHonorState("honor-test", state) && state.score == 661 &&
            state.history.size() == 3, "Daily clean-play gain must cap at 60");
        Check(manager.ApplyHonorViolation("honor-test", HonorChangeReason::MatchAbandoned),
            "Abandon penalty failed");
        Check(manager.GetHonorState("honor-test", state) && state.score == 561 &&
            state.history.front().delta == -100, "Penalty must be much larger than gain");
        manager.Load();
        Check(manager.GetHonorState("honor-test", state) && state.score == 561 &&
            state.history.size() == 4, "Honor state/history did not persist");

        std::ofstream("career_user_honor.csv", std::ios::trunc) <<
            "userId,score,dailyEarned,earnedDayUtc\nhonor-test,1601,0,0\n";
        manager.Load();
        auto claim = manager.ClaimHonorRewards("honor-test", 6);
        Check(claim.succeeded && claim.freeCurrencyAwarded == 2500 &&
            claim.victoryCurrencyAwarded == 5 && claim.claimedGrade == 6,
            "Grade 6 must include unclaimed grade 5 rewards");
        Check(!manager.ClaimHonorRewards("honor-test", 6).succeeded,
            "Honor reward duplicated");
        manager.Load();
        CareerWallet wallet{};
        Check(manager.GetWallet("honor-test", wallet) && wallet.claimedHonorGrade == 6 &&
            wallet.freeCurrency == 2500 && wallet.victoryCurrency == 5,
            "Honor claim did not survive reload");
        for (int i = 0; i < 55; ++i)
            Check(manager.ApplyHonorViolation("honor-test", HonorChangeReason::ConfirmationDeclined),
                "History fixture failed");
        manager.Load();
        Check(manager.GetHonorState("honor-test", state) && state.history.size() == 50 && state.score == 0,
            "History must retain exactly five pages and score must not go negative");
        std::filesystem::create_directory("career_user_honor.csv.tmp");
        auto previous = state;
        Check(!manager.ApplyHonorViolation("honor-test", HonorChangeReason::ChatViolation),
            "Failed save must not be accepted");
        manager.GetHonorState("honor-test", state);
        Check(state.history.front().reason == previous.history.front().reason && state.score == previous.score,
            "Failed save must roll back memory");
        std::cout << "PASS: initial Normal grade, boundaries, daily cap, heavy penalty, history, cumulative one-time rewards\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << "\nFixture: " << fixture << '\n';
        return 1;
    }
}
