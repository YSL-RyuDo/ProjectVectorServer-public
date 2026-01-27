#pragma once
#include "Common.h"
#include "OverlappedEx.h"

struct Vector3
{
    float x, y, z;
};

class Session
{
public:
    explicit Session(SOCKET socket);
    ~Session();

    void PostRecv();
    void OnRecv(int bytes, OverlappedEx* ov);
    SOCKET GetSocket() const { return socket_; }
    void Send(void* packet);

    Vector3 GetPosition() const { return position_; }
    void SetYaw(float yaw) { rotationYaw_ = yaw; }
    float GetYaw() const { return rotationYaw_; }

    void SetPlayerId(uint32_t id) { playerId_ = id; }
    uint32_t GetPlayerId() const { return playerId_; }

    float GetX() const { return position_.x; }
    float GetY() const { return position_.y; }
    float GetZ() const { return position_.z; }

    void Spawn(float spawnX, float spawnZ, float yaw);
    void SendSpawn(uint32_t playerId, float x, float y, float z);
    void OnMove(float inputX, float inputZ, float yaw);

    
private:
    void ProcessPackets();

private:
    SOCKET socket_;
    char recvBuffer_[4096];
    int recvSize_;

    uint32_t playerId_;
    Vector3 position_{ 0, 1, 0 };
    float rotationYaw_ = 0.f;
};
