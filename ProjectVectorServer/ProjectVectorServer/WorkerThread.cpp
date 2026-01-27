#include "WorkerThread.h"
#include "IocpCore.h"

WorkerThread::WorkerThread(IocpCore* iocp)
    : iocp_(iocp)
{
}

void WorkerThread::Start()
{
    thread_ = std::thread(&WorkerThread::Run, this);
}

void WorkerThread::Run()
{
    while (true)
        iocp_->Dispatch();
}
