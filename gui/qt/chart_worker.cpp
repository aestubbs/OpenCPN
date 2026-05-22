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

#include "s52_engine.h"

namespace ocpn::qtui {

ChartWorker::ChartWorker(S52Engine* engine, QString s57data_dir,
                         QObject* parent)
    : QObject(parent), m_engine(engine), m_s57data_dir(std::move(s57data_dir)) {}

void ChartWorker::scanExtents(const QStringList& paths_000) {
  if (!m_engine) return;
  const QList<CellExtent> cells =
      m_engine->scanCellExtents(paths_000, m_s57data_dir);
  Q_EMIT extentsScanned(cells);
}

void ChartWorker::loadCell(const CellExtent& cell) {
  if (!m_engine) return;
  double n = 0, s = 0, e = 0, w = 0;
  s52sg::Buffer buf =
      m_engine->loadEncCell(cell.path, m_s57data_dir, &n, &s, &e, &w);
  if (buf.empty()) return;
  Q_EMIT cellLoaded(cell.name, buf, n, s, e, w);
}

}  // namespace ocpn::qtui
