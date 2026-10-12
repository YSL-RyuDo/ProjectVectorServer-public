#include "../ProjectVectorServer/ProjectVectorServer/AffinityService.h"
#include <cassert>
#include <iostream>
#include <thread>

using namespace VectorAffinity;
std::string Read(const std::filesystem::path& path)
{ std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}}; }
int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    assert(!std::filesystem::exists(root)); // ONLY a fresh isolated test directory.
    std::filesystem::create_directories(root);
    const std::array<int, 4> zero{};
    Store store(root);
    assert(store.Healthy());
    assert(store.Get("alice", zero).account.characters[0].level == 0);
    assert(!std::filesystem::exists(root / "character_affinity_progress.dat"));
    assert(store.Get("legacy", {0,2,5,1}).account.characters[1].level == 2);
    auto win = store.AwardMatch("alice", zero, 1, 0, true, 1, 180, true, true);
    assert(win.status == Ok && win.account.characters[0].points == 15);
    auto loss = store.AwardMatch("bob", zero, 1, 0, true, 0, 180, true, false);
    assert(loss.account.characters[0].points == 10);
    for (int i=2;i<=6;++i) loss = store.AwardMatch("bob", zero, i, 0, true, 0, 180, true, false);
    assert(loss.account.characters[0].points == 60); // No losing-streak penalty.
    auto repeat = store.AwardMatch("alice", zero, 1, 1, true, 0, 500, true, true);
    assert(repeat.account.characters[0].points == 15 && repeat.account.characters[1].points == 0);
    for (auto spec : {std::array<int,5>{0,0,180,1,1}, {1,2,180,1,1}, {1,0,179,1,1}, {1,0,180,0,1}})
        assert(store.AwardMatch("alice", zero, 2, 0, spec[0]!=0, spec[1], spec[2], spec[3]!=0, spec[4]!=0).account.revision == 1);
    assert(store.AwardMatch("alice", zero, 2, 4, true, 0, 180, true, true).status == Invalid);
    assert(store.CompleteDialogue("alice",zero,1,0,"S0_1",0).status==UnknownDialogue);
    assert(store.CompleteDialogue("alice",zero,1,0,"H1_1",0).status==Locked);
    assert(store.CompleteDialogue("alice",zero,1,0,"H0_3",0).status==InvalidChoice);
    assert(store.CompleteDialogue("alice",zero,1,0,"H0_1",1).status==InvalidChoice);
    auto seen=store.CompleteDialogue("alice",zero,1,0,"H0_1",0);
    assert(seen.status==Ok && seen.account.revision==2 && seen.account.characters[0].points==15);
    assert(store.CompleteDialogue("alice",zero,1,0,"H0_1",0).status==Ok);
    assert(store.CompleteDialogue("alice",zero,1,0,"H0_2",0).status==StaleRevision);
    auto choice=store.CompleteDialogue("alice",zero,2,0,"H0_3",2);
    assert(choice.status==Ok && choice.account.revision==3);
    assert(store.CompleteDialogue("alice",zero,3,0,"H0_3",1).status==ChoiceConflict);
    assert(store.Get("bob",zero).account.dialogues.empty());
    Store restarted(root);
    auto saved=restarted.Get("alice",zero);
    assert(saved.status==Ok && saved.account.dialogues.at({0,"H0_3"})==2 && saved.account.revision==3);
    assert(restarted.AwardMatch("alice",zero,1,0,true,0,180,true,true).account.revision==3);
    const auto original=Read(root/"character_affinity_progress.dat");
    std::filesystem::create_directory(root/"character_affinity_progress.dat.tmp");
    auto failed=restarted.AwardMatch("alice",zero,2,0,true,0,180,true,true);
    assert(failed.status==StorageError && failed.account.characters[0].points==15);
    assert(restarted.CompleteDialogue("alice",zero,3,0,"H0_2",0).status==StorageError);
    assert(Read(root/"character_affinity_progress.dat")==original);
    // Remove ONLY this test's empty failure-injection directory, never user data.
    assert(std::filesystem::remove(root/"character_affinity_progress.dat.tmp"));
    assert(restarted.AwardMatch("alice",zero,2,0,true,0,180,true,true).account.characters[0].points==30);
    Result one,two;
    std::thread a([&]{one=restarted.AwardMatch("alice",zero,3,0,true,0,180,true,true);});
    std::thread b([&]{two=restarted.AwardMatch("alice",zero,3,0,true,0,180,true,true);});a.join();b.join();
    assert(one.status==Ok && two.status==Ok && restarted.Get("alice",zero).account.characters[0].points==45);
    for (int i=4;i<110;++i) restarted.AwardMatch("alice",zero,i,0,true,0,180,true,true);
    auto capped=restarted.Get("alice",zero);
    assert(capped.account.characters[0].level==5 && capped.account.characters[0].points==1300);
    assert(restarted.NextPoints(capped.account.characters[0])==0);
    assert(capped.account.characters[1].points==0);
    auto imported=restarted.AwardMatch("legacy",{0,2,5,1},110,1,true,0,180,true,false);
    assert(imported.account.characters[1].points==260 && imported.account.characters[2].level==5);
    for(const auto& d:DialogueCatalog)
    {
        assert(d.character>=0 && d.character<4 && d.level>=0 && d.level<=5 && std::string(d.id).size()<32);
        for(const auto* next:d.next) if(next[0]) assert(FindDialogue(d.character,next));
    }
    const auto bad=root/"corrupt";std::filesystem::create_directory(bad);
    std::ofstream(bad/"character_affinity_progress.dat")<<"VECTOR_AFFINITY 1\nU \"alice\" -1 0 0 0 0 0 0 0 0 0\n";
    const auto corrupt=Read(bad/"character_affinity_progress.dat");Store broken(bad);
    assert(!broken.Healthy() && broken.AwardMatch("alice",zero,1,0,true,0,180,true,true).status==StorageError);
    assert(Read(bad/"character_affinity_progress.dat")==corrupt);
    const auto config=root/"bad-config";std::filesystem::create_directory(config);
    std::ofstream(config/"affinity_rules.csv")<<"key,value\ncompletionPoints,-1\n";
    Store invalidRules(config); assert(!invalidRules.Healthy());
    std::cout<<"PASS: win/loss/no-streak, eligibility, character isolation, legacy, cap, replay, concurrency, dialogue validation, revisions, restart, atomic rollback, corrupt data fail-closed\n";
}
