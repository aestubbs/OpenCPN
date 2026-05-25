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
 * Implement chart_worker.h.
 */

#include "chart_worker.h"

#include <QElapsedTimer>

#include "s52_engine.h"

namespace ocpn::qtui {

ChartWorker::ChartWorker(S52Engine* engine, QString s57data_dir,
                         QObject* parent)
    : QObject(parent), m_engine(engine), m_s57data_dir(std::move(s57data_dir)) {}

void ChartWorker::scanExtents(const QStringList& paths_000) {
  if (!m_engine) return;
  // Scan cell-by-cell and publish the growing catalog in batches, so the
  // boundary grid fills in progressively for a big set instead of after a
  // single long blocking scan. The accumulated list is re-emitted each time
  // (the consumer replaces its catalog wholesale).
  QElapsedTimer timer;
  timer.start();
  QList<CellExtent> cells;
  cells.reserve(paths_000.size());
  constexpr int kBatch = 25;
  int since_emit = 0;
  for (const QString& path : paths_000) {
    CellExtent ce = m_engine->scanOneCellExtent(path, m_s57data_dir);
    if (ce.valid()) cells.push_back(ce);
    if (++since_emit >= kBatch) {
      Q_EMIT extentsScanned(cells);
      since_emit = 0;
    }
  }
  Q_EMIT extentsScanned(cells);  // final (also covers the empty case)
  qWarning("ChartWorker: catalog scan %lld/%lld cells in %lld ms",
           (long long)cells.size(), (long long)paths_000.size(),
           (long long)timer.elapsed());
}

void ChartWorker::loadCell(const CellExtent& cell) {
  if (!m_engine) return;
  double n = 0, s = 0, e = 0, w = 0;
  s52sg::Buffer buf =
      m_engine->loadEncCell(cell.path, m_s57data_dir, &n, &s, &e, &w);
  if (buf.empty()) return;
  Q_EMIT cellLoaded(cell.name, buf, n, s, e, w);
}

void ChartWorker::setColorScheme(int scheme) {
  if (m_engine) m_engine->setColorScheme(scheme);
}

}  // namespace ocpn::qtui
