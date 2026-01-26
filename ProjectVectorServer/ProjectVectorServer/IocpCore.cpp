// IocpCore.cpp
#include "IocpCore.h"
#include "Session.h"
#include "OverlappedEx.h"

IocpCore::IocpCore() : iocp_(nullptr) {}

bool IocpCore::Init()
{
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    return iocp_ != nullptr;
}

bool IocpCore::RegisterHandle(HANDLE handle, ULONG_PTR key)
{
    return CreateIoCompletionPort(handle, iocp_, key, 0) != nullptr;
}

bool IocpCore::Dispatch()
{
    DWORD bytes;
    ULONG_PTR key;
    OverlappedEx* ov;

    BOOL ok = GetQueuedCompletionStatus(
        iocp_, &bytes, &key, (LPOVERLAPPED*)&ov, INFINITE);

    Session* session = reinterpret_cast<Session*>(key);

    if (!ok || bytes == 0)
    {
        delete session;
        return false;
    }

    if (ov->type == IOType::Recv)
        session->OnRecv(bytes);

    delete ov;
    return true;
}
