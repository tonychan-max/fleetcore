#pragma once

#include "common/proto.h"
#include "common/stream_buffer.h"
#include "common/qt_metatypes.h"

#include <QObject>

class QTcpSocket;

namespace fleetcore {

// ============================================================
// Talks to gateway over TCP, on behalf of the maintenance screen.
//
// Mirror image of the terminal's CommWorker: this one sends commands and
// receives acknowledgements, rather than the other way round. Same shape,
// opposite direction -- which is the point of ARCHITECTURE section 3-2:
// the pipeline is the same in both directions, only the roles differ.
// ============================================================

class MaintWorker : public QObject {
    Q_OBJECT

public:
    explicit MaintWorker(QString host, quint16 port, QObject* parent = nullptr);

public slots:
    void start();
    void stop();

    // Called from the GUI thread when the button is pressed. Because sender
    // and receiver are in different threads, Qt queues this like any other
    // cross-thread call -- the socket is only ever touched here.
    void sendLock(const QString& termNo);

signals:
    void ackReceived(const fleetcore::LockResponse& rsp);
    void linkStateChanged(bool connected);
    void logLine(const QString& text);

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
    uint16_t     next_seq_ = 1;
};

}  // namespace fleetcore

