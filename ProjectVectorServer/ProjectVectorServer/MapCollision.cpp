#include "MapCollision.h"

// 전역 맵 충돌 데이터
static std::vector<AABB2D> g_mapColliders;

static float Clamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 서버 시작 시 한 번만 호출
void InitTestMapCollision()
{
    g_mapColliders.clear();

    g_mapColliders.push_back({
        -13.98697f,   // minX
        -0.9982288f, // minZ
        -11.00423f,  // maxX
        1.999429f    // maxZ
        });
}

bool IsBlocked(float x, float z, float radius)
{
    for (const auto& box : g_mapColliders)
    {
        float closestX = Clamp(x, box.minX, box.maxX);
        float closestZ = Clamp(z, box.minZ, box.maxZ);

        float dx = x - closestX;
        float dz = z - closestZ;

        if (dx * dx + dz * dz < radius * radius)
            return true;
    }
    return false;
}

float GetGroundHeight(float /*x*/, float /*z*/)
{
    return 1.0f;
}
