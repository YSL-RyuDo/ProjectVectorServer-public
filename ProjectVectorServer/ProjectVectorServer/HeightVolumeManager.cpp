#include "HeightVolumeManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
std::vector<HeightVolume> HeightVolumeManager::volumes_;

void HeightVolumeManager::Load(const char* filename)
{
    volumes_.clear();

    std::ifstream file(filename);
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::stringstream ss(line);
        HeightVolume v;
        char c;

        ss >> v.minX >> c >> v.maxX >> c
            >> v.minZ >> c >> v.maxZ >> c
            >> v.y;

        volumes_.push_back(v);
    }
    std::cout << "[HeightVolume] Loaded: "
        << volumes_.size() << std::endl;
}

bool HeightVolumeManager::TryGetGroundY(float x, float z, float footY, float& outY)
{
    bool found = false;
    float bestY = -100000.f;

    constexpr float ENTER_MARGIN = 0.05f;   // 다리 위 판정 여유
    constexpr float BLOCK_MARGIN = 0.3f;    // 아래에서 차단 거리

    for (const HeightVolume& v : volumes_)
    {
        if (x >= v.minX && x <= v.maxX &&
            z >= v.minZ && z <= v.maxZ)
        {
            if (footY >= v.y - ENTER_MARGIN ||
                v.y - footY <= 0.8f) // STEP_HEIGHT와 동일
            {
                if (v.y > bestY)
                {
                    bestY = v.y;
                    found = true;
                }
            }
        }
    }

    if (found)
        outY = bestY;

    return found;
}

bool HeightVolumeManager::IsCeilingBlocked(float x, float z, float footY, float headY)
{
    constexpr float BLOCK_THICKNESS = 0.5f;
    constexpr float EPS = 0.05f;

    for (const HeightVolume& v : volumes_)
    {
        if (x >= v.minX && x <= v.maxX &&
            z >= v.minZ && z <= v.maxZ)
        {
            if (footY >= v.y - EPS)
                continue;

            if (headY >= v.y - EPS &&
                headY <= v.y + BLOCK_THICKNESS)
            {
                return true;
            }
        }
    }
    return false;
}
