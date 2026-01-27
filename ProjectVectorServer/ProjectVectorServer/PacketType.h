#pragma once
#include <cstdint>

enum class PacketType : uint16_t
{
    PKT_SC_HELLO = 1,
    PKT_SC_SPAWN_PLAYER = 2,
    PKT_SC_DESPAWN_PLAYER = 3,

    PKT_CS_MOVE = 10,
    PKT_SC_MOVE = 11,
    PKT_CS_ROTATE = 12,
    PKT_SC_ROTATE = 13,
};