#pragma once
#include "Common.h"

class IocpCore;

class Listener
{
public:
    bool Start(short port, IocpCore& iocp);

private:
    SOCKET listenSocket_;
};
