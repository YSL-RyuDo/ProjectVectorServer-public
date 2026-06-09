#include "ServerWeaponData.h"

namespace
{
    const std::unordered_map<int, ServerWeaponData> g_weaponTable =
    {
        // 보조무기
        { 0,  { 0,  ServerWeaponType::Pistol, 20, 45.0f, 0.20f, 12, 24, 1.4f } }, // P92
        { 1,  { 1,  ServerWeaponType::Pistol, 25, 45.0f, 0.25f, 7,  21, 1.5f } }, // P1911
        { 2,  { 2,  ServerWeaponType::Pistol, 35, 45.0f, 0.45f, 6,  18, 1.8f } }, // R1895

        // SMG
        { 3,  { 3,  ServerWeaponType::SMG, 20, 60.0f, 0.08f, 25, 75, 2.0f } }, // UZI
        { 4,  { 4,  ServerWeaponType::SMG, 22, 65.0f, 0.10f, 30, 90, 2.2f } }, // UMP

        // Shotgun
        { 5,  { 5,  ServerWeaponType::Shotgun, 80, 25.0f, 0.80f, 5,  15, 2.5f } }, // S1897
        { 6,  { 6,  ServerWeaponType::Shotgun, 60, 25.0f, 0.25f, 8,  24, 2.8f } }, // AA-12

        // DMR
        { 7,  { 7,  ServerWeaponType::DMR, 55, 90.0f, 0.35f, 10, 30, 2.5f } }, // Dragunov

        // AR
        { 8,  { 8,  ServerWeaponType::AR, 25, 80.0f, 0.10f, 25, 50, 2.4f } }, // M416
        { 9,  { 9,  ServerWeaponType::AR, 30, 80.0f, 0.12f, 30, 60, 2.6f } }, // AK47

        // LMG
        { 10, { 10, ServerWeaponType::LMG, 28, 80.0f, 0.11f, 75, 75, 3.5f } }, // M249

        // SR
        { 11, { 11, ServerWeaponType::SR, 100, 120.0f, 1.20f, 5,  15, 3.0f } }, // Kar98
        { 12, { 12, ServerWeaponType::SR, 150, 150.0f, 2.00f, 5,  10, 3.5f } }, // AWM

        // Knife
        { 13, { 13, ServerWeaponType::Knife, 50, 2.0f, 0.30f, 0, 0, 0.0f } }
    };
}

const ServerWeaponData* ServerWeaponTable::Find(int weaponId)
{
    auto it = g_weaponTable.find(weaponId);

    if (it == g_weaponTable.end())
        return nullptr;

    return &it->second;
}