#include "session_registry.h"

#include <iomanip>
#include <iostream>

void SessionRegistry::addSession(int sessionId, const std::string& clientIp)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    SessionInfo info;

    info.sessionId = sessionId;
    info.clientIp = clientIp;

    sessions[sessionId] = info;
}

void SessionRegistry::removeSession(
    int sessionId)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    sessions.erase(sessionId);
}

void SessionRegistry::updateAuthentication(int sessionId, const std::string& username, bool authenticated)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    auto iterator = sessions.find(sessionId);

    if (iterator == sessions.end())
    {
        return;
    }

    iterator->second.username = username;
    iterator->second.authenticated = authenticated;
}

void SessionRegistry::setTransferActive(int sessionId, bool transferActive)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    auto iterator = sessions.find(sessionId);

    if (iterator == sessions.end())
    {
        return;
    }

    iterator->second.transferActive = transferActive;
}

std::size_t SessionRegistry::getSessionCount() const
{
    std::lock_guard<std::mutex> lock(registryMutex);

    return sessions.size();
}

std::vector<SessionInfo> SessionRegistry::getSessions() const
{
    std::lock_guard<std::mutex> lock(registryMutex);

    std::vector<SessionInfo> result;

    result.reserve(sessions.size());

    for (const auto& entry : sessions)
    {
        result.push_back(entry.second);
    }

    return result;
}

void SessionRegistry::printSessions() const
{
    const std::vector<SessionInfo> snapshot = getSessions();

    std::cout << "\n==================== ACTIVE SESSIONS ====================\n";

    std::cout
        << std::left
        << std::setw(10) << "ID"
        << std::setw(20) << "CLIENT IP"
        << std::setw(18) << "USER"
        << "STATE\n"
        << "=========================================================\n";

    for (const SessionInfo& session : snapshot)
    {
        const std::string user =
            session.username.empty()
                ? "-"
                : session.username;

        const std::string state =
            session.transferActive
                ? "TRANSFERRING"
                : "IDLE";

        std::cout
            << std::left
            << std::setw(10)
            << session.sessionId
            << std::setw(20)
            << session.clientIp
            << std::setw(18)
            << user
            << state
            << '\n';
    }

    std::cout
        << "=========================================================\n"
        << "Total active sessions: " << snapshot.size() << '\n'
        << "=========================================================\n\n";
}