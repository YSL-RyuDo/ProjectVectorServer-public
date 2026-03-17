#include "Server.h"
#include "UserManager.h"
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
            listener_.Start(9000, iocp_);
        });
    UserManager::Instance().Load();
    listenerThread.join();
    return true;
}
