#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#pragma comment(lib, "bcrypt.lib")

// No signing material is sent to the client. Configuration is read once at server startup/use.
namespace VectorVoice
{
inline std::string Env(const char* name)
{
    char* value = nullptr; size_t size = 0;
    _dupenv_s(&value, &size, name);
    std::string result = value ? value : ""; free(value); return result;
}
inline bool SafeName(const std::string& value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}
struct Configuration
{
    const std::string server = Env("VECTOR_VIVOX_SERVER");
    const std::string domain = Env("VECTOR_VIVOX_DOMAIN");
    const std::string issuer = Env("VECTOR_VIVOX_ISSUER");
    const std::string key = Env("VECTOR_VIVOX_SIGNING_KEY");
    bool Ready() const {
        return server.starts_with("https://") && server.size() < 256 &&
            domain.size() < 128 && issuer.size() <= 79 && SafeName(domain) && SafeName(issuer) &&
            !key.empty() && key.size() <= 4096;
    }
};
inline const Configuration& Config() { static const Configuration config; return config; }
inline std::string Base64Url(const unsigned char* data, size_t count)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result; unsigned value = 0; int bits = 0;
    for (size_t i = 0; i < count; ++i) {
        value = (value << 8) | data[i]; bits += 8;
        while (bits >= 6) { bits -= 6; result += alphabet[(value >> bits) & 63]; }
    }
    if (bits) result += alphabet[(value << (6 - bits)) & 63];
    return result;
}
inline std::string Base64Url(const std::string& text) { return Base64Url(reinterpret_cast<const unsigned char*>(text.data()), text.size()); }
inline std::string Sign(const std::string& key, const std::string& text)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<unsigned char, 32> digest{};
    auto check = [](NTSTATUS status) { if (status < 0) throw std::runtime_error("Voice crypto unavailable"); };
    try {
        check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG));
        check(BCryptCreateHash(algorithm, &hash, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())), static_cast<ULONG>(key.size()), 0));
        check(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), static_cast<ULONG>(text.size()), 0));
        check(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    } catch (...) {
        if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); throw;
    }
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
    return Base64Url(digest.data(), digest.size());
}
inline std::string Nonce()
{
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Voice random unavailable");
    return Base64Url(bytes.data(), bytes.size());
}
inline std::string Account(const Configuration& config, const std::string& gameAccount)
{
    return "v_" + Sign(config.key, "vector-account:" + gameAccount);
}
inline std::string UserUri(const Configuration& config, const std::string& account)
{ return "sip:." + config.issuer + "." + account + ".@" + config.domain; }
inline std::string ChannelUri(const Configuration& config, const std::string& channel)
{ return "sip:confctl-g-" + config.issuer + "." + channel + "@" + config.domain; }
inline std::string Token(const Configuration& config, const std::string& account, const std::string& channel)
{
    static std::atomic<uint64_t> serial{static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) & 0x1FFFFFFFFFFFFFULL};
    const auto expires = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 60;
    std::string payload = "{\"iss\":\"" + config.issuer + "\",\"exp\":" + std::to_string(expires) +
        ",\"vxa\":\"" + (channel.empty() ? "login" : "join") + "\",\"vxi\":" + std::to_string(++serial) +
        ",\"f\":\"" + UserUri(config, account) + "\"";
    if (!channel.empty()) payload += ",\"t\":\"" + ChannelUri(config, channel) + "\"";
    payload += "}";
    const auto unsignedToken = "e30." + Base64Url(payload);
    return unsignedToken + "." + Sign(config.key, unsignedToken);
}
}

#pragma pack(push, 1)
struct CS_VoiceRequest
{
    PacketHeader header; uint32_t requestId; uint8_t action; // 0=context, 1=login, 2=join
    char channel[64];
};
struct SC_VoiceResponse
{
    PacketHeader header; uint32_t requestId; uint8_t status; uint8_t scope; // 0=none, 1=party, 2=team
    char server[256]; char domain[128]; char issuer[128]; char account[64]; char channel[64];
    char nickname[32]; char token[2048];
};
#pragma pack(pop)
static_assert(sizeof(CS_VoiceRequest) == 73);
static_assert(sizeof(SC_VoiceResponse) == 2730);
