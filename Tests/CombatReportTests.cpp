// Real dispatcher + loopback transport; data directory MUST be a new isolated path.
#include "../ProjectVectorServer/ProjectVectorServer/PacketDispatcher.cpp"
#include <cassert>
#include <filesystem>
#include <thread>

static void TrackerTests()
{
    using namespace CombatReport;
    static_assert(static_cast<uint16_t>(PacketType::PKT_CS_COMBAT_REPORT_REQUEST) == RequestType);
    static_assert(static_cast<uint16_t>(PacketType::PKT_SC_COMBAT_REPORT) == ResponseType);
    static_assert(sizeof(CS_Hit) == 9);
    Tracker tracker;
    tracker.Reset(42);
    Request request{{sizeof(Request), RequestType}, 99, 42, 0, 0};
    Entry event{}; event.opponentId = 7; event.hpDamage = 10; event.sourceId = 0;
    tracker.Record(41, event, Region::Head); // wrong room
    tracker.Record(42, event, Region::Head);
    tracker.Record(42, event, Region::Legs);
    assert(tracker.Page(request, true).status == 1); // alive/unfrozen
    tracker.Freeze(42, 7);
    tracker.Record(42, event, Region::Head); // cannot update a death snapshot
    auto page = tracker.Page(request, true);
    assert(page.header.size == 512 && page.header.type == 371 && page.requestId == 99);
    assert(page.entryCount == 1 && page.entries[0].hpDamage == 20);
    assert(page.entries[0].hits[1] == 1 && page.entries[0].hits[2] == 1 && page.killerId == 7);
    assert(tracker.Page(request, false).totalEntries == 0);
    request.roomId = 41; assert(tracker.Page(request, true).status == 2); request.roomId = 42;
    request.pageIndex = 1; assert(tracker.Page(request, true).status == 3); request.pageIndex = 0;
    request.lifeId = page.lifeId;
    tracker.Reset(42); tracker.Freeze(42, 0);
    assert(tracker.Page(request, true).status == 2); // old life rejected after reset
    request.lifeId = 0;
    page = tracker.Page(request, true);
    assert(page.entryCount == 0 && page.pageCount == 1 && page.killerId == 0);
    tracker.Reset(42);
    for (int i = 0; i < 130; ++i) { event.sourceId = i; tracker.Record(42, event, Region::Body); }
    tracker.Freeze(42, 7);
    page = tracker.Page(request, true);
    assert(page.totalEntries == 128 && page.pageCount == 16 && page.flags == 3);
    request.lifeId = page.lifeId;
    for (int i = 0; i < 16; ++i)
    {
        request.pageIndex = static_cast<uint16_t>(i);
        auto part = tracker.Page(request, true);
        assert(part.entryCount == 8 && part.entries[0].sourceId == i * 8);
    }
    tracker.Reset(42); request.lifeId = 0; request.pageIndex = 0;
    event.hpDamage = (std::numeric_limits<uint32_t>::max)();
    tracker.Record(42, event, Region::Body); tracker.Record(42, event, Region::Body);
    tracker.Freeze(42, 7); page = tracker.Page(request, true);
    assert(page.flags == 3 && page.entries[0].hpDamage == event.hpDamage);
    tracker.Reset(42); event.hpDamage = 1;
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back([&] { for (int n=0;n<1000;++n) tracker.Record(42,event,Region::Body); });
    for (auto& worker : workers) worker.join();
    tracker.Freeze(42, 7); page = tracker.Page(request, true);
    assert(page.entries[0].hpDamage == 4000 && page.entries[0].hits[0] == 4000);
}

static void Exact(SOCKET socket, char* data, int size)
{
    while (size > 0) { const int got = recv(socket, data, size, 0); assert(got > 0); data += got; size -= got; }
}
template<class T> static T Receive(SOCKET socket, uint16_t type)
{
    for (int i = 0; i < 200; ++i)
    {
        PacketHeader header{}; Exact(socket, reinterpret_cast<char*>(&header), sizeof(header));
        assert(header.size >= sizeof(header));
        std::vector<char> bytes(header.size); memcpy(bytes.data(), &header, sizeof(header));
        Exact(socket, bytes.data()+sizeof(header), header.size-sizeof(header));
        if (header.type == type) { assert(header.size == sizeof(T)); T out{}; memcpy(&out,bytes.data(),sizeof(T)); return out; }
        assert(header.type != CombatReport::ResponseType); // no unsolicited reports
    }
    assert(false); return {};
}
static void DrainWithoutReports(SOCKET socket)
{
    for (;;)
    {
        fd_set set; FD_ZERO(&set); FD_SET(socket, &set); timeval timeout{};
        if (select(0, &set, nullptr, nullptr, &timeout) == 0) return;
        PacketHeader header{}; Exact(socket,reinterpret_cast<char*>(&header),sizeof(header));
        std::vector<char> bytes(header.size-sizeof(header)); Exact(socket,bytes.data(),static_cast<int>(bytes.size()));
        assert(header.type != CombatReport::ResponseType);
    }
}

