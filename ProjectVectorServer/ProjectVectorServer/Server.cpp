// Server.cpp
#include "Server.h"
#include <thread>

bool Server::Start()
{
    iocp_.Init();

    std::thread listenerThread([&]()
        {
            listener_.Start(9000, iocp_);
        });

    while (true)
    {
        iocp_.Dispatch();
    }

    listenerThread.join();
    return true;
}
