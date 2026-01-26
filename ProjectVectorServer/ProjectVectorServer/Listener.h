// Listener.h
#pragma once
#include "IocpCore.h"

class Listener
{
public:
    bool Start(short port, IocpCore& iocp);

private:
    SOCKET listenSocket_;
};
