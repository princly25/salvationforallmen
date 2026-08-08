#include "crypto/ProfileSeedEngine.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
unsigned char decodeNibble(char value)
{
    if (value >= '0' && value <= '9') {
        return static_cast<unsigned char>(value - '0');
    }
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    if (lower >= 'a' && lower <= 'f') {
        return static_cast<unsigned char>(lower - 'a' + 10);
    }
    throw std::invalid_argument("Master seed contains a non-hexadecimal character");
}
}

ProfileSeedEngine::ProfileSeedEngine(const std::string& masterHexSeed)
{
    if (masterHexSeed.size() != m_masterSeed.size() * 2) {
        throw std::invalid_argument("Master seed must contain exactly 64 hexadecimal characters");
    }

    for (std::size_t index = 0; index < m_masterSeed.size(); ++index) {
        m_masterSeed[index] = static_cast<unsigned char>(
            (decodeNibble(masterHexSeed[index * 2]) << 4U) | decodeNibble(masterHexSeed[index * 2 + 1]));
    }
}

std::array<unsigned char, 32> ProfileSeedEngine::deriveDigest(const std::string& salt) const
{
    using ContextPointer = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    ContextPointer context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!context) {
        throw std::runtime_error("OpenSSL could not allocate a digest context");
    }

    static constexpr unsigned char domainSeparator[] = "AntiDetectBrowser/ProfileSeed/v1";
    const unsigned char delimiter = 0;
    std::array<unsigned char, 32> digest{};
    unsigned int digestLength = 0;

    const bool ok = EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1
        && EVP_DigestUpdate(context.get(), domainSeparator, sizeof(domainSeparator) - 1) == 1
        && EVP_DigestUpdate(context.get(), &delimiter, sizeof(delimiter)) == 1
        && EVP_DigestUpdate(context.get(), m_masterSeed.data(), m_masterSeed.size()) == 1
        && EVP_DigestUpdate(context.get(), &delimiter, sizeof(delimiter)) == 1
        && EVP_DigestUpdate(context.get(), salt.data(), salt.size()) == 1
        && EVP_DigestFinal_ex(context.get(), digest.data(), &digestLength) == 1;

    if (!ok || digestLength != digest.size()) {
        throw std::runtime_error("OpenSSL SHA-256 derivation failed");
    }
    return digest;
}

std::uint32_t ProfileSeedEngine::deriveSeed(const std::string& salt) const
{
    const auto digest = deriveDigest(salt);
    return (static_cast<std::uint32_t>(digest[0]) << 24U)
        | (static_cast<std::uint32_t>(digest[1]) << 16U)
        | (static_cast<std::uint32_t>(digest[2]) << 8U)
        | static_cast<std::uint32_t>(digest[3]);
}

double ProfileSeedEngine::deriveFloat(const std::string& salt, double minimum, double maximum) const
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) {
        throw std::invalid_argument("Float derivation requires finite, ordered bounds");
    }
    if (minimum == maximum) {
        return minimum;
    }

    const auto digest = deriveDigest(salt);
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8U) | digest[index];
    }

    const long double unit = static_cast<long double>(value)
        / static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    return minimum + static_cast<double>(unit * static_cast<long double>(maximum - minimum));
}
