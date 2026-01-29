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

        // 1. HELLO
        session->SendHello(myId);

        // 2. 스폰 위치 설정 (좌표 그대로)
        session->Spawn(6.0f, 1.0f, 6.0f, 0.0f);

        // 3. 세션 등록
        SessionManager::Instance().Add(session);

        // 4. 자기 자신 스폰 전송
        {
            Vector3 pos = session->GetPosition();
            session->SendSpawn(
                myId,
                pos.x,
                pos.y,
                pos.z
            );
        }

        // 5. 기존 유저들에게 "새 유저" 스폰 전송
        for (Session* other : SessionManager::Instance().GetAll())
        {
            if (other == session)
                continue;

            Vector3 myPos = session->GetPosition();
            other->SendSpawn(
                myId,
                myPos.x,
                myPos.y,
                myPos.z
            );
        }

        // 6. 새 유저에게 "기존 유저들" 스폰 전송
        for (Session* other : SessionManager::Instance().GetAll())
        {
            if (other == session)
                continue;

            Vector3 otherPos = other->GetPosition();
            session->SendSpawn(
                other->GetPlayerId(),
                otherPos.x,
                otherPos.y,
                otherPos.z
            );
        }

        // 7. IOCP 등록
        iocp.RegisterHandle((HANDLE)client, (ULONG_PTR)session);
        session->PostRecv();
    }
}
