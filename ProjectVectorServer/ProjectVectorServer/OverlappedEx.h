#pragma once
#include <winsock2.h>

enum class IOType
{
    Accept,
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
