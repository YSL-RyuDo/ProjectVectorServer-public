#pragma once
#include "Common.h"

class Session;

class SessionManager
{
public:
    static SessionManager& Instance();

    void Add(Session* session);
    void Remove(Session* session);

    std::vector<Session*> GetAll();
    void Broadcast(void* packet);

private:
    std::unordered_map<SOCKET, Session*> sessions_;
    std::mutex mutex_;
};
