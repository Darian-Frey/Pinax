#include "metadata/request_queue.h"

#include <algorithm>
#include <utility>

namespace pinax::metadata {

using namespace std::chrono;

RequestQueue::RequestQueue(Fetcher& fetcher, QueuePolicy policy, QObject* parent)
    : QObject(parent)
    , fetcher_(fetcher)
    , policy_(policy)
{
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &RequestQueue::startNext);
}

void RequestQueue::enqueue(const QUrl& url, std::function<void(const HttpReply&)> done)
{
    queue_.push_back({url, std::move(done), 0, policy_.firstBackoff});
    if (!busy_ && !timer_.isActive())
        startNext();
}

void RequestQueue::cancelAll()
{
    queue_.clear();
    timer_.stop();
    ++generation_;
    busy_ = false;
    if (paused_) {
        paused_ = false;
        emit resumed();
    }
    emit idle();
}

void RequestQueue::startNext()
{
    if (busy_)
        return;
    if (queue_.empty()) {
        emit idle();
        return;
    }
    // Keep the spacing even after a pause or a quick reply.
    const auto sinceLast = duration_cast<milliseconds>(steady_clock::now() - lastStart_);
    if (lastStart_ != steady_clock::time_point {} && sinceLast < policy_.minInterval) {
        timer_.start(policy_.minInterval - sinceLast);
        return;
    }
    if (paused_) {
        paused_ = false;
        emit resumed();
    }

    Job job = std::move(queue_.front());
    queue_.pop_front();
    ++job.attempts;
    busy_ = true;
    lastStart_ = steady_clock::now();
    const unsigned generation = generation_;
    const QUrl url = job.url;
    fetcher_.get(url, [this, generation, job = std::move(job)](const HttpReply& reply) mutable {
        if (generation != generation_)
            return; // cancelled meanwhile
        busy_ = false;
        finished(std::move(job), reply);
    });
}

void RequestQueue::finished(Job job, const HttpReply& reply)
{
    const bool slowDown = reply.status == 429 || reply.status == 503;
    const bool serverTrouble = reply.status >= 500 || reply.status == 0;

    if ((slowDown || serverTrouble) && job.attempts < policy_.maxAttempts) {
        milliseconds wait = job.backoff;
        if (slowDown && reply.retryAfterSeconds)
            wait = seconds(*reply.retryAfterSeconds);
        job.backoff = std::min(job.backoff * 2, policy_.maxBackoff);
        retryLater(std::move(job), wait, slowDown);
        return;
    }

    job.done(reply);
    startNext();
}

void RequestQueue::retryLater(Job job, milliseconds wait, bool pauseQueue)
{
    queue_.push_front(std::move(job));
    if (pauseQueue) {
        paused_ = true;
        emit paused(static_cast<int>(duration_cast<seconds>(wait).count()));
    }
    timer_.start(wait);
}

} // namespace pinax::metadata
