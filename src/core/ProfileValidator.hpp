#pragma once

#include "core/ProfileConfig.hpp"

#include <QStringList>

struct ValidationResult {
    bool isValid{true};
    QStringList discrepancies;
};

class ProfileValidator {
public:
    [[nodiscard]] static ValidationResult validateProfile(const ProfileConfig& config);
};

