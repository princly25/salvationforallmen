#include "core/ProfileSandbox.hpp"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

#include <stdexcept>

bool ProfileSandboxPaths::isComplete() const
{
    return !rootPath.isEmpty() && !persistentStoragePath.isEmpty() && !cachePath.isEmpty()
        && !dictionariesPath.isEmpty();
}

QString ProfileSandbox::defaultRootPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("profiles"));
}

bool ProfileSandbox::isSafeProfileId(const QString& profileId)
{
    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return profileId != QStringLiteral(".") && profileId != QStringLiteral("..")
        && safeId.match(profileId).hasMatch();
}

ProfileSandboxPaths ProfileSandbox::prepare(const QString& profileId, const QString& storageRoot)
{
    if (!isSafeProfileId(profileId)) {
        throw std::invalid_argument("Profile id contains unsafe path characters");
    }

    const QString basePath = QDir::cleanPath(storageRoot.isEmpty() ? defaultRootPath() : storageRoot);
    QDir baseDirectory(basePath);
    if (!baseDirectory.mkpath(QStringLiteral("."))) {
        throw std::runtime_error("Unable to create the profile storage root");
    }

    ProfileSandboxPaths paths;
    paths.rootPath = baseDirectory.filePath(profileId);
    paths.persistentStoragePath = QDir(paths.rootPath).filePath(QStringLiteral("storage"));
    paths.cachePath = QDir(paths.rootPath).filePath(QStringLiteral("cache"));
    paths.dictionariesPath = baseDirectory.filePath(QStringLiteral("dictionaries"));

    for (const QString& path : {paths.rootPath, paths.persistentStoragePath, paths.cachePath,
                                paths.dictionariesPath}) {
        if (!QDir().mkpath(path)) {
            throw std::runtime_error("Unable to create an isolated profile directory");
        }
    }

    const QByteArray dictionaries = QDir::toNativeSeparators(paths.dictionariesPath).toUtf8();
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_DICTIONARIES_PATH")) {
        qputenv("QTWEBENGINE_DICTIONARIES_PATH", dictionaries);
    }

    return paths;
}

