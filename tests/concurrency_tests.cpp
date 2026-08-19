#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

#include "../server/session_registry.h"

int main()
{
    SessionRegistry registry;

    const int numberOfClients = 8;
    std::vector<std::thread> threads;

    // Several threads modify the same registry at the same time.
    // The mutex inside SessionRegistry must keep the data consistent.
    for (int id = 1; id <= numberOfClients; ++id)
    {
        threads.emplace_back([&registry, id]()
        {
            registry.addSession(id, "127.0.0.1");
            registry.updateAuthentication(
                id,
                "user" + std::to_string(id),
                true);
            registry.setTransferActive(id, id % 2 == 0);
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    assert(registry.getSessionCount() == numberOfClients);

    const std::vector<SessionInfo> sessions = registry.getSessions();
    assert(sessions.size() == numberOfClients);

    // Remove all sessions concurrently as a second check.
    threads.clear();
    for (int id = 1; id <= numberOfClients; ++id)
    {
        threads.emplace_back([&registry, id]()
        {
            registry.removeSession(id);
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    assert(registry.getSessionCount() == 0);

    std::cout << "[TEST][PASS] concurrency_tests\n";
    return 0;
}