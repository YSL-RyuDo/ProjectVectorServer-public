// Listener.cpp
#include "Listener.h"
#include "Session.h"

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
        auto* session = new Session(client);

        iocp.RegisterHandle((HANDLE)client, (ULONG_PTR)session);
        session->PostRecv();
    }
}
