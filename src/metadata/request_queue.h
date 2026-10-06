#pragma once

#include "metadata/http.h"

#include <QObject>
#include <QTimer>

#include <chrono>
#include <deque>
#include <functional>

namespace pinax::metadata {

// When to ask, and when to wait. Defaults suit the providers; tests shrink
// them to milliseconds.
struct QueuePolicy {
    std::chrono::milliseconds minInterval {1000};    // between the starts of two requests
    std::chrono::milliseconds firstBackoff {30000};  // after a 429 or 503 with no Retry-After
    std::chrono::milliseconds maxBackoff {600000};
    int maxAttempts = 5; // per request, before its failure is delivered
};

// Serialises requests to one provider and keeps them polite (AV-009): no two
// start closer than `minInterval`; a 429 or 503 pauses the whole queue — for
// the Retry-After the provider gives, else a doubling backoff — and the same
// request is tried again; other server errors and network failures are
// retried with backoff too. Only after `maxAttempts` is a failure handed
// back. Everything is driven by timers on the calling thread.
class RequestQueue : public QObject {
    Q_OBJECT

public:
    RequestQueue(Fetcher& fetcher, QueuePolicy policy = {}, QObject* parent = nullptr);

    void enqueue(const QUrl& url, std::function<void(const HttpReply&)> done);

    // Drops every request not yet started. The one in flight, if any,
    // completes but its answer is discarded.
    void cancelAll();

    int pending() const { return static_cast<int>(queue_.size()) + (busy_ ? 1 : 0); }
    bool isPaused() const { return paused_; }

signals:
    // The provider asked us to wait; requests resume in about `seconds`.
    void paused(int seconds);
    void resumed();
    // Nothing queued or in flight.
    void idle();

private:
    struct Job {
        QUrl url;
        std::function<void(const HttpReply&)> done;
        int attempts = 0;
        std::chrono::milliseconds backoff {0};
    };

    void startNext();
    void finished(Job job, const HttpReply& reply);
    void retryLater(Job job, std::chrono::milliseconds wait, bool pauseQueue);

    Fetcher& fetcher_;
    QueuePolicy policy_;
    std::deque<Job> queue_;
    QTimer timer_;
    bool busy_ = false;
    bool paused_ = false;
    unsigned generation_ = 0; // bumped by cancelAll, so late replies are dropped
    std::chrono::steady_clock::time_point lastStart_ {};
};

} // namespace pinax::metadata
