/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * OwnShipHolder -- a mutex-guarded OwnShipState, written by the nav-feed
 * worker thread and read by the GUI thread (P2.11). The AIS targets get the
 * AisTargetStore; own ship is a single value, so a tiny locked holder is the
 * thread-safe equivalent.
 */

#ifndef OCPN_QT_OWN_SHIP_HOLDER_H_
#define OCPN_QT_OWN_SHIP_HOLDER_H_

#include <QMutex>

#include "nav_data.h"

namespace ocpn::qtui {

class OwnShipHolder {
public:
  void set(const OwnShipState& s) {
    QMutexLocker lock(&m_mutex);
    m_state = s;
  }
  OwnShipState get() const {
    QMutexLocker lock(&m_mutex);
    return m_state;
  }

private:
  OwnShipState m_state;
  mutable QMutex m_mutex;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_OWN_SHIP_HOLDER_H_
