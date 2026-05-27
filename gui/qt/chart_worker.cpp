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
#include <QFileInfo>

#include "chart_catalog_db.h"
#include "ocharts_service.h"
#include "osenc_reader.h"
#include "s52_engine.h"
#include "senc_cache.h"

namespace ocpn::qtui {

namespace {
// Chart cell formats handled by the worker.
enum class CellKind { Enc000, SencPlain, Ocharts };
CellKind kindOf(const QString& path) {
  const QString ext = QFileInfo(path).suffix().toLower();
  if (ext == QStringLiteral("oesu") || ext == QStringLiteral("oesenc"))
    return CellKind::Ocharts;  // encrypted -> oexserverd
  if (ext == QStringLiteral("s57")) return CellKind::SencPlain;  // plaintext OSENC
  return CellKind::Enc000;  // raw S-57 .000 via OGR
}

// Build a catalog entry from an OSENC header + the cell file path.
CellExtent extentFromOsenc(const OsencHeader& h, const QString& path) {
  CellExtent ce;
  ce.path = path;
  ce.name = QFileInfo(path).completeBaseName();
  ce.north = h.north; ce.south = h.south; ce.east = h.east; ce.west = h.west;
  ce.nativeScale = h.nativeScale;
  ce.band = CellExtent::bandFromName(ce.name);
  ce.navFeatures = 1;  // OSENC headers don't carry a feature count; include it
  return ce;
}
}  // namespace

ChartWorker::ChartWorker(S52Engine* engine, QString s57data_dir,
                         QObject* parent)
    : QObject(parent), m_engine(engine), m_s57data_dir(std::move(s57data_dir)) {}

ChartWorker::~ChartWorker() = default;

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
  int n_cached = 0;
  // Persistent catalog cache: a hit skips the (expensive) per-cell scan.
  ChartCatalogCache cache;
  for (const QString& path : paths_000) {
    const qint64 mtime = QFileInfo(path).lastModified().toSecsSinceEpoch();
    CellExtent ce;
    if (cache.get(path, mtime, ce)) {
      ++n_cached;
      cells.push_back(ce);
      if (++since_emit >= kBatch) {
        Q_EMIT extentsScanned(cells);
        since_emit = 0;
      }
      continue;  // cache hit -- no scan/decrypt
    }
    switch (kindOf(path)) {
      case CellKind::Ocharts: {
        // Decrypt just the header (cheap) for the extent + native scale.
        bool ok = false;
        const QByteArray hdr =
            OChartsService::instance().decryptCellHeader(path, ok);
        OsencHeader h;
        if (ok && scanOsencHeaderBytes(hdr, h)) ce = extentFromOsenc(h, path);
        break;
      }
      case CellKind::SencPlain: {
        OsencHeader h;
        if (scanOsencHeaderFile(path, h)) ce = extentFromOsenc(h, path);
        break;
      }
      case CellKind::Enc000:
        ce = m_engine->scanOneCellExtent(path, m_s57data_dir);
        break;
    }
    if (ce.valid()) {
      cache.put(ce, mtime);  // remember so next launch is instant
      cells.push_back(ce);
    }
    if (++since_emit >= kBatch) {
      Q_EMIT extentsScanned(cells);
      since_emit = 0;
      qWarning("ChartWorker: scan progress %lld cells, %lld ms",
               (long long)cells.size(), (long long)timer.elapsed());
    }
  }
  Q_EMIT extentsScanned(cells);  // final (also covers the empty case)
  qWarning("ChartWorker: catalog scan %lld/%lld cells (%d cached) in %lld ms",
           (long long)cells.size(), (long long)paths_000.size(), n_cached,
           (long long)timer.elapsed());
}

void ChartWorker::loadCell(const CellExtent& cell) {
  if (!m_engine) return;
  double n = 0, s = 0, e = 0, w = 0;
  s52sg::Buffer buf;
  switch (kindOf(cell.path)) {
    case CellKind::Ocharts: {
      if (!m_senc_cache) m_senc_cache = std::make_unique<SencCache>();
      const qint64 mtime =
          QFileInfo(cell.path).lastModified().toSecsSinceEpoch();
      // Cache hit -> skip the (slow) daemon decrypt; just re-decode.
      QByteArray osenc = m_senc_cache->get(cell.path, mtime);
      if (osenc.isEmpty()) {
        bool ok = false;
        osenc = OChartsService::instance().decryptCell(cell.path, ok);
        if (ok && !osenc.isEmpty())
          m_senc_cache->put(cell.path, mtime, osenc);
      }
      if (!osenc.isEmpty()) buf = m_engine->decodeOsenc(osenc, &n, &s, &e, &w);
      break;
    }
    case CellKind::SencPlain:
      buf = m_engine->loadOsencCell(cell.path, &n, &s, &e, &w);
      break;
    case CellKind::Enc000:
      buf = m_engine->loadEncCell(cell.path, m_s57data_dir, &n, &s, &e, &w);
      break;
  }
  if (buf.empty()) return;
  Q_EMIT cellLoaded(cell.name, buf, n, s, e, w);
}

void ChartWorker::setColorScheme(int scheme) {
  if (m_engine) m_engine->setColorScheme(scheme);
}

}  // namespace ocpn::qtui
