#ifndef AUTHENTICATION_MANAGER_H
#define AUTHENTICATION_MANAGER_H

#include <string>
#include <unordered_map>

class AuthenticationManager
{
public:
    // Đọc danh sách tài khoản từ file.
    // Mỗi dòng có dạng: username:password
    bool loadUsers(const std::string& filePath);

    bool userExists(const std::string& username) const;

    bool validateCredentials(const std::string& username, const std::string& password) const;

private:
    std::unordered_map<std::string, std::string> users;
};

#endif