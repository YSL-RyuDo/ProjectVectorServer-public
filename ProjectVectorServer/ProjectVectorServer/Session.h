#pragma once
#include "Common.h"
#include "OverlappedEx.h"
#include "packetType.h"
struct Vector3
{
    float x, y, z;
};

enum class SessionState
{
    CONNECTED,
    AUTHENTICATED,
    IN_LOBBY,
    IN_ROOM,
    IN_GAME
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

    void SetUserId(const std::string& id) { userId_ = id; }
    std::string GetUserId() const { return userId_; }

    void Disconnect();

    void SetAuthenticated(bool val) { isAuthenticated_ = val; }
    bool IsAuthenticated() const { return isAuthenticated_; }

    void SetPlayerId(uint32_t id) { playerId_ = id; }
    uint32_t GetPlayerId() const { return playerId_; }

    Vector3 GetPosition() const { return position_; }
    void SetPosition(float x, float y, float z)
    {
        position_.x = x;
        position_.y = y;
        position_.z = z;
    }

    void SetYaw(float yaw) { rotationYaw_ = yaw; }
    float GetYaw() const { return rotationYaw_; }

    void SendHello(uint32_t myId);
    void SendSpawn(uint32_t playerId, float x, float y, float z);

    void Spawn(float x, float y, float z, float yaw);

    int GetHP() const { return hp_; }
    void SetHP(int hp) { hp_ = hp; }
    void Damage(int amount);

    bool IsDead() const { return hp_ <= 0; }


    void Respawn();
private:
    void ProcessPackets();

private:
    SOCKET socket_;
    char recvBuffer_[4096];
    int recvSize_ = 0;
    bool isAuthenticated_ = false;
    std::string userId_;

    uint32_t playerId_ = 0;
    Vector3 position_{ 0.f, 1.f, 0.f };
    float rotationYaw_ = 0.f;

    int hp_ = 100;
    bool isDead_ = false;
};
