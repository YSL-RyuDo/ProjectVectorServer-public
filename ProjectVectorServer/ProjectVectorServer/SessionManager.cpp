#include "SessionManager.h"
#include "Session.h"
#include "Packet.h"

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

std::vector<Session*> SessionManager::GetLobbySessions()
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);

    std::vector<Session*> list;
    list.reserve(sessions_.size());

    for (auto& pair : sessions_)
    {
        Session* session = pair.second;
        if (session != nullptr && session->GetState() == SessionState::IN_LOBBY)
            list.push_back(session);
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

void SessionManager::BroadcastLobby(void* packet)
{
    std::vector<Session*> copied = GetLobbySessions();

    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet);

    std::cout << "[BROADCAST LOBBY BEGIN] type=" << header->type
        << " size=" << header->size
        << " targetCount=" << copied.size()
        << std::endl;

    for (Session* session : copied)
    {
        if (session == nullptr)
            continue;

        std::cout << "[BROADCAST LOBBY TARGET] session=" << session
            << " playerId=" << session->GetPlayerId()
            << " userId=" << session->GetUserId()
            << " state=" << (int)session->GetState()
            << std::endl;

        session->Send(packet);
    }

    std::cout << "[BROADCAST LOBBY END]" << std::endl;
}