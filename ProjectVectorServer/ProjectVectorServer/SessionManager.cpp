#include "SessionManager.h"
#include "Session.h"

SessionManager& SessionManager::Instance()
{
    static SessionManager inst;
    return inst;

}

void SessionManager::Add(Session* session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session->GetPlayerId()] = session;

    std::cout << "[SESSION ADD] playerId="
        << session->GetPlayerId()
        << std::endl;
}

void SessionManager::Remove(uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(playerId);
}

Session* SessionManager::Find(uint32_t playerId)
{
    auto it = sessions_.find(playerId);
    if (it == sessions_.end())
        return nullptr;

    return it->second;
}

std::vector<Session*> SessionManager::GetAll()
{
    std::vector<Session*> list;
    for (auto& pair : sessions_)
    {
        list.push_back(pair.second);
    }
    return list;
}

void SessionManager::Broadcast(void* packet)
{
    for (auto& pair : sessions_)
    {
        pair.second->Send(packet);
    }
}
