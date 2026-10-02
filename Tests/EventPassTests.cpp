#define NOMINMAX
#include <Windows.h>
#include "../ProjectVectorServer/ProjectVectorServer/ShopService.h"
#include "../ProjectVectorServer/ProjectVectorServer/CareerManager.h"
#include <cassert>
#include <thread>
#include <iostream>
int main(int argc,char** argv)
{
    assert(argc==2);const auto root=std::filesystem::absolute(argv[1]);
    assert(!std::filesystem::exists(root));std::filesystem::create_directories(root);std::filesystem::current_path(root);
    std::ofstream("users.csv")<<"userId,nickname\nqwer,Test\nother,Other\n";
    std::ofstream("career_seasons.csv")<<"seasonId,seasonName,startUnix,endUnix,isActive\n1,Test,0,4102444800,1\n";
    std::ofstream("career_user_wallet.csv")<<"userId,freeCurrency,victoryCurrency,claimedLevelReward,claimedHonorGrade,shopState\nqwer,0,0,1,0,\nother,0,0,0,0,\n";
    auto& c=CareerManager::Instance();c.Load();
    auto s=c.ExecutePass("qwer",0,0,0);assert(s.result==0&&s.level==1&&s.progress[0]==1);
    assert(c.ExecutePass("qwer",1,0,s.week).result==2);
    assert(c.ExecutePass("qwer",1,1,s.week-7).result==2);
    assert(c.ExecutePass("qwer",2,30,s.week).result==2);
    assert(c.ExecutePass("qwer",2,-1,s.week).result==2);
    CareerManager::PassResult a,b;
    std::thread one([&]{a=c.ExecutePass("qwer",2,1,s.week);});
    std::thread two([&]{b=c.ExecutePass("qwer",2,1,s.week);});one.join();two.join();
    assert((a.result==0)!=(b.result==0));
    c.Load();s=c.ExecutePass("qwer",0,0,0);assert(s.currency0==300&&s.rewardMask==1&&s.progress[0]==1);
    assert(c.ExecutePass("qwer",2,1,s.week).result==2);
    CareerMatchPlayer player{};player.userId="qwer";player.team=0;player.kills=20;
    for(int i=0;i<3;i++) c.RecordMatch("PassTest",std::time(nullptr),60,5,2,0,{player});
    auto progress=c.ExecutePass("qwer",0,0,0);assert(progress.progress[1]==3&&progress.progress[5]==50);
    auto quest=c.ExecutePass("qwer",1,1,progress.week);assert(quest.result==0&&quest.level==2&&quest.xp==100);
    assert(c.ExecutePass("qwer",1,1,progress.week).result==2);
    c.Load();auto restored=c.ExecutePass("qwer",0,0,0);assert(restored.xp==100&&restored.progress[1]==3&&restored.progress[5]==50);
    assert(c.ExecutePass("other",0,0,0).currency0==0);
    CareerWallet saved;assert(c.GetWallet("qwer",saved));
    const int64_t today=std::time(nullptr)/86400;
    std::ofstream("career_user_wallet.csv")<<"userId,freeCurrency,victoryCurrency,claimedLevelReward,claimedHonorGrade,shopState,passState\nqwer,"<<saved.freeCurrency<<",0,1,0,,"<<saved.passState<<"\nother,0,0,0,0,,1 "<<(today-14)*86400<<' '<<s.week-7<<' '<<today-7<<" 3 500 0 9\n";
    c.Load();auto rollover=c.ExecutePass("other",0,0,0);assert(rollover.result==0&&rollover.questMask==8&&rollover.progress[0]==1&&rollover.xp==500);
    assert(c.ExecutePass("other",1,0,s.week-7).result==2);
    CareerWallet before;assert(c.GetWallet("other",before));
    std::filesystem::create_directory("career_user_wallet.csv.tmp");
    assert(c.ExecutePass("other",2,1,s.week).result==3);
    CareerWallet after;assert(c.GetWallet("other",after));assert(before.passState==after.passState&&after.freeCurrency==0);
    assert(c.ExecutePass("missing",0,0,0).result==1);
    std::cout<<"PASS: legacy migration, isolation, eligibility, repeat visit, duplicate/concurrent claim, restart, atomic rollback\n";
}
