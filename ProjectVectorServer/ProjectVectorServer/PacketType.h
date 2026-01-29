// PacketType.h
#pragma once
#include <cstdint>

enum class PacketType : uint16_t
{
    PKT_SC_HELLO = 1,
    PKT_SC_SPAWN_PLAYER = 2,
    PKT_SC_DESPAWN_PLAYER = 3,
    PKT_CS_MOVE_STATE = 10,
    PKT_SC_MOVE_STATE = 11,

    PKT_CS_HIT = 20,
    PKT_SC_HP_UPDATE = 21,
    PKT_SC_DIE = 22,

};
