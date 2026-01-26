// Session.cpp
#include "Session.h"

Session::Session(SOCKET socket)
    : socket_(socket), connected_(true)
{
}

Session::~Session()
{
    closesocket(socket_);
}

void Session::PostRecv()
{
    auto* ov = new OverlappedEx(IOType::Recv);
    DWORD flags = 0;
    WSARecv(socket_, &ov->wsaBuf, 1, nullptr, &flags, &ov->overlapped, nullptr);
}

void Session::OnRecv(int bytes)
{
    // 여기서 패킷 처리로 위임
}
