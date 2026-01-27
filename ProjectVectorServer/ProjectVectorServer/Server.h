#pragma once
#include "Common.h"
#include "IocpCore.h"
#include "Listener.h"
#include "WorkerThread.h"

class Server
{
public:
    bool Start();

private:
    IocpCore iocp_;
    Listener listener_;
    std::vector<WorkerThread*> workers_;
};
