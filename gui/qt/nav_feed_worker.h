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
 * NavFeedWorker -- runs the AIS decode off the GUI thread (P2.11 worker
 * thread). Lives on its own QThread; on a timer it pulls the next batch of
 * NMEA sentences (currently a replayed log -- swap for a real CommDriver
 * later), decodes AIS through the real AisDecoder, mirrors the decoder's
 * targets into the thread-safe AisTargetStore (with a last-seen stamp + a
 * staleness prune), and updates the OwnShipHolder from RMC.
 *
 * It emits a single coalesced updated() per tick (NOT per sentence) which is
 * delivered to the GUI thread by a queued connection; the GUI thread then
 * snapshots the store and re-renders. So the per-sentence CPU work is off the
 * main thread and the main thread does only the lightweight snapshot.
 */

#ifndef OCPN_QT_NAV_FEED_WORKER_H_
#define OCPN_QT_NAV_FEED_WORKER_H_

#include <QObject>
#include <QStringList>

#include "nav_data.h"

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class AisTargetStore;
class OwnShipHolder;

class NavFeedWorker : public QObject {
  Q_OBJECT

public:
  // `store` / `own` are owned by the provider and outlive this worker; both
  // are thread-safe. `log_path` is the NMEA log to replay.
  NavFeedWorker(AisTargetStore* store, OwnShipHolder* own,
                const QString& log_path, QObject* parent = nullptr);

  bool hasData() const { return !m_lines.isEmpty(); }

public slots:
  void start();  // create + start the timer (invoked on the worker thread)
  void stop();

signals:
  void updated();  // coalesced, one per tick; queued to the GUI thread

private:
  void tick();
  void decodeLine(const QString& line);

  AisTargetStore* m_store;
  OwnShipHolder* m_own;
  OwnShipState m_own_state;  // accumulated from RMC; published to m_own per tick
  QStringList m_lines;
  int m_pos = 0;
  QTimer* m_timer = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NAV_FEED_WORKER_H_
