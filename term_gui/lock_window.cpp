#include "term_gui/lock_window.h"

#include <QDateTime>
#include <QLabel>
#include <QVBoxLayout>

namespace fleetcore {

LockWindow::LockWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("fleetcore terminal");
    resize(600, 400);

    state_label_ = new QLabel(this);
    state_label_->setAlignment(Qt::AlignCenter);

    detail_label_ = new QLabel(this);
    detail_label_->setAlignment(Qt::AlignCenter);

    link_label_ = new QLabel(this);
    link_label_->setAlignment(Qt::AlignCenter);

    // A layout positions the widgets and resizes them with the window.
    // Fixed coordinates would break the moment the screen size changes,
    // and vehicle terminals come in several sizes.
    auto* layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(state_label_);
    layout->addWidget(detail_label_);
    layout->addStretch();
    layout->addWidget(link_label_);

    applyUnlockedStyle();
    onLinkStateChanged(false);
}

void LockWindow::onLockReceived(const fleetcore::LockRequest& req) {
    applyLockedStyle();

    const QDateTime ts =
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(req.header.timestamp));

    // term_no is a fixed-length field, not a C string: it has no terminator.
    // QString::fromLatin1 with an explicit length is the safe way to read it.
    const QString term = QString::fromLatin1(req.term_no, sizeof(req.term_no));

    detail_label_->setText(
        QString("terminal %1   seq %2\n%3")
            .arg(term)
            .arg(req.header.seq)
            .arg(ts.toString("yyyy-MM-dd HH:mm:ss.zzz")));
}

void LockWindow::onLinkStateChanged(bool connected) {
    link_label_->setText(connected ? "link: connected" : "link: disconnected");
    link_label_->setStyleSheet(
        connected ? "color: #7f7; font-size: 14px; padding: 8px;"
                  : "color: #f77; font-size: 14px; padding: 8px;");
}

void LockWindow::applyLockedStyle() {
    setStyleSheet("background-color: #7a1111;");
    state_label_->setText("LOCKED");
    state_label_->setStyleSheet(
        "color: white; font-size: 72px; font-weight: bold;");
    detail_label_->setStyleSheet("color: #fcc; font-size: 18px;");
}

void LockWindow::applyUnlockedStyle() {
    setStyleSheet("background-color: #2b2b2b;");
    state_label_->setText("UNLOCKED");
    state_label_->setStyleSheet("color: #888; font-size: 72px;");
    detail_label_->setText("waiting for a command");
    detail_label_->setStyleSheet("color: #666; font-size: 18px;");
}

}  // namespace fleetcore