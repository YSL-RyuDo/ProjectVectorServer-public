#pragma once
#include "OverlappedEx.h"
#include <atomic>

class Session
{
public:
    explicit Session(SOCKET socket);
    ~Session();

    void PostRecv();
    void OnRecv(int bytes);

    SOCKET GetSocket() const { return socket_; }

private:
    SOCKET socket_;
    std::atomic<bool> connected_;
};
