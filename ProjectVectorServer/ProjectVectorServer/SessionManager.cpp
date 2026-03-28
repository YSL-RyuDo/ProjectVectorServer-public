#include "SessionManager.h"
#include "Session.h"

SessionManager& SessionManager::Instance()
{
    static SessionManager inst;
    return inst;

}

uint32_t SessionManager::AllocatePlayerId()
{
	return nextPlayerId_.fetch_add(1);
}

void SessionManager::Add(Session* session)
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    sessions_[session->GetPlayerId()] = session;

    std::cout << "[SESSION ADD] playerId="
        << session->GetPlayerId()
        << std::endl;
}

void SessionManager::Remove(uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    sessions_.erase(playerId);
}

Session* SessionManager::Find(uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    auto it = sessions_.find(playerId);
    if (it == sessions_.end())
        return nullptr;

    return it->second;
}

std::vector<Session*> SessionManager::GetAll()
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);

    std::vector<Session*> list;
	list.reserve(sessions_.size());

    for (auto& pair : sessions_)
    {
        list.push_back(pair.second);
    }
    
    return list;
}

void SessionManager::Broadcast(void* packet)
{
	std::vector<Session*> copied = GetAll();

	for (Session* session : copied)
	{
		session->Send(packet);
	}
}
