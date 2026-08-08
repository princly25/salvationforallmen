#pragma once

#include <array>
#include <cstdint>
#include <string>

class ProfileSeedEngine {
public:
    explicit ProfileSeedEngine(const std::string& masterHexSeed);

    [[nodiscard]] double deriveFloat(const std::string& salt, double minimum,
                                     double maximum) const;
    [[nodiscard]] std::uint32_t deriveSeed(const std::string& salt) const;

private:
    [[nodiscard]] std::array<unsigned char, 32> deriveDigest(const std::string& salt) const;

    std::array<unsigned char, 32> m_masterSeed{};
};

