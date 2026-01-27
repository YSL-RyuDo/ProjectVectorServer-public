#include "GroundHeightMap.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>

std::unordered_map<long long, HeightSample> GroundHeightMap::samples_;

static long long MakeKey(int x, int z)
{
    return (static_cast<long long>(x) << 32) | (z & 0xffffffff);
}

void GroundHeightMap::Load(const char* filename)
{
    samples_.clear();

    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cout << "[GroundHeightMap] Failed to open: " << filename << std::endl;
        return;
    }

    std::string line;
    int count = 0;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::stringstream ss(line);
        float x, z, y;
        char comma;

        if (!(ss >> x >> comma >> z >> comma >> y))
            continue;

        int ix = static_cast<int>(std::round(x * 2)); // grid 0.5
        int iz = static_cast<int>(std::round(z * 2));

        samples_[MakeKey(ix, iz)] = { y };
        count++;
    }

    std::cout << "[GroundHeightMap] Loaded samples: " << count << std::endl;
}

float GroundHeightMap::Sample(float x, float z)
{
    int ix = static_cast<int>(std::round(x * 2));
    int iz = static_cast<int>(std::round(z * 2));

    auto it = samples_.find(MakeKey(ix, iz));
    if (it != samples_.end())
        return it->second.y;

    return 0.0f; // fallback (절대 1.0 쓰지 마십시오)
}
