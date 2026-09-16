#pragma once
class Session;
struct PacketHeader;
class ProfilePacketService
{
public:
    static void Dispatch(Session* session, PacketHeader* header);
};
