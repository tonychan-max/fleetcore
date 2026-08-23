// term_gui -- the vehicle terminal's screen.
//
// Two threads: one talks to termd over TCP, one draws. They exchange
// nothing but signals, so no lock appears anywhere in this program.
//
// Phase 2 splits these into separate processes over QLocalSocket
// (ADR-0001). The window's slots do not change when that happens: they
// already know nothing about where a command came from.

#include "term_gui/comm_worker.h"
#include "term_gui/lock_window.h"

#include "common/ipc_message.h"

#include <QApplication>
#include <QThread>

#include <charconv>
#include <cstdio>
#include <cstring>

using namespace fleetcore;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Registers LockRequest with Qt's type system so it can be copied into
    // the event queue when a signal crosses a thread boundary.
    qRegisterMetaType<fleetcore::LockRequest>("fleetcore::LockRequest");

    int32_t slot = 1;
    if (argc >= 2) {
        const char* first = argv[1];
        const char* last  = first + std::strlen(first);
        const auto res = std::from_chars(first, last, slot);
        if (res.ec != std::errc{} || res.ptr != last || slot <= 0) {
            std::fprintf(stderr, "slot must be a positive number\n");
            return 1;
        }
    }
    const quint16 port = static_cast<quint16>(TERM_PORT_BASE + slot);

    LockWindow window;
    window.show();

    QThread    thread;
    CommWorker worker("127.0.0.1", port);
    worker.moveToThread(&thread);

    // The two cross-thread connections. Qt sees that sender and receiver
    // live in different threads and queues the call rather than making it
    // directly -- which is why the window never touches worker data.
    QObject::connect(&worker, &CommWorker::lockReceived,
                     &window, &LockWindow::onLockReceived);
    QObject::connect(&worker, &CommWorker::linkStateChanged,
                     &window, &LockWindow::onLinkStateChanged);

    QObject::connect(&thread, &QThread::started, &worker, &CommWorker::start);

    // Shut the thread down cleanly when the window closes, otherwise the
    // process leaves a running thread behind and Qt complains.
    QObject::connect(&app, &QApplication::aboutToQuit, [&worker, &thread]() {
        QMetaObject::invokeMethod(&worker, "stop", Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    });

    thread.start();
    return app.exec();
}