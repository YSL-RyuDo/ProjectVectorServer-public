#include "Common.h"
#include "Server.h"
#include "MapCollision.h"
#include "GroundHeightMap.h"
#include "HeightVolumeManager.h"

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    GroundHeightMap::Load("ServerGround.csv");
    HeightVolumeManager::Load("HeightVolumes.csv");
    InitTestMapCollision();


    Server server;
    server.Start();

    WSACleanup();
    return 0;
}
