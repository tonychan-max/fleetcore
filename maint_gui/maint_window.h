#pragma once

#include "common/qt_metatypes.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;

namespace fleetcore {

class MaintWindow : public QWidget {
    Q_OBJECT

public:
    explicit MaintWindow(QWidget* parent = nullptr);

signals:
    // Emitted when the operator presses Lock. The worker picks this up in
    // its own thread.
    void lockRequested(const QString& termNo);

public slots:
    void onAckReceived(const fleetcore::LockResponse& rsp);
    void onLinkStateChanged(bool connected);
    void onLogLine(const QString& text);

private:
    QComboBox*      term_select_ = nullptr;
    QPushButton*    lock_button_ = nullptr;
    QPlainTextEdit* log_view_    = nullptr;
    QLabel*         link_label_  = nullptr;
};

}  // namespace fleetcore
