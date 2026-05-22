#include "ServerWeaponData.h"

namespace
{
    const std::unordered_map<int, ServerWeaponData> g_weaponTable =
    {
        {
            1,
            ServerWeaponData
            {
                1,
                ServerWeaponType::AR,
                40,
                1500.0f,
                0.1f,
                25,
                50,
                2.4f
            }
        },
        {
            2,
            ServerWeaponData
            {
                2,
                ServerWeaponType::SR,
                150,
                1500.0f,
                2.0f,
                5,
                10,
                3.5f
            }
        },
        {
            3,
            ServerWeaponData
            {
                3,
                ServerWeaponType::Knife,
                50,
                2.0f,
                0.3f,
                0,
                0,
                0.0f
            }
        },
        {
            4,
            ServerWeaponData
            {
                4,
                ServerWeaponType::Pistol,
                20,
                45.0f,
                0.1f,
                12,
                24,
                1.4f
            }
        }
    };
}

const ServerWeaponData* ServerWeaponTable::Find(int weaponId)
{
    auto it = g_weaponTable.find(weaponId);

    if (it == g_weaponTable.end())
        return nullptr;

    return &it->second;
}