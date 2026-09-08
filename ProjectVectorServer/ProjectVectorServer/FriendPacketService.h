#pragma once

class Session;
struct PacketHeader;

class FriendPacketService
{
public:
    static void Dispatch(Session* session, PacketHeader* header);
};