int main(int argc, char** argv)
{
    TrackerTests();
    assert(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    assert(!std::filesystem::exists(root));
    std::filesystem::create_directories(root); std::filesystem::current_path(root);
    WSADATA wsa{}; assert(WSAStartup(MAKEWORD(2,2), &wsa) == 0);
    UserManager::Instance().Load(); UserManager::Instance().LoadLevelExpTable(); CareerManager::Instance().Load();
    FriendManager::Instance().Load(); ProfileManager::Instance().Load(); WeaponSkinManager::Instance().Load();
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0); assert(listen(listener,8) == 0);
    int length=sizeof(address); getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length);
    std::vector<std::shared_ptr<Session>> peers;
    std::vector<SOCKET> clients;
    uint32_t roomId=0;
    for (int i=0;i<7;++i)
    {
        SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        assert(connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0);
        DWORD timeout=3000; setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
        auto peer=std::make_shared<Session>(accept(listener,nullptr,nullptr));
        const std::string name="combat"+std::to_string(i);
        assert(UserManager::Instance().Register(name,name,"TestOnly123","TestOnly123") == 0);
        peer->SetUserId(name); peer->SetPlayerId(SessionManager::Instance().AllocatePlayerId());
        peer->SetAuthenticated(true); peer->SetState(SessionState::IN_GAME);
        SessionManager::Instance().RegisterConnection(peer); SessionManager::Instance().Add(peer.get());
        RoomMember member{}; member.playerId=peer->GetPlayerId(); member.nickname=name;
        Room room{};
        if (!i) roomId=RoomManager::Instance().CreateRoom("Combat test","TestMap",name,member);
        else assert(RoomManager::Instance().EnterRoom(roomId,member,room));
        peer->SetCurrentRoomId(roomId); peer->InitRoundState(); peer->SetCurrentWeaponId(0);
        peers.push_back(peer); clients.push_back(client);
    }
    closesocket(listener);
    auto hit = [&](int from,int to,uint8_t region) {
        CS_Hit packet{{sizeof(CS_Hit),static_cast<uint16_t>(PacketType::PKT_CS_HIT)},peers[to]->GetPlayerId(),region};
        PacketDispatcher::Dispatch(peers[from].get(),&packet.header);
    };
    auto request = [&](int player,uint32_t life=0,uint16_t page=0) {
        CombatReport::Request packet{{sizeof(CombatReport::Request),CombatReport::RequestType},123,roomId,life,page};
        PacketDispatcher::Dispatch(peers[player].get(),&packet.header);
        return Receive<CombatReport::Response>(clients[player],CombatReport::ResponseType);
    };
    assert(request(0).status == 1); // alive gets no data
    peers[1]->SetArmor(30);
    hit(0,1,0); assert(peers[1]->GetHP()==100 && peers[1]->GetBarrier()==10);
    hit(0,1,1); assert(peers[1]->GetHP()==90 && peers[1]->GetBarrier()==0); // head still base 20
    hit(0,1,2); assert(peers[1]->GetHP()==70); // legs still base 20
    hit(0,1,3); assert(peers[1]->GetHP()==70); // malformed region rejected
    hit(0,2,0); assert(peers[2]->GetHP()==100); // friendly fire unchanged
    CS_Hit shortHit{{sizeof(PacketHeader),120},peers[1]->GetPlayerId(),0};
    PacketDispatcher::Dispatch(peers[0].get(),&shortHit.header); assert(peers[1]->GetHP()==70);
    hit(1,0,0); // victim also dealt damage
    hit(0,1,0); hit(0,1,0); hit(0,1,0); hit(0,1,1); // last hit applies only remaining 10
    auto death=Receive<SC_Die>(clients[1],static_cast<uint16_t>(PacketType::PKT_SC_DIE));
    assert(death.deadPlayerId==peers[1]->GetPlayerId() && death.killerPlayerId==peers[0]->GetPlayerId());
    assert(peers[0]->GetMatchStats().damageDealt==130 && peers[0]->GetMatchStats().headshotKills==1);
    assert(peers[0]->GetMatchStats().kills==1 && peers[1]->GetMatchStats().deaths==1);
    auto report=request(1);
    assert(report.status==0 && report.entryCount==2 && report.killerId==peers[0]->GetPlayerId());
    const auto& received=report.entries[0];
    assert(received.direction==1 && received.hpDamage==100 && received.barrierDamage==30);
    assert(received.hits[0]==4 && received.hits[1]==2 && received.hits[2]==1 && received.hits[3]==0);
    assert(report.entries[1].direction==0 && report.entries[1].hpDamage==20);
    hit(0,1,0); auto again=request(1,report.lifeId);
    assert(memcmp(&report,&again,sizeof(report))==0); // duplicate/dead hit does not mutate snapshot
    assert(request(1,report.lifeId+1).status==2);
    assert(request(1,report.lifeId,1).status==3);
    assert(request(0).status==1); // killer cannot inspect the victim report
    peers[6]->SetHP(0); peers[6]->GetCombatReport().Freeze(roomId,0);
    assert(request(6).status==1); // observer blocked
    for (auto client:clients) DrainWithoutReports(client);
    CombatReport::Request malformed{{sizeof(PacketHeader),CombatReport::RequestType},0,roomId,0,0};
    PacketDispatcher::Dispatch(peers[1].get(),&malformed.header); DrainWithoutReports(clients[1]);
    peers[1]->SetAuthenticated(false);
    CombatReport::Request unauthenticated{{sizeof(CombatReport::Request),CombatReport::RequestType},0,roomId,0,0};
    PacketDispatcher::Dispatch(peers[1].get(),&unauthenticated.header);
    DrainWithoutReports(clients[1]); // Existing authentication gate silently rejects, before report dispatch.
    peers[1]->SetAuthenticated(true);
    const uint32_t oldLife=report.lifeId;
    peers[1]->ResetRoundStateKeepInventory(); assert(request(1).status==1);
    peers[1]->SetHP(0); peers[1]->GetCombatReport().Freeze(roomId,0);
    assert(request(1,oldLife).status==2);
    assert(request(1).entryCount==0);
    peers[1]->InitRoundState(); assert(request(1).status==1);
    // All four currently damaging skills use the shared recorder; ticks are unlocalized.
    for (int skill=1;skill<=4;++skill)
    {
        peers[1]->InitRoundState(); peers[1]->SetArmor(5); peers[1]->SetHP(10);
        peers[1]->SetLastDamager(peers[0]->GetPlayerId());
        int applied=CombatReport::ApplyDamage(peers[0].get(),peers[1].get(),999,
            CombatReport::Source::Skill,skill,CombatReport::Region::Head);
        assert(applied==15);
        auto skillReport=request(1);
        assert(skillReport.entryCount==1 && skillReport.entries[0].sourceKind==1 && skillReport.entries[0].sourceId==skill);
        assert(skillReport.entries[0].hits[3]==1 && skillReport.entries[0].hits[1]==0);
        assert(skillReport.entries[0].hpDamage==10 && skillReport.entries[0].barrierDamage==5);
    }
    // A persistent area still reports its damage when the owner has already left.
    peers[1]->InitRoundState(); peers[1]->SetLastDamager(9999);
    assert(CombatReport::ApplyDamage(nullptr,peers[1].get(),100,CombatReport::Source::Skill,4,
        CombatReport::Region::None,9999)==100);
    const auto orphan=request(1);
    assert(orphan.entryCount==1 && orphan.entries[0].opponentId==9999 && orphan.entries[0].characterId==-1);
    assert(orphan.entries[0].nickname[0]=='\0' && orphan.entries[0].hpDamage==100);
    peers[1]->Respawn(); assert(request(1).status==1);
    peers[1]->SetCurrentRoomId(0); assert(request(1).status==1);
    for (auto client:clients) closesocket(client);
    std::cout << "COMBAT_REPORT_PASS: bounds, pagination, stale/reset, concurrency, real hit dispatcher, unchanged damage/KDA, HP/barrier, skills, owner privacy, wire response and legacy compatibility\n";
}
