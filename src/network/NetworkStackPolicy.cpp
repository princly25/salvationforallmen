#include "network/NetworkStackPolicy.hpp"

#include <QHttp2Configuration>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslEllipticCurve>

void NetworkStackPolicy::apply(QNetworkRequest& request)
{
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setProtocol(QSsl::TlsV1_2OrLater);

    // Preserve the ordering used by current Chromium/OpenSSL builds where the
    // local TLS backend supports the cipher suite.
    const QStringList preferred{
        QStringLiteral("TLS_AES_128_GCM_SHA256"),
        QStringLiteral("TLS_AES_256_GCM_SHA384"),
        QStringLiteral("TLS_CHACHA20_POLY1305_SHA256"),
        QStringLiteral("ECDHE-ECDSA-AES128-GCM-SHA256"),
        QStringLiteral("ECDHE-RSA-AES128-GCM-SHA256"),
        QStringLiteral("ECDHE-ECDSA-AES256-GCM-SHA384"),
        QStringLiteral("ECDHE-RSA-AES256-GCM-SHA384"),
        QStringLiteral("ECDHE-ECDSA-CHACHA20-POLY1305"),
        QStringLiteral("ECDHE-RSA-CHACHA20-POLY1305"),
    };
    QList<QSslCipher> ciphers;
    for (const QString& name : preferred) {
        const QSslCipher cipher(name);
        if (!cipher.isNull()) {
            ciphers.append(cipher);
        }
    }
    if (!ciphers.isEmpty()) {
        ssl.setCiphers(ciphers);
    }
    QList<QSslEllipticCurve> curves;
    for (const QString& name : {QStringLiteral("X25519"), QStringLiteral("prime256v1"),
                                QStringLiteral("secp384r1")}) {
        const QSslEllipticCurve curve = QSslEllipticCurve::fromShortName(name);
        if (curve.isValid()) {
            curves.append(curve);
        }
    }
    if (!curves.isEmpty()) {
        ssl.setEllipticCurves(curves);
    }
    ssl.setAllowedNextProtocols({QByteArray(QSslConfiguration::ALPNProtocolHTTP2),
                                 QByteArray(QSslConfiguration::NextProtocolHttp1_1)});
    request.setSslConfiguration(ssl);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

    QHttp2Configuration http2;
    http2.setServerPushEnabled(false);
    http2.setHuffmanCompressionEnabled(true);
    http2.setSessionReceiveWindowSize(6291456);
    http2.setStreamReceiveWindowSize(6291456);
    http2.setMaxFrameSize(16384);
    request.setHttp2Configuration(http2);
}
