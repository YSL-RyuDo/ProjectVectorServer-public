#pragma once
#include "Common.h"

class Session;

class SessionManager
{
public:
    static SessionManager& Instance();

    void Add(Session* session);
    void Remove(uint32_t playerId);

    std::vector<Session*> GetAll();
    void Broadcast(void* packet);
    Session* Find(uint32_t playerId);

private:
    std::unordered_map<uint32_t, Session*> sessions_;
    std::mutex mutex_;
};

