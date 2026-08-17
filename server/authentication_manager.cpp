#include "authentication_manager.h"

#include <fstream>
#include <iostream>

bool AuthenticationManager::loadUsers(const std::string& filePath)
{
    std::ifstream file(filePath);

    if (!file.is_open())
    {
        std::cerr << "Cannot open user file: " << filePath << '\n';

        return false;
    }

    users.clear();

    std::string line;

    while (std::getline(file, line))
    {
        // Bỏ qua dòng trống và comment.
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        const std::size_t separatorPosition = line.find(':');

        if (separatorPosition == std::string::npos)
        {
            continue;
        }

        const std::string username = line.substr(0, separatorPosition);

        const std::string password = line.substr(separatorPosition + 1);

        if (!username.empty() && !password.empty())
        {
            users[username] = password;
        }
    }

    return true;
}

bool AuthenticationManager::userExists(const std::string& username) const
{
    return users.find(username) != users.end();
}

bool AuthenticationManager::validateCredentials(const std::string& username, const std::string& password) const
{
    const auto iterator = users.find(username);

    if (iterator == users.end())
    {
        return false;
    }

    return iterator->second == password;
}