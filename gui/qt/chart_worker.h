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

#include <memory>

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "chart_extent.h"
#include "s52_engine.h"  // ChartDisplaySettings
#include "s52_sg.h"

Q_DECLARE_METATYPE(s52sg::Buffer)
Q_DECLARE_METATYPE(ocpn::qtui::ChartDisplaySettings)

namespace ocpn::qtui {

class Cm93Dictionary;
class DecodedCellCache;

class S52Engine;

class ChartWorker : public QObject {
  Q_OBJECT

public:
  // `engine` is created on the main thread but only ever touched from the
  // worker thread after construction (init() stays on main). Not owned.
  explicit ChartWorker(S52Engine* engine, QString s57data_dir,
                       QObject* parent = nullptr);
  ~ChartWorker() override;  // out-of-line for the unique_ptr<SencCache> pimpl

public slots:
  /** Open every cell, union feature envelopes (no decode), and emit the
   *  catalog. Cheap relative to a full load; safe to run for a whole set. */
  void scanExtents(const QStringList& paths_000);

  /** Decode one cell's full symbology into a world-coordinate buffer and
   *  emit it. Serialised against other loadCell calls on this thread. */
  void loadCell(const ocpn::qtui::CellExtent& cell);

public:
  /** Publish the set of cells a canvas still wants drawn. Thread-safe and
   *  called DIRECTLY from the UI thread (not a queued slot) so it updates the
   *  filter out of band: a loadCell that was queued for a cell the user has
   *  since panned past is dropped at dequeue WITHOUT the (expensive) decode,
   *  instead of grinding through a stale backlog one cell at a time.
   *
   *  `owner` identifies the calling canvas (its `this`). A cell is kept if ANY
   *  canvas wants it -- so a shared worker (split view, P6.1) never cancels one
   *  pane's decode because the other pane no longer needs it. Calling it at
   *  least once arms the filter; before that, nothing is skipped. */
  void setWanted(const void* owner, const QSet<QString>& names);

  /** Drop a canvas's wanted set when it goes away (shared worker, split view),
   *  so its last selection doesn't keep cells un-cancellable forever. */
  void forgetWanted(const void* owner);

signals:
  /** A decoded raster (KAP) chart: image + linear world rectangle
   *  (P2.7). Emitted alongside cellLoaded for vector cells. */
  void rasterCellLoaded(const QString& id, const QImage& image, double north,
                        double south, double east, double west,
                        double worldYTop, double worldYBottom);

private:
  void loadRasterCell_(const ocpn::qtui::CellExtent& cell);
  // True if `name` is still wanted (or the filter hasn't been armed yet).
  bool isWanted(const QString& name);

  // Resume the slots section my signal insertion above terminated --
  // setColorScheme/applyDisplaySettings are invoked by name from the
  // canvas and silently failed as plain publics.
public slots:

  /** Switch the S-52 colour scheme (0=day,1=dusk,2=night) on the decode
   *  thread, serialised against loadCell so it never races a decode. The
   *  canvas re-requests the loaded cells afterwards to re-emit with it. */
  void setColorScheme(int scheme);

  /** Apply the S-52 decode-time display settings (depth shading/contours,
   *  symbol/boundary style, important-text-only, SCAMIN) on the decode thread,
   *  serialised against loadCell. The canvas re-requests loaded cells after. */
  void applyDisplaySettings(const ocpn::qtui::ChartDisplaySettings& settings);

signals:
  void extentsScanned(const QList<ocpn::qtui::CellExtent>& cells);
  void cellLoaded(const QString& id, const s52sg::Buffer& buffer, double north,
                  double south, double east, double west);
  /** A requested cell will NOT arrive as a cellLoaded: either it decoded to
   *  nothing (`genuineEmpty` = true -- the canvas records it so it isn't
   *  re-requested in a tight loop) or the request was superseded/skipped
   *  before decoding (`genuineEmpty` = false -- eligible to retry). Lets the
   *  canvas clear its in-flight bookkeeping instead of leaking it forever. */
  void cellUnavailable(const QString& id, bool genuineEmpty);

private:
  S52Engine* m_engine;
  QString m_s57data_dir;
  // Decrypted-OSENC cache (o-charts), created lazily on the worker thread.
  std::unique_ptr<class SencCache> m_senc_cache;
  // Decoded-buffer LRU (all cell kinds): skips the expensive re-decode when the
  // quilt re-selects a cell on pan/zoom. Flushed on any display-setting or
  // colour-scheme change (those bake into the decode). Lazily created.
  std::unique_ptr<DecodedCellCache> m_decoded_cache;
  // True while scanExtents is running. The scan pumps the worker's event
  // queue between cells so queued loadCell requests are serviced during a long
  // (o-charts decrypt) scan instead of waiting for it to finish; this guards
  // against a re-entrant scanExtents arriving on that pump.
  bool m_scanning = false;
  // Per-CM93-root dictionary cache (P2.19): loaded once per set.
  QHash<QString, std::shared_ptr<ocpn::qtui::Cm93Dictionary>> m_cm93_dicts;

  // Cancellation filter (see setWanted): per-canvas wanted sets, unioned. A
  // cell is decoded if ANY canvas still wants it. Written by UI thread(s), read
  // by the worker thread, all under the mutex. Empty => filter not yet armed.
  QMutex m_wanted_mutex;
  QHash<const void*, QSet<QString>> m_wanted_by;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_WORKER_H_
