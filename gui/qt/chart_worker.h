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
 * ChartWorker -- runs the expensive chart I/O (extent scan + per-cell
 * symbology decode) on a dedicated worker thread so the UI thread never
 * blocks on chart loading.
 *
 * Lives on its own QThread (ChartCanvas moves it there). Its slots are
 * invoked across the thread boundary via queued connections; its results
 * come back to the main thread the same way, as `s52sg::Buffer` /
 * `CellExtent` value types (Qt-only, no scene-graph or GPU objects -- those
 * are built on the render thread from the buffer).
 *
 * The decode reaches into s52plib, which keeps global state (`ps52plib`),
 * so it is NOT re-entrant. A single ChartWorker on a single thread keeps
 * every decode serialised; queued `loadCell` requests drain one at a time.
 *
 * The extent scan (scanExtents) touches only the OGR S-57 driver, no
 * s52plib, so it is independent of that constraint -- but it runs on the
 * same worker so a flood of decode requests can't starve it and vice versa.
 */

#ifndef OCPN_QT_CHART_WORKER_H_
#define OCPN_QT_CHART_WORKER_H_

#include <QObject>
#include <QString>
#include <QStringList>

#include "chart_extent.h"
#include "s52_sg.h"

Q_DECLARE_METATYPE(s52sg::Buffer)

namespace ocpn::qtui {

class S52Engine;

class ChartWorker : public QObject {
  Q_OBJECT

public:
  // `engine` is created on the main thread but only ever touched from the
  // worker thread after construction (init() stays on main). Not owned.
  explicit ChartWorker(S52Engine* engine, QString s57data_dir,
                       QObject* parent = nullptr);

public Q_SLOTS:
  /** Open every cell, union feature envelopes (no decode), and emit the
   *  catalog. Cheap relative to a full load; safe to run for a whole set. */
  void scanExtents(const QStringList& paths_000);

  /** Decode one cell's full symbology into a world-coordinate buffer and
   *  emit it. Serialised against other loadCell calls on this thread. */
  void loadCell(const ocpn::qtui::CellExtent& cell);

Q_SIGNALS:
  void extentsScanned(const QList<ocpn::qtui::CellExtent>& cells);
  void cellLoaded(const QString& id, const s52sg::Buffer& buffer, double north,
                  double south, double east, double west);

private:
  S52Engine* m_engine;
  QString m_s57data_dir;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_WORKER_H_
