#pragma once
#include "Common.h"
#include "atomic"

class Session;

class SessionManager
{
public:
    static SessionManager& Instance();

    uint32_t AllocatePlayerId();

    void RegisterConnection(const std::shared_ptr<Session>& session);
    void RemoveConnection(Session* session);
    std::shared_ptr<Session> FindConnection(Session* session);

    void Add(Session* session);
    void Remove(uint32_t playerId);

    std::vector<std::shared_ptr<Session>> GetAll();
    std::vector<std::shared_ptr<Session>> GetLobbySessions();

    void Broadcast(void* packet);
	void BroadcastLobby(void* packet);

    std::shared_ptr<Session> Find(uint32_t playerId);

private:
    std::unordered_map<Session*, std::shared_ptr<Session>> connections_;
    std::unordered_map<uint32_t, std::shared_ptr<Session>> sessions_;
    std::mutex sessionManagerMutex_;
	std::atomic<uint32_t> nextPlayerId_{ 1 };
};

