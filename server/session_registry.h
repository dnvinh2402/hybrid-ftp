#ifndef SESSION_REGISTRY_H
#define SESSION_REGISTRY_H

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct SessionInfo
{
    int sessionId = 0;

    std::string clientIp;
    std::string username;

    bool authenticated = false;
    bool transferActive = false;
};

class SessionRegistry
{
public:
    void addSession(int sessionId, const std::string& clientIp);

    void removeSession(int sessionId);

    void updateAuthentication(int sessionId, const std::string& username, bool authenticated);

    void setTransferActive(int sessionId, bool transferActive);

    std::size_t getSessionCount() const;

    std::vector<SessionInfo>
    getSessions() const;

    void printSessions() const;

private:
    // Nhiều client thread có thể truy cập registry
    // cùng lúc nên cần mutex.
    mutable std::mutex registryMutex;

    std::unordered_map<int, SessionInfo> sessions;
};

#endif