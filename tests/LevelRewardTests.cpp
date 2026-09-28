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
    // All database writes go to a new disposable fixture, never the server's live CSVs.
    const auto fixture = std::filesystem::temp_directory_path() /
        ("vector-level-rewards-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(fixture);
    std::filesystem::current_path(fixture);
    try
    {
        std::ofstream("users.csv") << "userId,nickname\nreward-test,Reward Test\n";
        std::ofstream("career_user_wallet.csv") <<
            "userId,freeCurrency,victoryCurrency\nreward-test,17,3\n";
        auto& manager = CareerManager::Instance();
        manager.Load();
        CareerWallet wallet{};
        Check(manager.GetWallet("reward-test", wallet) && wallet.freeCurrency == 17 &&
            wallet.victoryCurrency == 3 && wallet.claimedLevelReward == 0, "Legacy wallet migration failed");

        auto claim = manager.ClaimLevelRewards("reward-test", 40, 40);
        Check(claim.succeeded && claim.claimedLevel == 40 && claim.freeCurrencyAwarded == 12000 &&
            claim.victoryCurrencyAwarded == 8, "Level 40 must claim every unclaimed level through 40");
        Check(!manager.ClaimLevelRewards("reward-test", 40, 40).succeeded, "Duplicate claim awarded currency");
        Check(!manager.ClaimLevelRewards("reward-test", 40, 39).succeeded, "Earlier claim awarded currency");
        Check(!manager.ClaimLevelRewards("reward-test", 40, 41).succeeded, "Unreached level awarded currency");
        manager.Load();
        Check(manager.GetWallet("reward-test", wallet) && wallet.claimedLevelReward == 40 &&
            wallet.freeCurrency == 12017 && wallet.victoryCurrency == 11, "Claim and wallet did not survive reload");

        claim = manager.ClaimLevelRewards("reward-test", 45, 45);
        Check(claim.succeeded && claim.freeCurrencyAwarded == 1500 && claim.victoryCurrencyAwarded == 1,
            "Level 45 milestone incorrect");
        claim = manager.ClaimLevelRewards("reward-test", 46, 46);
        Check(claim.succeeded && claim.freeCurrencyAwarded == 300 && claim.victoryCurrencyAwarded == 0,
            "Level 46 bonus incorrect");
        claim = manager.ClaimLevelRewards("reward-test", 50, 50);
        Check(claim.succeeded && claim.freeCurrencyAwarded == 1200 && claim.victoryCurrencyAwarded == 1,
            "Level 50 bonus milestone incorrect");

        std::filesystem::create_directory("career_user_wallet.csv.tmp");
        Check(!manager.ClaimLevelRewards("reward-test", 51, 51).succeeded, "Failed persistence reported success");
        Check(manager.GetWallet("reward-test", wallet) && wallet.claimedLevelReward == 50 &&
            wallet.freeCurrency == 15017 && wallet.victoryCurrency == 13, "Failed save did not roll back");
        std::cout << "PASS: legacy migration, cumulative claims, duplicate/locked rejection, reload, 45+ rewards, save rollback\n";
        std::cout << "Fixture: " << fixture << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << "\nFixture: " << fixture << '\n';
        return 1;
    }
}
