#pragma once
#include "Packet.h"
#include "PacketType.h"
class Session;
struct PacketHeader;

class PacketDispatcher
{
public:
    static void Dispatch(Session* session, PacketHeader* header);
    static void HandleDeath(Session* target);
};
