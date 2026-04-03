#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

enum class RegisterResult : uint8_t
{
    SUCCESS = 0,
    ID_DUPLICATE = 1,
    NICK_DUPLICATE = 2,
    PASSWORD_MISMATCH = 3,
    INVALID_INPUT = 4
};

enum class LoginResult : uint8_t
{
    SUCCESS = 0,
    NO_ID = 1,
    WRONG_PASSWORD = 2,
    INVALID_INPUT = 3
};