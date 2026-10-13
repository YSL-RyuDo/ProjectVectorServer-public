#include "Server.h"
#include "CareerManager.h"
#include "FriendManager.h"
#include "ProfileManager.h"
#include "UserManager.h"
#include "WeaponSkinManager.h"
#include <Ws2tcpip.h>  

bool Server::Start()
{
    iocp_.Init();

    for (int i = 0; i < 4; i++)
    {
        auto* worker = new WorkerThread(&iocp_);
        worker->Start();
        workers_.push_back(worker);
    }

    std::thread listenerThread([&]()
        {
            // Opt-in isolated QA listener; ordinary deployments retain port 9000.
            int port = 9000;
            char* value=nullptr; size_t size=0;
            if (_dupenv_s(&value,&size,"VECTOR_QA_PORT")==0 && value) {
                int requested=atoi(value); if(requested>=1024 && requested<=65535) port=requested;
                free(value);
            }
            listener_.Start(static_cast<uint16_t>(port), iocp_);
        });

    UserManager::Instance().Load();
    UserManager::Instance().LoadPlayerData();
    UserManager::Instance().LoadLevelExpTable();
    UserManager::Instance().LoadCharacterOwnedData();
    UserManager::Instance().LoadCharacterAffinityData();
    CareerManager::Instance().Load();
    FriendManager::Instance().Load();
    ProfileManager::Instance().Load();
    WeaponSkinManager::Instance().Load();

    listenerThread.join();
    return true;
}
