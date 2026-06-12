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

#include <QDir>
#include <QRegularExpression>

#include "cm93_cell_reader.h"
#include "cm93_dictionary.h"
#include "cm93_scanner.h"
#include "raster_chart.h"

#include <QCoreApplication>
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
enum class CellKind { Enc000, SencPlain, Ocharts, Cm93, Raster };
// CM93 cell files are ?lllnnnn.X with X a scale letter (P2.19).
const QRegularExpression kCm93CellRe(
    QStringLiteral("^.\\d{7}\\.[ZABCDEFGzabcdefg]$"));
CellKind kindOf(const QString& path) {
  if (path.endsWith(QStringLiteral(".kap"), Qt::CaseInsensitive))
    return CellKind::Raster;
  if (kCm93CellRe.match(QFileInfo(path).fileName()).hasMatch())
    return CellKind::Cm93;
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
  // M_COVR coverage: the quilt selects -- and P2.17 clips -- against the
  // cell's TRUE charted area instead of its (often far larger) bbox.
  ce.coverage = h.coverage;
  return ce;
}
}  // namespace

ChartWorker::ChartWorker(S52Engine* engine, QString s57data_dir,
                         QObject* parent)
    : QObject(parent), m_engine(engine), m_s57data_dir(std::move(s57data_dir)) {}

ChartWorker::~ChartWorker() = default;

void ChartWorker::scanExtents(const QStringList& paths_000) {
  if (!m_engine) return;
  // The scan pumps the worker's event queue between cells (see below), which
  // can re-deliver a queued scanExtents; ignore that re-entry so scans never
  // nest. A genuinely-needed re-scan is re-triggered by the next dir change.
  if (m_scanning) return;
  m_scanning = true;
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
    // A directory entry is a CM93 set root (P2.19): expand it in place via
    // the CM93 scanner (cheap header-only reads, no per-cell cache).
    if (QFileInfo(path).isDir()) {
      if (Cm93Scanner::isCm93Root(path)) {
        const QList<CellExtent> cm93 = Cm93Scanner::scan(path);
        cells.append(cm93);
        Q_EMIT extentsScanned(cells);
        qWarning("ChartWorker: CM93 set %s -> %lld cells",
                 path.toUtf8().constData(), (long long)cm93.size());
      }
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      continue;
    }
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
      case CellKind::Raster: {
        const RasterChart rc = RasterChartReader::scanHeader(path);
        if (rc.ok) {
          ce.name = QStringLiteral("KAP-") + QFileInfo(path).completeBaseName();
          ce.path = path;
          ce.north = rc.north;
          ce.south = rc.south;
          ce.east = rc.east;
          ce.west = rc.west;
          ce.nativeScale = rc.nativeScale;
          ce.navFeatures = 1;  // raster content is always "chart surface"
          ce.band = rc.nativeScale > 1000000   ? 1
                    : rc.nativeScale > 300000  ? 2
                    : rc.nativeScale > 90000   ? 3
                    : rc.nativeScale > 30000   ? 4
                    : rc.nativeScale > 10000   ? 5
                                               : 6;
        } else {
          qWarning("scan: KAP rejected %s: %s", qPrintable(path),
                   qPrintable(rc.error));
        }
        break;
      }
      case CellKind::Cm93:
        // CM93 cells arrive pre-scanned (the root expansion below feeds
        // ready extents); a stray single cell gets a header-only read.
        if (Cm93CellReader::readHeaderExtent(path, &ce.south, &ce.north,
                                             &ce.west, &ce.east)) {
          if (ce.west > 180.0) ce.west -= 360.0;
          if (ce.east > 180.0) ce.east -= 360.0;
          ce.name = QStringLiteral("CM93-") + QFileInfo(path).fileName();
          ce.path = path;
          ce.nativeScale =
              Cm93Scanner::scaleForChar(path.back());
          ce.band = Cm93Scanner::bandForChar(path.back());
        }
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
    // Service queued work (notably loadCell for the cells the user is looking
    // at) between cells, so a long o-charts decrypt scan doesn't block chart
    // rendering for minutes. This thread's event loop is otherwise stuck
    // inside this slot until the whole scan returns. Cheap when nothing is
    // queued; a queued loadCell runs to completion here, then the scan resumes.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }
  Q_EMIT extentsScanned(cells);  // final (also covers the empty case)
  qWarning("ChartWorker: catalog scan %lld/%lld cells (%d cached) in %lld ms",
           (long long)cells.size(), (long long)paths_000.size(), n_cached,
           (long long)timer.elapsed());
  m_scanning = false;
}

