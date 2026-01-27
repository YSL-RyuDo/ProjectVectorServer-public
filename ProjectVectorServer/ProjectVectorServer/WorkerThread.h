#pragma once
#include "Common.h"

class IocpCore;

class WorkerThread
{
public:
    WorkerThread(IocpCore* iocp);
    void Start();

private:
    void Run();

private:
    IocpCore* iocp_;
    std::thread thread_;
};
