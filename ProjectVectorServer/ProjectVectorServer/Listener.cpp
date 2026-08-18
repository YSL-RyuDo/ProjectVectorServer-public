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

    while (true)
    {
        SOCKET client = accept(listenSocket_, nullptr, nullptr);
        if (client == INVALID_SOCKET)
            continue;

        auto session = std::make_shared<Session>(client);
        SessionManager::Instance().RegisterConnection(session);

        if (!iocp.RegisterHandle((HANDLE)client, (ULONG_PTR)session.get()) ||
            !session->PostRecv())
        {
            session->Disconnect();
            SessionManager::Instance().RemoveConnection(session.get());
        }
    }
}
