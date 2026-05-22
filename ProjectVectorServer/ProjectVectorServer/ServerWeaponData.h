#pragma once
#include <cstdint>
#include <unordered_map>

enum class ServerWeaponType : uint8_t
{
    Pistol = 0,
    AR = 1,
    SMG = 2,
    LMG = 3,
    SR = 4,
    Knife = 5
};

struct ServerWeaponData
{
    int weaponId = 0;
    ServerWeaponType weaponType = ServerWeaponType::Pistol;

    int damage = 0;
    float range = 0.0f;
    float fireRate = 0.0f;

    int magSize = 0;
    int maxReserveAmmo = 0;
    float reloadTime = 0.0f;
};

class ServerWeaponTable
{
public:
    static const ServerWeaponData* Find(int weaponId);
};