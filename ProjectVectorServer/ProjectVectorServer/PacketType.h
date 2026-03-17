// PacketType.h
#pragma once
#include <cstdint>

enum class PacketType : uint16_t
{
    // 회원가입
    PKT_CS_REGISTER = 0,
    PKT_SC_REGISTER_RESULT = 1,

    // 로그인
    PKT_CS_LOGIN = 2,
    PKT_SC_LOGIN_RESULT = 3,

    PKT_SC_HELLO = 100,
    PKT_SC_SPAWN_PLAYER = 101,
    PKT_SC_DESPAWN_PLAYER = 102,
    PKT_CS_MOVE_STATE = 110,
    PKT_SC_MOVE_STATE = 111,
    PKT_CS_HIT = 120,
    PKT_SC_HP_UPDATE = 121,
    PKT_SC_DIE = 122,
};