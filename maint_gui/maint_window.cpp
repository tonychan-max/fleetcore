#include "maint_gui/maint_window.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace fleetcore {

MaintWindow::MaintWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("fleetcore maintenance");
    resize(520, 400);

    term_select_ = new QComboBox(this);
    // The list is hard-coded to match locator's table. Step 3 replaces both
    // with the shared-memory terminal table, at which point this is
    // populated from what is actually connected.
    term_select_->addItems({"1001", "1002"});

    lock_button_ = new QPushButton("LOCK", this);
    lock_button_->setMinimumHeight(48);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel("Terminal:", this));
    top->addWidget(term_select_);
    top->addStretch();

    log_view_ = new QPlainTextEdit(this);
    log_view_->setReadOnly(true);

    link_label_ = new QLabel(this);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(lock_button_);
    layout->addWidget(log_view_, 1);
    layout->addWidget(link_label_);

    connect(lock_button_, &QPushButton::clicked, this, [this]() {
        emit lockRequested(term_select_->currentText());
    });

    onLinkStateChanged(false);
}

void MaintWindow::onAckReceived(const fleetcore::LockResponse& rsp) {
    const QString term = QString::fromLatin1(rsp.term_no, sizeof(rsp.term_no));
    onLogLine(QString("ACK        terminal %1  seq %2  result 0x%3")
                  .arg(term)
                  .arg(rsp.header.seq)
                  .arg(rsp.result, 2, 16, QChar('0')));
}

void MaintWindow::onLinkStateChanged(bool connected) {
    link_label_->setText(connected ? "link: connected" : "link: disconnected");
    link_label_->setStyleSheet(connected ? "color: green;" : "color: red;");
    lock_button_->setEnabled(connected);
}

void MaintWindow::onLogLine(const QString& text) {
    log_view_->appendPlainText(
        QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + "  " + text);
}

}  // namespace fleetcore