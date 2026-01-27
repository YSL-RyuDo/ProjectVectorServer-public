#include "Listener.h"
#include "IocpCore.h"
#include "Session.h"
#include "SessionManager.h"
#include <Ws2tcpip.h>  

bool Listener::Start(short port, IocpCore& iocp)
{
    listenSocket_ = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(listenSocket_, (sockaddr*)&addr, sizeof(addr));
    listen(listenSocket_, SOMAXCONN);

    static uint32_t g_playerId = 1;

    while (true)
    {
        SOCKET client = accept(listenSocket_, nullptr, nullptr);
        auto* session = new Session(client);

        uint32_t myId = g_playerId++;
        session->SetPlayerId(myId);

        float spawnX = 6.0f;
        float spawnZ = 1.0f;
        float spawnYaw = -5.0f;

        session->Spawn(spawnX, spawnZ, spawnYaw);

        SessionManager::Instance().Add(session);

        session->SendSpawn(
            myId,
            session->GetX(),
            session->GetY(),
            session->GetZ()
        );

        for (Session* other : SessionManager::Instance().GetAll())
        {
            if (other == session)
                continue;

            other->SendSpawn(
                myId,
                session->GetX(),
                session->GetY(),
                session->GetZ()
            );
        }

        for (Session* other : SessionManager::Instance().GetAll())
        {
            if (other == session)
                continue;

            session->SendSpawn(
                other->GetPlayerId(),
                other->GetX(),
                other->GetY(),
                other->GetZ()
            );
        }

        iocp.RegisterHandle((HANDLE)client, (ULONG_PTR)session);
        session->PostRecv();
    }
}
