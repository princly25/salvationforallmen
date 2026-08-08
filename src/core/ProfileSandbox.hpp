#pragma once

#include <QString>

struct ProfileSandboxPaths {
    QString rootPath;
    QString persistentStoragePath;
    QString cachePath;
    QString dictionariesPath;

    [[nodiscard]] bool isComplete() const;
};

class ProfileSandbox {
public:
    [[nodiscard]] static QString defaultRootPath();
    [[nodiscard]] static bool isSafeProfileId(const QString& profileId);
    [[nodiscard]] static ProfileSandboxPaths prepare(const QString& profileId,
                                                     const QString& storageRoot = {});
};

