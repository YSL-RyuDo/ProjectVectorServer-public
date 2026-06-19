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
    Knife = 5,
    Shotgun = 6,
    DMR = 7
};

enum class ServerWeaponId : int
{
    P92 = 0,
    P1911 = 1,
    R1895 = 2,

    UZI = 3,
    UMP = 4,

    S1897 = 5,
    AA12 = 6,

    Dragunov = 7,

    M416 = 8,
    AK47 = 9,

    M249 = 10,

    Kar98 = 11,
    AWM = 12,

    Knife = 13
};

struct ServerWeaponData
{
    int weaponId = 0;

    ServerWeaponType weaponType =
        ServerWeaponType::Pistol;

    int damage = 0;
    float range = 0.0f;

    // 다음 발사까지 필요한 시간
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