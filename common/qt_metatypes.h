#pragma once

// Declares the wire structs to Qt's type system, so they can be copied into
// the event queue when a signal crosses a thread boundary.
//
// A separate header because Q_DECLARE_METATYPE is a template specialisation:
// it must be seen before anything instantiates QMetaTypeId for that type.
// Putting it in whichever header happens to need it first works until a
// second header needs it too, and then the order decides whether it compiles.
//
// proto.h stays free of Qt: gateway, locator and termd do not link it.

#include "common/proto.h"

#include <QMetaType>

Q_DECLARE_METATYPE(fleetcore::LockRequest)
Q_DECLARE_METATYPE(fleetcore::LockResponse)