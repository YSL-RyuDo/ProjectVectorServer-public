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

void SessionManager::RegisterConnection(const std::shared_ptr<Session>& session)
{
    if (session == nullptr)
        return;

    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    connections_[session.get()] = session;
}

void SessionManager::RemoveConnection(Session* session)
{
    if (session == nullptr)
        return;

    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    connections_.erase(session);
}

std::shared_ptr<Session> SessionManager::FindConnection(Session* session)
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    auto it = connections_.find(session);
    if (it == connections_.end())
        return nullptr;

    return it->second;
}

void SessionManager::Add(Session* session)
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    auto connectionIt = connections_.find(session);
    if (connectionIt == connections_.end())
    {
        std::cerr << "[SESSION ADD FAIL] unregistered connection="
            << session << std::endl;
        return;
    }

    sessions_[session->GetPlayerId()] = connectionIt->second;

    std::cout << "[SESSION ADD] playerId="
        << session->GetPlayerId()
        << std::endl;
}

void SessionManager::Remove(uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    sessions_.erase(playerId);
}

std::shared_ptr<Session> SessionManager::Find(uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    auto it = sessions_.find(playerId);
    if (it == sessions_.end())
        return nullptr;

    return it->second;
}

std::shared_ptr<Session> SessionManager::FindByUserId(
    const std::string& userId)
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);
    for (const auto& pair : sessions_)
    {
        const std::shared_ptr<Session>& session = pair.second;
        if (session != nullptr && session->GetUserId() == userId)
            return session;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Session>> SessionManager::GetAll()
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);

    std::vector<std::shared_ptr<Session>> list;
	list.reserve(sessions_.size());

    for (auto& pair : sessions_)
    {
        list.push_back(pair.second);
    }
    
    return list;
}

std::vector<std::shared_ptr<Session>> SessionManager::GetLobbySessions()
{
    std::lock_guard<std::mutex> lock(sessionManagerMutex_);

    std::vector<std::shared_ptr<Session>> list;
    list.reserve(sessions_.size());

    for (auto& pair : sessions_)
    {
        const std::shared_ptr<Session>& session = pair.second;
        if (session != nullptr && session->GetState() == SessionState::IN_LOBBY)
            list.push_back(session);
    }

    return list;
}

void SessionManager::Broadcast(void* packet)
{
	std::vector<std::shared_ptr<Session>> copied = GetAll();

	for (const std::shared_ptr<Session>& session : copied)
	{
		session->Send(packet);
	}
}

void SessionManager::BroadcastLobby(void* packet)
{
    std::vector<std::shared_ptr<Session>> copied = GetLobbySessions();

    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet);

    std::cout << "[BROADCAST LOBBY BEGIN] type=" << header->type
        << " size=" << header->size
        << " targetCount=" << copied.size()
        << std::endl;

    for (const std::shared_ptr<Session>& session : copied)
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
