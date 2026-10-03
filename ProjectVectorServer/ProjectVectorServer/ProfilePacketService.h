#pragma once
class Session;
struct PacketHeader;
class ProfilePacketService
{
public:
    // Returns true only after successfully saving the player's appearance.
    static bool Dispatch(Session* session, PacketHeader* header);
};