void ChartWorker::loadRasterCell_(const CellExtent& cell) {
  const RasterChart rc = RasterChartReader::load(cell.path);
  if (!rc.ok) {
    qWarning("loadCell: KAP decode failed %s: %s", qPrintable(cell.path),
             qPrintable(rc.error));
    return;
  }
  qWarning("loadCell: KAP %s %dx%d scale=%d", qPrintable(cell.name),
           rc.image.width(), rc.image.height(), rc.nativeScale);
  Q_EMIT rasterCellLoaded(cell.name, rc.image, rc.north, rc.south, rc.east,
                          rc.west, rc.worldYTop, rc.worldYBottom);
}

void ChartWorker::loadCell(const CellExtent& cell) {
  if (!m_engine) return;
  if (kindOf(cell.path) == CellKind::Raster) {
    loadRasterCell_(cell);
    return;
  }
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
      if (!osenc.isEmpty())
        buf = m_engine->decodeOsenc(osenc, &n, &s, &e, &w, cell.nativeScale);
      break;
    }
    case CellKind::SencPlain:
      buf = m_engine->loadOsencCell(cell.path, &n, &s, &e, &w, cell.nativeScale);
      break;
    case CellKind::Enc000:
      buf = m_engine->loadEncCell(cell.path, m_s57data_dir, &n, &s, &e, &w);
      break;
    case CellKind::Cm93: {
      // Per-root dictionary cache: walk up from the cell to the set root.
      QString root = QFileInfo(cell.path).absolutePath();
      for (int up = 0; up < 4 && !root.isEmpty(); ++up) {
        if (m_cm93_dicts.contains(root) || Cm93Scanner::isCm93Root(root))
          break;
        root = QFileInfo(root).absolutePath();
      }
      if (!m_cm93_dicts.contains(root)) {
        auto dict = std::make_shared<Cm93Dictionary>();
        if (!dict->load(root)) {
          // The dictionary may live in a subdir (CM93SYS etc.).
          for (const QString& sub :
               QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (dict->load(root + QDir::separator() + sub)) break;
          }
        }
        m_cm93_dicts.insert(root, dict);
      }
      const auto dict = m_cm93_dicts.value(root);
      if (dict && dict->isOk())
        buf = m_engine->loadCm93Cell(cell.path, dict.get(), &n, &s, &e, &w);
      break;
    }
  }
  if (buf.empty()) {
    // A needed cell that decodes to nothing leaves its boundary rectangle on
    // screen with no content -- make that visible rather than silent.
    qWarning("loadCell: EMPTY decode name=%s kind=%d cov=%lld path=%s",
             qPrintable(cell.name), static_cast<int>(kindOf(cell.path)),
             static_cast<long long>(cell.coverage.size()),
             qPrintable(cell.path));
    return;
  }
  int n_snd = 0, sc_min = 2000000000, sc_max = 0;
  for (const s52sg::Label& l : buf.labels)
    if (l.isSounding) {
      ++n_snd;
      if (l.scamin < sc_min) sc_min = l.scamin;
      if (l.scamin > sc_max) sc_max = l.scamin;
    }
  qWarning(
      "loadCell: name=%s kind=%d scale=%d cov=%lld prims=%lld labels=%lld "
      "soundings=%d sndScamin=[%d..%d] query=%lld",
      qPrintable(cell.name), static_cast<int>(kindOf(cell.path)),
      cell.nativeScale, static_cast<long long>(cell.coverage.size()),
      static_cast<long long>(buf.prims.size()),
      static_cast<long long>(buf.labels.size()), n_snd,
      n_snd ? sc_min : 0, sc_max, static_cast<long long>(buf.queryObjects.size()));
  Q_EMIT cellLoaded(cell.name, buf, n, s, e, w);
}

void ChartWorker::setColorScheme(int scheme) {
  if (m_engine) m_engine->setColorScheme(scheme);
}

void ChartWorker::applyDisplaySettings(const ChartDisplaySettings& settings) {
  if (m_engine) m_engine->applyDisplaySettings(settings);
}

}  // namespace ocpn::qtui
