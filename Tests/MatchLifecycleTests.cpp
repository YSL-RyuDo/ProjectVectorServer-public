// Standalone harness only (not in the shipping vcxproj). Calls the real dispatcher
// and settlement code with loopback sockets and fresh temporary account data.
#include "../ProjectVectorServer/ProjectVectorServer/PacketDispatcher.cpp"
#include <cassert>
#include <filesystem>

static void Exact(SOCKET socket, char* data, int size)
{
    while (size > 0) { int got=recv(socket,data,size,0); assert(got>0); data+=got; size-=got; }
}
template<class T> T Receive(SOCKET socket, PacketType type)
{
    for(int tries=0;tries<100;tries++)
    {
        PacketHeader header{}; Exact(socket,reinterpret_cast<char*>(&header),sizeof(header));
        std::vector<char> packet(header.size); memcpy(packet.data(),&header,sizeof(header));
        Exact(socket,packet.data()+sizeof(header),header.size-sizeof(header));
        if(header.type==static_cast<uint16_t>(type)) { assert(header.size==sizeof(T)); T out{}; memcpy(&out,packet.data(),sizeof(T)); return out; }
    }
    assert(false); return {};
}
int main(int argc,char** argv)
{
    assert(argc==2);
    const auto root=std::filesystem::absolute(argv[1]);
    assert(!std::filesystem::exists(root)); std::filesystem::create_directories(root); std::filesystem::current_path(root);
    std::ofstream("career_seasons.csv") << "seasonId,seasonName,startUnix,endUnix,isActive\n1,Test,0,4102444800,1\n";
    WSADATA wsa{}; assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    UserManager::Instance().Load(); UserManager::Instance().LoadLevelExpTable(); CareerManager::Instance().Load();
    FriendManager::Instance().Load(); ProfileManager::Instance().Load(); WeaponSkinManager::Instance().Load();
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0); assert(listen(listener,6)==0);
    int length=sizeof(address); getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length);
    std::vector<std::shared_ptr<Session>> peers; std::vector<SOCKET> clients;
    for(int i=0;i<6;i++)
    {
        SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); assert(connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
        DWORD timeout=3000; setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
        auto peer=std::make_shared<Session>(accept(listener,nullptr,nullptr));
        std::string user="lifecycle"+std::to_string(i);
        assert(UserManager::Instance().Register(user,user,"TestOnly123","TestOnly123")==0);
        ChipCraftRecord craft{}; assert(ProfileManager::Instance().GetChipCraft(user,craft));
        craft.preset.cardId=static_cast<uint8_t>(i%3);
        assert(ProfileManager::Instance().SaveChipCraft(user,craft.preset));
        assert(UserManager::Instance().SetPlayerIcon(user,i%3));
        peer->SetUserId(user); peer->SetPlayerId(SessionManager::Instance().AllocatePlayerId()); peer->SetAuthenticated(true); peer->SetState(SessionState::IN_LOBBY);
        SessionManager::Instance().RegisterConnection(peer); SessionManager::Instance().Add(peer.get());
        peers.push_back(peer); clients.push_back(client);
    }
    closesocket(listener);
    auto id=[&](int i){return peers[i]->GetPlayerId();};
    // Extreme skill differences may change team balance, never split a duo.
    for(int sample=0;sample<32;sample++)
    {
        std::vector<MatchmakingEntry> input(6),balanced; double expected=0;
        for(int i=0;i<6;i++) { input[i].playerId=id(i); input[i].partyId=i<4?100+i/2:200+i; input[i].partySize=i<4?2:1; input[i].matchmakingRating=((i+sample)%5)*500; }
        assert(BuildBalancedMatchmakingGroup(input,balanced,expected)); assert(!DoesTeamMaskSplitParty(balanced,0b010101));
        input[4].partyId=input[5].partyId=300; input[4].partySize=input[5].partySize=2;
        assert(!BuildBalancedMatchmakingGroup(input,balanced,expected)); // three duos cannot form intact 3v3 teams
    }
    assert(g_lobbyParties.Join(id(0),id(1)) && g_lobbyParties.Join(id(0),id(2)));
    assert(g_lobbyParties.Join(id(3),id(4)) && g_lobbyParties.Join(id(3),id(5)));
    auto queue=[&](uint8_t mode)
    {
        // Match the existing party-mode gate before requesting ranked/normal queue.
        assert(g_lobbyParties.SetMode(id(0), mode));
        assert(g_lobbyParties.SetMode(id(3), mode));
        QueueSocialParty(peers[0].get(),mode); QueueSocialParty(peers[3].get(),mode); assert(g_matchmakingQueue.size()==6);
        for(auto& entry:g_matchmakingQueue) entry.queuedAt-=std::chrono::seconds(6);
        std::vector<MatchmakingEntry> group; assert(TryTakeMatchmakingGroup(mode,group));
        assert(!DoesTeamMaskSplitParty(group,0b010101)); assert(CreateMatchmakingRoom(group));
        return peers[0]->GetCurrentRoomId();
    };
    uint32_t roomId=queue(1);
    PacketHeader leave{sizeof(PacketHeader),static_cast<uint16_t>(PacketType::PKT_CS_RESULT_LEAVE)};
    PacketDispatcher::Dispatch(peers[0].get(),&leave); assert(peers[0]->GetCurrentRoomId()==roomId); // cannot forge an early return
    Room room{}; assert(RoomManager::Instance().GetRoom(roomId,room));
    uint32_t mvp=0;
    for(auto& member:room.members)
    {
        auto peer=SessionManager::Instance().Find(member.playerId); auto& stats=peer->GetMatchStats();
        stats.kills=member.team==RoomTeam::A?10+member.slotIndex:90; stats.assists=5; stats.deaths=2; stats.firstHalfKills=stats.kills;
        if(member.team==RoomTeam::A && member.slotIndex==2) mvp=member.playerId;
    }
    BroadcastMatchResultDetail(roomId,RoomTeam::A,7,4);
    std::vector<int> rankAfter;
    for(int i=0;i<6;i++)
    {
        auto appearance=Receive<SC_MatchAppearance>(clients[i],PacketType::PKT_SC_MATCH_APPEARANCE);
        assert(appearance.roomId==roomId && appearance.memberCount==6);
        for(const auto& value:appearance.members)
        {
            assert(value.playerIconId==value.preset.cardId);
            for(int p=0;p<6;p++) if(value.playerId==id(p)) assert(value.playerIconId==p%3);
        }
        auto packet=Receive<SC_MatchResultDetail>(clients[i],PacketType::PKT_SC_MATCH_RESULT_DETAIL);
        assert(packet.memberCount==6 && packet.matchType==1 && packet.scoreA==7 && packet.scoreB==4);
        int mvps=0;
        for(auto& member:packet.members)
        {
            if(member.isMvp) { mvps++; assert(member.playerId==mvp); }
            if(member.playerId!=id(i)) continue;
            assert(member.rankPointsAfter-member.rankPointsBefore==member.rankPointDelta);
            assert(member.rankPointDelta==(member.team==0?member.rankPointsAfter:0)); // fresh-account floor
            CareerWallet wallet{}; assert(CareerManager::Instance().GetWallet(peers[i]->GetUserId(),wallet));
            assert(wallet.freeCurrency==member.totalFreeCurrency && wallet.victoryCurrency==member.totalVictoryCurrency);
            assert(member.victoryCurrencyReward==(member.team==0?1:0)); rankAfter.push_back(member.rankPointsAfter);
        }
        assert(mvps==1);
    }
    CareerWallet before{},after{}; CareerManager::Instance().GetWallet(peers[0]->GetUserId(),before);
    BroadcastMatchResultDetail(roomId,RoomTeam::A,7,4); CareerManager::Instance().GetWallet(peers[0]->GetUserId(),after);
    assert(before.freeCurrency==after.freeCurrency && before.victoryCurrency==after.victoryCurrency);
    assert(CareerManager::Instance().GetMatchHistory(peers[0]->GetUserId(),-1,10).size()==1);
    // Return separately, as real clients do after finishing the rank/MVP presentation.
    PacketDispatcher::Dispatch(peers[0].get(),&leave); QueueSocialParty(peers[0].get(),1); assert(g_matchmakingQueue.empty());
    PacketDispatcher::Dispatch(peers[1].get(),&leave); QueueSocialParty(peers[0].get(),1); assert(g_matchmakingQueue.empty());
    assert(g_lobbyParties.Members(id(0)).size()==3 && g_lobbyParties.Leader(id(1))==id(0));
    SendPartyAvailability(peers[0].get());
    SC_PartyAvailability availability{};
    do { availability=Receive<SC_PartyAvailability>(clients[0],PacketType::PKT_SC_PARTY_AVAILABILITY); } while(availability.lobbyMask!=3);
    assert(availability.count==3);
    for(int i=2;i<6;i++) PacketDispatcher::Dispatch(peers[i].get(),&leave);
    // New normal match: same parties, no visible ranked-point changes.
    roomId=queue(0); BroadcastMatchResultDetail(roomId,RoomTeam::B,3,7);
    for(int i=0;i<6;i++)
    {
        auto packet=Receive<SC_MatchResultDetail>(clients[i],PacketType::PKT_SC_MATCH_RESULT_DETAIL); assert(packet.matchType==0);
        CareerOverview overview{}; assert(CareerManager::Instance().GetOverview(peers[i]->GetUserId(),overview)); assert(overview.stats.rankPoints==rankAfter[i]);
        PacketDispatcher::Dispatch(peers[i].get(),&leave);
    }
    QueueSocialParty(peers[0].get(),0); assert(g_matchmakingQueue.size()==3);
    peers[0]->Disconnect(); TickSocialLobby();
    assert(g_matchmakingQueue.empty()); assert(g_lobbyParties.Leader(id(1))==id(1) && g_lobbyParties.Members(id(1)).size()==2);
    std::cout << "PASS: real settlement packet MVP, rank delta/floor, shared wallet, duplicate settlement, forged early return, partial-return queue block, party retention/rematch, normal rank protection, disconnect cancellation/leader succession\n";
    for(auto client:clients) closesocket(client);
    return 0;
}
