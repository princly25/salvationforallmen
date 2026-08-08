#pragma once

#include <QNetworkRequest>

class NetworkStackPolicy final {
public:
    // Applies the closest Chromium-compatible TLS/ALPN and HTTP/2 settings
    // available through Qt. Chromium owns WebEngine's internal stack; these
    // settings therefore apply to QNetworkAccessManager traffic only.
    static void apply(QNetworkRequest& request);
};
