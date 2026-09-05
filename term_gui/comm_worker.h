#pragma once

#include "common/proto.h"
#include "common/stream_buffer.h"
#include "common/qt_metatypes.h"

#include <QObject>

class QTcpSocket;

namespace fleetcore {

// ============================================================
// Talks to termd over TCP. Lives in its own thread.
//
// The vehicle terminal is the client: it connects outward to termd, which
// waits. That matches how terminals behave in the field -- they come and go,
// the server stays put.
//
// This class knows nothing about the screen. It decodes bytes into
// LockRequest and emits a signal; Qt delivers that to the GUI thread's event
// queue, which is the same mechanism a message queue provides between
// processes -- an event is handed to the other side's loop rather than
// calling into it directly. That is why no mutex appears anywhere here.
// ============================================================

class CommWorker : public QObject {
    Q_OBJECT

public:
    explicit CommWorker(QString host, quint16 port, QObject* parent = nullptr);

public slots:
    // Entry point, invoked once the thread has started.
    void start();
    void stop();

signals:
    void lockReceived(const fleetcore::LockRequest& req);
    void linkStateChanged(bool connected);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void tryReconnect();

private:
    void handleMessage(const uint8_t* buf, std::size_t len);

    QString      host_;
    quint16      port_;
    QTcpSocket*  socket_ = nullptr;
    StreamBuffer rx_;
};

}  // namespace fleetcore
