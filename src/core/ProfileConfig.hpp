#pragma once

#include <QNetworkProxy>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <string>

struct HardwareFingerprint {
    int cpuCores{8};
    int memoryGb{16};
    int screenWidth{1920};
    int screenHeight{1080};
    std::string webglVendor{"Google Inc. (Intel)"};
    std::string webglRenderer{"ANGLE (Intel, Intel UHD Graphics Direct3D11)"};
    std::string masterSeedHex;
};

struct ProfileConfig {
    QString id;
    QString name;
    QString userAgent;
    QNetworkProxy proxy{QNetworkProxy::NoProxy};
    QUrl proxyVerificationUrl;
    QString expectedProxyIp;
    QString geoDatabasePath{QStringLiteral("data/GeoLite2-City.mmdb")};
    HardwareFingerprint hardware;
    QString countryCode;
    QString timezone{"UTC"};
    int timezoneOffsetMinutes{0};
    QStringList languages{"en-US", "en"};
};
