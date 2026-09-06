#include "security.h"
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ag {
bool valid_token(const std::string& token) {
    return token.size() == 64 && std::all_of(token.begin(), token.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool constant_time_equal(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    volatile unsigned char difference = 0;
    for (size_t i = 0; i < left.size(); ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}
std::string read_token_file(const std::string& path) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    std::string token(64, '\0');
    if (!file.read(token.data(), 64) || !valid_token(token)) throw std::runtime_error("Invalid token file");
    std::string rest;
    char c;
    while (file.get(c)) {
        if ((c != '\r' && c != '\n') || rest.size() >= 2) throw std::runtime_error("Invalid token file length");
        rest += c;
    }
    return token;
}
void create_token_file(const std::string& path) {
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("System random generator failed");
#else
    std::ifstream random("/dev/urandom", std::ios::binary);
    if (!random.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("System random generator failed");
#endif
    std::string token;
    for (auto byte : bytes) { token += "0123456789abcdef"[byte >> 4]; token += "0123456789abcdef"[byte & 15]; }
#ifdef _WIN32
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr))
        throw std::runtime_error("Cannot create private token permissions");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    auto file = CreateFileW(std::filesystem::u8path(path).c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create token file; choose a new path in a private directory");
    DWORD written = 0;
    bool ok = WriteFile(file, token.data(), static_cast<DWORD>(token.size()), &written, nullptr) && written == token.size();
    ok = FlushFileBuffers(file) && ok;
    CloseHandle(file);
#else
    int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (file < 0) throw std::runtime_error("Cannot create token file; choose a new path in a private directory");
    bool ok = ::write(file, token.data(), token.size()) == static_cast<ssize_t>(token.size());
    ok = (::fsync(file) == 0) && ok;
    ::close(file);
#endif
    if (!ok) throw std::runtime_error("Token write failed; discard the incomplete file");
}
}
