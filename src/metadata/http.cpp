#include "metadata/http.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace pinax::metadata {

namespace {

constexpr int timeoutMs = 20000;

} // namespace

NetworkFetcher::NetworkFetcher(QNetworkAccessManager& network)
    : network_(network)
{
}

QByteArray NetworkFetcher::userAgent()
{
    return QByteArrayLiteral("Pinax/" PINAX_VERSION " (personal library catalogue)");
}

void NetworkFetcher::get(const QUrl& url, std::function<void(const HttpReply&)> done)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(timeoutMs);

    QNetworkReply* reply = network_.get(request);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done = std::move(done)] {
        HttpReply result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.body = reply->readAll();
        if (result.status == 0)
            result.error = reply->errorString();
        bool ok = false;
        const int retryAfter = reply->rawHeader("Retry-After").toInt(&ok);
        if (ok && retryAfter >= 0)
            result.retryAfterSeconds = retryAfter;
        reply->deleteLater();
        done(result);
    });
}

} // namespace pinax::metadata
