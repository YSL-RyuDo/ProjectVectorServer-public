// IocpCore.h
#pragma once
#include <windows.h>

class IocpCore
{
public:
    IocpCore();
    ~IocpCore();

    bool Init();
    bool RegisterHandle(HANDLE handle, ULONG_PTR key);
    bool Dispatch();

private:
    HANDLE iocp_;
};
