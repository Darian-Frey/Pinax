#pragma once

#include "metadata/http.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>

#include <map>
#include <vector>

namespace pinax::metadata {

// Serves scripted replies per URL, asynchronously as the network would, and
// records what was asked for. Never touches the network (D-020).
class FakeFetcher : public Fetcher {
public:
    void script(const QUrl& url, std::vector<HttpReply> replies) { scripts_[url.toString()] = std::move(replies); }

    void get(const QUrl& url, std::function<void(const HttpReply&)> done) override
    {
        requested.push_back(url.toString());
        times.push_back(clock.elapsed());
        HttpReply reply {404, {}, {}, std::nullopt};
        auto& replies = scripts_[url.toString()];
        if (!replies.empty()) {
            reply = replies.front();
            if (replies.size() > 1)
                replies.erase(replies.begin());
        }
        QTimer::singleShot(0, [reply, done] { done(reply); });
    }

    std::vector<QString> requested;
    std::vector<qint64> times;
    QElapsedTimer clock;

private:
    std::map<QString, std::vector<HttpReply>> scripts_;
};

} // namespace pinax::metadata
