#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace pinax::metadata {

// One HTTP response, or the failure to get one (status 0).
struct HttpReply {
    int status = 0;
    QByteArray body;
    QString error;                          // set when status is 0
    std::optional<int> retryAfterSeconds;   // from a Retry-After header
};

// Fetches a URL. The one seam between Pinax and the network, so tests serve
// recorded responses and never touch a provider (D-020).
class Fetcher {
public:
    virtual ~Fetcher() = default;
    virtual void get(const QUrl& url, std::function<void(const HttpReply&)> done) = 0;
};

// The real thing: Qt Network, asynchronous on the calling thread
// (ARCHITECTURE.md §4). Identifies itself as Pinax and nothing more personal.
class NetworkFetcher : public Fetcher {
public:
    explicit NetworkFetcher(QNetworkAccessManager& network);
    void get(const QUrl& url, std::function<void(const HttpReply&)> done) override;

    static QByteArray userAgent();

private:
    QNetworkAccessManager& network_;
};

} // namespace pinax::metadata
