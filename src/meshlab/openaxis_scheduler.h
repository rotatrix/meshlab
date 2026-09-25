#pragma once
#include <openaxis/scheduler.hpp>
#include <QObject>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <climits>

namespace meshlab_openaxis {
class QtScheduler final : public QObject, public openaxis::Scheduler {
public:
    using Clock = std::function<double()>;
    explicit QtScheduler(Clock clock = [] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }) : clock(std::move(clock)) {}
    std::function<void()> beforeDispatch;
    void post(Callback callback) override {
        QMetaObject::invokeMethod(this, [this, callback = std::move(callback)] {
            if (beforeDispatch) beforeDispatch();
            callback();
        }, Qt::QueuedConnection);
    }
    void post_at(double deadline, Callback callback) override {
        QMetaObject::invokeMethod(this, [this, deadline, callback = std::move(callback)] {
            arm(deadline, callback);
        }, Qt::QueuedConnection);
    }
private:
    Clock clock;
    void arm(double deadline, Callback callback) {
        const auto delay = int(std::clamp(std::ceil((deadline - clock()) * 1000), 0., double(INT_MAX)));
        QTimer::singleShot(delay, Qt::PreciseTimer, this, [this, deadline, callback] {
            // The SDK consumes this wakeup once. Never deliver it before its
            // absolute deadline: an early retry/timeout would otherwise be lost.
            if (clock() < deadline) { arm(deadline, callback); return; }
            if (beforeDispatch) beforeDispatch();
            callback();
        });
    }
};
}
