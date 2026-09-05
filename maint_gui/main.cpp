// maint_gui -- the maintenance screen.
//
// Same two-thread shape as term_gui, opposite direction: this one sends
// commands and displays acknowledgements.

#include "maint_gui/maint_window.h"
#include "maint_gui/maint_worker.h"

#include "common/ipc_message.h"

#include <QApplication>
#include <QThread>

using namespace fleetcore;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    qRegisterMetaType<fleetcore::LockResponse>("fleetcore::LockResponse");

    MaintWindow window;
    window.show();

    QThread     thread;
    MaintWorker worker("127.0.0.1", GATEWAY_PORT);
    worker.moveToThread(&thread);

    // GUI -> worker: the button press crosses into the worker's thread.
    QObject::connect(&window, &MaintWindow::lockRequested,
                     &worker, &MaintWorker::sendLock);

    // worker -> GUI: acknowledgements and status come back.
    QObject::connect(&worker, &MaintWorker::ackReceived,
                     &window, &MaintWindow::onAckReceived);
    QObject::connect(&worker, &MaintWorker::linkStateChanged,
                     &window, &MaintWindow::onLinkStateChanged);
    QObject::connect(&worker, &MaintWorker::logLine,
                     &window, &MaintWindow::onLogLine);

    QObject::connect(&thread, &QThread::started, &worker, &MaintWorker::start);

    QObject::connect(&app, &QApplication::aboutToQuit, [&worker, &thread]() {
        QMetaObject::invokeMethod(&worker, "stop", Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    });

    thread.start();
    return app.exec();
}