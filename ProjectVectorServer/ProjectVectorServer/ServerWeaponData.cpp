#include "ServerWeaponData.h"

namespace
{
    const std::unordered_map<int, ServerWeaponData> g_weaponTable =
    {
        // 0: P92
        {
            static_cast<int>(ServerWeaponId::P92),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::P92),
                ServerWeaponType::Pistol,
                20,
                300.0f,
                0.2f,
                12,
                36,
                1.4f
            }
        },

        // 1: P1911
        {
            static_cast<int>(ServerWeaponId::P1911),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::P1911),
                ServerWeaponType::Pistol,
                25,
                1000.0f,
                0.25f,
                7,
                21,
                1.5f
            }
        },

        // 2: R1895
        {
            static_cast<int>(ServerWeaponId::R1895),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::R1895),
                ServerWeaponType::Pistol,
                50,
                300.0f,
                0.45f,
                6,
                18,
                1.7f
            }
        },

        // 3: UZI
        {
            static_cast<int>(ServerWeaponId::UZI),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::UZI),
                ServerWeaponType::SMG,
                24,
                200.0f,
                0.06f,
                20,
                60,
                2.2f
            }
        },

        // 4: UMP
        {
            static_cast<int>(ServerWeaponId::UMP),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::UMP),
                ServerWeaponType::SMG,
                23,
                1000.0f,
                0.075f,
                30,
                90,
                2.2f
            }
        },

        // 5: S1897
        {
            static_cast<int>(ServerWeaponId::S1897),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::S1897),
                ServerWeaponType::Shotgun,
                20,
                25.0f,
                1.0f,
                5,
                15,
                2.35f
            }
        },

        // 6: AA-12
        {
            static_cast<int>(ServerWeaponId::AA12),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::AA12),
                ServerWeaponType::Shotgun,
                19,
                100.0f,
                0.5f,
                5,
                15,
                2.1f
            }
        },

        // 7: Dragunov
        {
            static_cast<int>(ServerWeaponId::Dragunov),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::Dragunov),
                ServerWeaponType::DMR,
                75,
                3800.0f,
                0.22f,
                12,
                36,
                2.4f
            }
        },

        // 8: M416
        {
            static_cast<int>(ServerWeaponId::M416),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::M416),
                ServerWeaponType::AR,
                35,
                3600.0f,
                0.09f,
                30,
                90,
                2.4f
            }
        },

        // 9: AK47
        {
            static_cast<int>(ServerWeaponId::AK47),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::AK47),
                ServerWeaponType::AR,
                40,
                3000.0f,
                0.1f,
                25,
                75,
                2.4f
            }
        },

        // 10: M249
        {
            static_cast<int>(ServerWeaponId::M249),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::M249),
                ServerWeaponType::LMG,
                37,
                3600.0f,
                0.078f,
                100,
                200,
                4.5f
            }
        },

        // 11: Kar98
        {
            static_cast<int>(ServerWeaponId::Kar98),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::Kar98),
                ServerWeaponType::SR,
                105,
                4700.0f,
                0.8f,
                5,
                15,
                2.5f
            }
        },

        // 12: AWM
        {
            static_cast<int>(ServerWeaponId::AWM),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::AWM),
                ServerWeaponType::SR,
                150,
                3900.0f,
                2.0f,
                5,
                15,
                3.5f
            }
        },

        // 13: Knife
        {
            static_cast<int>(ServerWeaponId::Knife),
            ServerWeaponData
            {
                static_cast<int>(ServerWeaponId::Knife),
                ServerWeaponType::Knife,
                50,
                2.0f,
                0.3f,
                0,
                0,
                0.0f
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