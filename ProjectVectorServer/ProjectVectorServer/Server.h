// Server.h
#pragma once
#include "IocpCore.h"
#include "Listener.h"

class Server
{
public:
    bool Start();

private:
    IocpCore iocp_;
    Listener listener_;
};
