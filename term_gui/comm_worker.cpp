#include "term_gui/comm_worker.h"

#include "common/wire.h"

#include <QTcpSocket>
#include <QTimer>

#include <cstdio>

namespace fleetcore {

namespace {

// How long to wait before dialling termd again. A terminal that cannot
// reach the server should keep trying rather than give up: it may be out
// of coverage, or the server may be restarting.
constexpr int RECONNECT_MS = 2000;

}  // namespace

CommWorker::CommWorker(QString host, quint16 port, QObject* parent)
    : QObject(parent), host_(std::move(host)), port_(port) {}

void CommWorker::start() {
    // Created here, not in the constructor: the object is constructed in the
    // GUI thread and then moved, so anything owning a socket descriptor must
    // come into being after the move, in the thread that will use it.
    socket_ = new QTcpSocket(this);

    connect(socket_, &QTcpSocket::connected,    this, &CommWorker::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &CommWorker::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &CommWorker::onReadyRead);

    tryReconnect();
}

void CommWorker::stop() {
    if (socket_) socket_->abort();
}

void CommWorker::tryReconnect() {
    if (!socket_ || socket_->state() != QAbstractSocket::UnconnectedState) return;

    std::fprintf(stderr, "[comm] connecting to %s:%u\n",
                 host_.toStdString().c_str(), port_);
    socket_->connectToHost(host_, port_);
}

void CommWorker::onConnected() {
    std::fprintf(stderr, "[comm] connected\n");
    rx_.reset();
    emit linkStateChanged(true);
}

void CommWorker::onDisconnected() {
    std::fprintf(stderr, "[comm] disconnected\n");
    rx_.reset();
    emit linkStateChanged(false);

    // Single-shot timer rather than a loop: this thread must stay in its
    // event loop to keep receiving socket signals.
    QTimer::singleShot(RECONNECT_MS, this, &CommWorker::tryReconnect);
}

void CommWorker::onReadyRead() {
    const QByteArray chunk = socket_->readAll();
    rx_.append(reinterpret_cast<const uint8_t*>(chunk.constData()),
               static_cast<std::size_t>(chunk.size()));

    const uint8_t* msg = nullptr;
    std::size_t    len = 0;
    while (rx_.next(&msg, &len)) {
        handleMessage(msg, len);
        rx_.consume(len);
    }
}

void CommWorker::handleMessage(const uint8_t* buf, std::size_t len) {
    Header h{};
    const WireError e = unpack_header(buf, len, h);
    if (e != WireError::Ok) {
        std::fprintf(stderr, "[comm] STREAM %s -- dropping connection\n",
                     to_string(e));
        rx_.reset();
        socket_->abort();   // triggers onDisconnected, which schedules a retry
        return;
    }

    if (h.code != CODE_LOCK_REQUEST) {
        // Known length, unknown code: skip it. Without the length field this
        // would force a disconnect.
        std::fprintf(stderr, "[comm] MESSAGE code 0x%02X not handled\n", h.code);
        return;
    }

    LockRequest req{};
    const WireError e2 = unpack_lock_request(buf, len, req);
    if (e2 != WireError::Ok) {
        std::fprintf(stderr, "[comm] MESSAGE %s\n", to_string(e2));
        return;
    }

    std::fprintf(stderr, "[comm] LOCK term_no=%.4s seq=%u\n",
                 req.term_no, req.header.seq);

    emit lockReceived(req);
}

}  // namespace fleetcore