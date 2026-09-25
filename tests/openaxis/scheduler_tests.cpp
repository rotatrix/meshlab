#include "openaxis_scheduler.h"
#include <QCoreApplication>
#include <QThread>
#include <iostream>
#include <thread>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    double clock = 0;
    meshlab_openaxis::QtScheduler scheduler([&] { return clock; });
    bool posted = false, deadline = false, tooEarly = false, wrongThread = false;
    scheduler.post([&] { posted = true; wrongThread |= QThread::currentThread() != app.thread(); });
    if (posted) return 1; // SDK callbacks must never run inline.
    std::thread worker([&] {
        scheduler.post_at(.05, [&] {
            deadline = true;
            tooEarly = clock < .05;
            wrongThread |= QThread::currentThread() != app.thread();
        });
    });
    worker.join();
    bool destroyedCallback = false;
    auto *temporary = new meshlab_openaxis::QtScheduler([&] { return clock; });
    temporary->post_at(.05, [&] { destroyedCallback = true; });
    QTimer::singleShot(20, &app, [temporary] { delete temporary; });
    // Force the host timer to expire while the SDK clock is still before the
    // deadline. A retry must remain scheduled until the deadline is reached.
    QTimer::singleShot(120, &app, [&] { clock = .05; });
    QTimer::singleShot(300, &app, &QCoreApplication::quit);
    app.exec();
    if (!posted || !deadline || tooEarly || wrongThread || destroyedCallback) {
        std::cerr << "Scheduler lost/ran early a deadline, used the wrong thread, or outlived its owner\n";
        return 2;
    }
    std::cout << "Queued dispatch, retry deadline rearming, GUI thread and destruction passed\n";
}
