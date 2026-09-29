#define NOMINMAX
#include <Windows.h>
#include "../ProjectVectorServer/ProjectVectorServer/ShopService.h"
#include "../ProjectVectorServer/ProjectVectorServer/CareerManager.h"
#include <cassert>
#include <iostream>
#include <thread>
int main(int argc,char** argv)
{
    assert(argc==3);
    const auto catalog=std::filesystem::absolute(argv[1]);
    const auto root=std::filesystem::absolute(argv[2]);
    assert(!std::filesystem::exists(root));std::filesystem::create_directories(root);
    std::filesystem::copy_file(catalog,root/"shop_catalog.csv");std::filesystem::current_path(root);
    std::ofstream("users.csv") << "userId,nickname\nqwer,Test\nother,Other\n";
    std::ofstream("career_user_wallet.csv") << "userId,freeCurrency,victoryCurrency,claimedLevelReward,claimedHonorGrade\nqwer,12000,100,3,2\nother,0,0,0,0\n";
    // Legacy shop data imports items/history, not a second source of currency.
    VectorShop::Engine legacy(root);
    auto previous=legacy.Execute("qwer",2,0,1,100,99);assert(previous.status==0);
    auto& career=CareerManager::Instance();career.Load();
    auto initial=career.ExecuteShop(legacy,"qwer",0,0,1,0,0);
    assert(initial.status==0 && initial.account.coin0==12000 && initial.account.coin1==100 && initial.account.owned.count(100));
    auto draw=career.ExecuteShop(legacy,"qwer",1,1,10,0,100);
    assert(draw.status==0 && draw.account.coin1==90 && draw.rewards.size()==10);
    CareerWallet wallet;assert(career.GetWallet("qwer",wallet));
    assert(wallet.freeCurrency==draw.account.coin0 && wallet.victoryCurrency==90 && wallet.claimedLevelReward==3 && wallet.claimedHonorGrade==2);
    career.Load(); // Atomic CSV round trip retains transaction ledger and rewards.
    auto retry=career.ExecuteShop(legacy,"qwer",1,1,10,0,100);
    assert(retry.status==0 && retry.account.coin1==90 && retry.account.history.size()==11);
    auto reward=career.GrantMatchReward("qwer",true);
    auto afterMatch=career.ExecuteShop(legacy,"qwer",1,1,10,0,100);
    assert(afterMatch.account.coin0==reward.totalFreeCurrency && afterMatch.account.coin1==91 && afterMatch.rewards.size()==10);
    assert(career.ExecuteShop(legacy,"other",1,0,1,0,1).status==3);
    VectorShop::Result a,b;
    std::thread one([&]{a=career.ExecuteShop(legacy,"qwer",1,1,1,0,101);});
    std::thread two([&]{b=career.ExecuteShop(legacy,"qwer",1,1,1,0,101);});one.join();two.join();
    assert(a.status==0 && b.status==0 && a.account.coin1==90 && b.account.coin1==90);
    std::filesystem::create_directory("career_user_wallet.csv.tmp");
    auto failed=career.ExecuteShop(legacy,"qwer",1,1,1,0,102);
    assert(failed.status==5 && failed.rewards.empty() && failed.account.coin1==90 && failed.account.history.size()==12);
    assert(career.GetWallet("qwer",wallet) && wallet.victoryCurrency==90);
    std::cout << "PASS: shared wallet, legacy item-only import, persisted ledger, restart/retry, concurrent request, insufficient funds, atomic failure rollback\n";
}
