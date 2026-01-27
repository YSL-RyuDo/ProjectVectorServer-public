#pragma once
#include "Common.h"

class IocpCore
{
public:
    bool Init();
    bool RegisterHandle(HANDLE handle, ULONG_PTR key);
    void Dispatch();

private:
    HANDLE iocp_;
};
