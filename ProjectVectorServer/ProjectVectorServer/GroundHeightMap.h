#pragma once
#include <unordered_map>

struct HeightSample
{
    float y;
};

class GroundHeightMap
{
public:
    static void Load(const char* filename);
    static float Sample(float x, float z);

private:
    static std::unordered_map<long long, HeightSample> samples_;
};
