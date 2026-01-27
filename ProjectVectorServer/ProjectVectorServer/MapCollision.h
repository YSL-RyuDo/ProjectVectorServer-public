#pragma once
#include <vector>

// 서버용 2D AABB (XZ 평면)
struct AABB2D
{
    float minX;
    float minZ;
    float maxX;
    float maxZ;
};

// 서버 시작 시 1회 호출
void InitTestMapCollision();

// (x, z) 위치에서 플레이어 반지름을 고려해 막혔는지 검사
bool IsBlocked(float x, float z, float radius);

// Ground 높이 (지금은 임시, 이후 확장)
float GetGroundHeight(float x, float z);
