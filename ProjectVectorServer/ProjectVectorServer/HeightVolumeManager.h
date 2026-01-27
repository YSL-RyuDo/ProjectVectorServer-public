#pragma once
#include <vector>

struct HeightVolume
{
    float minX, maxX;
    float minZ, maxZ;
    float y;
};

class HeightVolumeManager
{
public:
    static void Load(const char* filename);
    static bool TryGetGroundY(float x, float z, float footY, float& outY);
    static bool IsCeilingBlocked(float x, float z, float footY, float headY);

private:
    static std::vector<HeightVolume> volumes_;
};
