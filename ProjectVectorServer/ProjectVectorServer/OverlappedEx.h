#pragma once
#include "Common.h"

enum class IOType
{
    Recv,
    Send
};

struct OverlappedEx
{
    OVERLAPPED overlapped{};
    IOType type;
    WSABUF wsaBuf;
    char buffer[1024];

    OverlappedEx(IOType t) : type(t)
    {
        wsaBuf.buf = buffer;
        wsaBuf.len = sizeof(buffer);
    }
};
