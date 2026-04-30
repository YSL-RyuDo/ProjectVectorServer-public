#include "Common.h"
#include "Server.h"

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    Server server;
    server.Start();

    WSACleanup();
    return 0;
}
