#pragma once
#include "Common.h"
#include "atomic"

class Session;

class SessionManager
{
public:
    static SessionManager& Instance();

    uint32_t AllocatePlayerId();

    void Add(Session* session);
    void Remove(uint32_t playerId);

    std::vector<Session*> GetAll();
    std::vector<Session*> GetLobbySessions();

    void Broadcast(void* packet);
	void BroadcastLobby(void* packet);

    Session* Find(uint32_t playerId);

private:
    std::unordered_map<uint32_t, Session*> sessions_;
    std::mutex sessionManagerMutex_;
	std::atomic<uint32_t> nextPlayerId_{ 1 };
};

