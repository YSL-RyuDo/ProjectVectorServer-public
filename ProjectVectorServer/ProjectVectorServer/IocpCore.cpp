#include "IocpCore.h"
#include "Session.h"
#include "SessionManager.h"
#include "OverlappedEx.h"

bool IocpCore::Init()
{
    iocp_ = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE, nullptr, 0, 0);
    return iocp_ != nullptr;
}

bool IocpCore::RegisterHandle(HANDLE handle, ULONG_PTR key)
{
    return CreateIoCompletionPort(
        handle, iocp_, key, 0) != nullptr;
}

void IocpCore::Dispatch()
{
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OverlappedEx* ov = nullptr;

    BOOL ok = GetQueuedCompletionStatus(
        iocp_, &bytes, &key,
        (LPOVERLAPPED*)&ov, INFINITE);

    Session* sessionKey = reinterpret_cast<Session*>(key);
    std::shared_ptr<Session> session =
        SessionManager::Instance().FindConnection(sessionKey);

    if (session == nullptr)
    {
        delete ov;
        return;
    }

    if (!ok || bytes == 0)
    {
        session->Disconnect();
        SessionManager::Instance().RemoveConnection(session.get());
        delete ov;
        return;
    }

    if (ov->type == IOType::Recv)
    {
        session->OnRecv(bytes, ov);
        if (!session->PostRecv())
        {
            session->Disconnect();
            SessionManager::Instance().RemoveConnection(session.get());
        }
    }

    delete ov;
}
