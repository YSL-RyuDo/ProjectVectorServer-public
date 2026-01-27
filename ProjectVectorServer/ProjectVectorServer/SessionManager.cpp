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
    sessions_[session->GetSocket()] = session;
}

void SessionManager::Remove(Session* session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session->GetSocket());
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