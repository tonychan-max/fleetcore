#include "maint_gui/maint_worker.h"

#include "common/wire.h"

#include <QTcpSocket>
#include <QTimer>

namespace fleetcore {

namespace {
constexpr int RECONNECT_MS = 2000;
}

MaintWorker::MaintWorker(QString host, quint16 port, QObject* parent)
    : QObject(parent), host_(std::move(host)), port_(port) {}

void MaintWorker::start() {
    socket_ = new QTcpSocket(this);

    connect(socket_, &QTcpSocket::connected,    this, &MaintWorker::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &MaintWorker::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &MaintWorker::onReadyRead);

    tryReconnect();
}

void MaintWorker::stop() {
    if (socket_) socket_->abort();
}

void MaintWorker::tryReconnect() {
    if (!socket_ || socket_->state() != QAbstractSocket::UnconnectedState) return;
    emit logLine(QString("connecting to %1:%2").arg(host_).arg(port_));
    socket_->connectToHost(host_, port_);
}

void MaintWorker::onConnected() {
    rx_.reset();
    emit logLine("connected");
    emit linkStateChanged(true);
}

void MaintWorker::onDisconnected() {
    rx_.reset();
    emit logLine("disconnected");
    emit linkStateChanged(false);
    QTimer::singleShot(RECONNECT_MS, this, &MaintWorker::tryReconnect);
}

void MaintWorker::sendLock(const QString& termNo) {
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) {
        emit logLine("not connected -- command dropped");
        return;
    }

    const QByteArray raw = termNo.toLatin1();
    if (raw.size() != 4) {
        emit logLine("terminal number must be exactly 4 characters");
        return;
    }

    const LockRequest req = make_lock_request(raw.constData(), next_seq_);

    uint8_t buf[MAX_MESSAGE_LEN];
    const std::size_t n = pack_lock_request(req, buf, sizeof(buf));
    if (n == 0) {
        emit logLine("pack failed");
        return;
    }

    const qint64 written =
        socket_->write(reinterpret_cast<const char*>(buf), static_cast<qint64>(n));
    if (written != static_cast<qint64>(n)) {
        emit logLine("short write");
        return;
    }

    emit logLine(QString("LOCK sent  terminal %1  seq %2").arg(termNo).arg(next_seq_));
    ++next_seq_;
}

void MaintWorker::onReadyRead() {
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

void MaintWorker::handleMessage(const uint8_t* buf, std::size_t len) {
    Header h{};
    const WireError e = unpack_header(buf, len, h);
    if (e != WireError::Ok) {
        emit logLine(QString("STREAM %1 -- dropping connection").arg(to_string(e)));
        rx_.reset();
        socket_->abort();
        return;
    }

    if (h.code != CODE_LOCK_RESPONSE) {
        emit logLine(QString("code 0x%1 not handled")
                         .arg(h.code, 2, 16, QChar('0')));
        return;
    }

    LockResponse rsp{};
    const WireError e2 = unpack_lock_response(buf, len, rsp);
    if (e2 != WireError::Ok) {
        emit logLine(QString("MESSAGE %1").arg(to_string(e2)));
        return;
    }

    emit ackReceived(rsp);
}

}  // namespace fleetcore