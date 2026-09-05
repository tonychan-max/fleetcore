#pragma once

#include "common/qt_metatypes.h"

#include <QWidget>

class QLabel;

namespace fleetcore {

// ============================================================
// The terminal's lock screen.
//
// Knows nothing about sockets or message queues. It is handed a decoded
// LockRequest and shows it; where that came from is the caller's problem.
//
// That separation is what makes step 3 easy: the same slot works whether
// the message arrives from a test button, a worker thread, or -- in
// Phase 2 -- a separate process over QLocalSocket (ADR-0001).
// ============================================================

class LockWindow : public QWidget {
    Q_OBJECT   // required for signals and slots; see AUTOMOC in CMakeLists

public:
    explicit LockWindow(QWidget* parent = nullptr);

public slots:
    // Called when a lock command arrives.
    void onLockReceived(const fleetcore::LockRequest& req);

    // Called when the link to termd goes up or down.
    void onLinkStateChanged(bool connected);

private:
    void applyLockedStyle();
    void applyUnlockedStyle();

    QLabel* state_label_  = nullptr;   // LOCKED / UNLOCKED
    QLabel* detail_label_ = nullptr;   // terminal number, sequence, time
    QLabel* link_label_   = nullptr;   // connection status
};

}  // namespace fleetcore