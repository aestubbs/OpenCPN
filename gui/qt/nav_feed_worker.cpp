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
 * Implement nav_feed_worker.h.
 */

#include "nav_feed_worker.h"

#include <cmath>

#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "ais_target_store.h"
#include "model/ais_decoder.h"
#include "model/ais_target_data.h"
#include "own_ship_holder.h"

namespace ocpn::qtui {

namespace {
constexpr int kLinesPerTick = 40;          // feed rate -> animation speed
constexpr qint64 kStaleMs = 10 * 60 * 1000;  // drop targets unseen 10 min

inline bool finitePos(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90.0 &&
         std::abs(lon) <= 360.0 && !(lat == 0.0 && lon == 0.0);
}

double nmeaCoord(const QString& field, const QString& hemi) {
  bool ok = false;
  const double v = field.toDouble(&ok);
  if (!ok) return 0.0;
  const double deg = std::floor(v / 100.0);
  double dd = deg + (v - deg * 100.0) / 60.0;
  if (hemi == QLatin1String("S") || hemi == QLatin1String("W")) dd = -dd;
  return dd;
}
}  // namespace

NavFeedWorker::NavFeedWorker(AisTargetStore* store, OwnShipHolder* own,
                             const QString& log_path, QObject* parent)
    : QObject(parent), m_store(store), m_own(own) {
  QFile f(log_path);
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream ts(&f);
    while (!ts.atEnd()) {
      const QString line = ts.readLine().trimmed();
      if (!line.isEmpty()) m_lines.append(line);
    }
  } else {
    qWarning("NavFeedWorker: cannot open %s", qPrintable(log_path));
  }
}

void NavFeedWorker::start() {
  if (!m_timer) {
    m_timer = new QTimer(this);  // created on the worker thread
    m_timer->setInterval(100);   // 10 ticks/sec
    connect(m_timer, &QTimer::timeout, this, &NavFeedWorker::tick);
  }
  if (hasData()) m_timer->start();
}

void NavFeedWorker::stop() {
  if (m_timer) m_timer->stop();
}

void NavFeedWorker::decodeLine(const QString& line) {
  if (line.contains(QLatin1String("VDM")) ||
      line.contains(QLatin1String("VDO"))) {
    if (g_pAIS) g_pAIS->DecodeN0183(line);
  } else if (line.contains(QLatin1String("RMC"))) {
    // $..RMC,time,status,lat,N/S,lon,E/W,sog,cog,date,... -> own-ship state.
    // Accumulated locally (no model globals from this thread); published to
    // the thread-safe OwnShipHolder once per tick.
    const QStringList fld = line.split(QLatin1Char(','));
    if (fld.size() >= 9 && fld[2] == QLatin1String("A")) {
      m_own_state.valid = true;
      m_own_state.lat = nmeaCoord(fld[3], fld[4]);
      m_own_state.lon = nmeaCoord(fld[5], fld[6]);
      bool ok = false;
      const double sog = fld[7].toDouble(&ok);
      if (ok) m_own_state.sog = sog;
      const double cog = fld[8].toDouble(&ok);
      if (ok) {
        m_own_state.cog = cog;
        m_own_state.hdg = kHeadingUnavailable;  // RMC has no heading
      }
    }
  }
}

void NavFeedWorker::tick() {
  if (m_lines.isEmpty()) return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  // Decode this tick's batch of sentences (the CPU work, off the GUI thread).
  for (int i = 0; i < kLinesPerTick; ++i) {
    if (m_pos >= m_lines.size()) m_pos = 0;  // loop the log
    decodeLine(m_lines[m_pos++]);
  }

  // Mirror the decoder's current targets into the store (stamped now), prune.
  if (g_pAIS && m_store) {
    for (const auto& [mmsi, td] : g_pAIS->GetTargetList()) {
      if (!td || td->b_lost || td->b_removed) continue;
      if (!finitePos(td->Lat, td->Lon)) continue;
      AisTarget t;
      t.mmsi = td->MMSI;
      t.lat = td->Lat;
      t.lon = td->Lon;
      t.cog = std::isfinite(td->COG) ? td->COG : 0.0;
      t.sog = std::isfinite(td->SOG) ? td->SOG : 0.0;
      t.hdg = td->HDG;
      t.name = td->GetFullName().trimmed();
      m_store->upsert(t, now);
    }
    m_store->prune(now, kStaleMs);
  }

  // Publish own ship to the thread-safe holder (the GUI thread mirrors it to
  // the model globals for ActiveTrack -- no two threads touch gLat).
  if (m_own && m_own_state.valid && finitePos(m_own_state.lat, m_own_state.lon))
    m_own->set(m_own_state);

  emit updated();  // one coalesced wake-up per tick
}

}  // namespace ocpn::qtui
