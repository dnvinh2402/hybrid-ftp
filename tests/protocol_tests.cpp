#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../server/authentication_manager.h"
#include "../common/sha256.h"

int main()
{
    namespace fs = std::filesystem;

    // Authentication test with a small temporary user database.
    const fs::path userFile = fs::temp_directory_path() / "hybridftp_users_test.txt";
    {
        std::ofstream output(userFile);
        output << "admin:admin123\n";
        output << "student:student123\n";
    }

    AuthenticationManager authentication;
    assert(authentication.loadUsers(userFile.string()));
    assert(authentication.userExists("admin"));
    assert(!authentication.userExists("unknown"));
    assert(authentication.validateCredentials("admin", "admin123"));
    assert(!authentication.validateCredentials("admin", "wrong"));

    // SHA-256 standard known-answer test:
    // SHA256("abc") = ba7816bf...15ad
    const fs::path hashFile = fs::temp_directory_path() / "hybridftp_sha_test.txt";
    {
        std::ofstream output(hashFile, std::ios::binary);
        output << "abc";
    }

    const std::string digest = SHA256::hashFile(hashFile.string());
    assert(digest ==
           "ba7816bf8f01cfea414140de5dae2223"
           "b00361a396177a9cb410ff61f20015ad");

    fs::remove(userFile);
    fs::remove(hashFile);

    std::cout << "[TEST][PASS] protocol_tests\n";
    return 0;
}