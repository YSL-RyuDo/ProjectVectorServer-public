// Main.cpp
#include <winsock2.h>
#include <iostream>
#include "Core/Server.h"

#pragma comment(lib, "ws2_32.lib")

int main()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cout << "WSAStartup failed\n";
        return -1;
    }

    Server server;
    if (!server.Start())
    {
        std::cout << "Server start failed\n";
        WSACleanup();
        return -1;
    }

    // Server::Start()는 내부에서 Dispatch 루프를 돈다
    // main은 여기서 블로킹되지 않아도 됨
    std::cout << "Server stopped\n";

    WSACleanup();
    return 0;
}
