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
 * Implement nmea_log_replay.h.
 */

#include "nmea_log_replay.h"

#include <cmath>

#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "model/ais_decoder.h"
#include "model/own_ship.h"

namespace ocpn::qtui {

namespace {
constexpr int kLinesPerTick = 40;  // feed rate -> animation speed

// Parse an NMEA ddmm.mmmm / dddmm.mmmm coordinate into signed degrees.
double nmeaCoord(const QString& field, const QString& hemi) {
  bool ok = false;
  const double v = field.toDouble(&ok);
  if (!ok) return 0.0;
  const double deg = std::floor(v / 100.0);
  double dd = deg + (v - deg * 100.0) / 60.0;
  if (hemi == QLatin1String("S") || hemi == QLatin1String("W")) dd = -dd;
  return dd;
}

// Update the own-ship globals from an RMC sentence.
void handleRmc(const QStringList& f) {
  // $..RMC,time,status,lat,N/S,lon,E/W,sog,cog,date,...
  if (f.size() < 9 || f[2] != QLatin1String("A")) return;  // A = valid fix
  gLat = nmeaCoord(f[3], f[4]);
  gLon = nmeaCoord(f[5], f[6]);
  bool ok = false;
  const double sog = f[7].toDouble(&ok);
  if (ok) gSog = sog;
  const double cog = f[8].toDouble(&ok);
  if (ok) gCog = cog;
}
}  // namespace

NmeaLogReplay::NmeaLogReplay(const QString& log_path, QObject* parent)
    : QObject(parent) {
  QFile f(log_path);
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream ts(&f);
    while (!ts.atEnd()) {
      const QString line = ts.readLine().trimmed();
      if (!line.isEmpty()) m_lines.append(line);
    }
  } else {
    qWarning("NmeaLogReplay: cannot open %s", qPrintable(log_path));
  }
  m_timer = new QTimer(this);
  m_timer->setInterval(100);  // 10 batches/sec
  connect(m_timer, &QTimer::timeout, this, &NmeaLogReplay::tick);
}

void NmeaLogReplay::setRunning(bool run) {
  if (run && hasData())
    m_timer->start();
  else
    m_timer->stop();
}

void NmeaLogReplay::tick() {
  if (m_lines.isEmpty()) return;
  for (int i = 0; i < kLinesPerTick; ++i) {
    if (m_pos >= m_lines.size()) m_pos = 0;  // loop
    const QString& line = m_lines[m_pos++];
    // AIS sentences -> the real decoder (mirrors the comm-bus path's decode).
    if (line.contains(QLatin1String("VDM")) ||
        line.contains(QLatin1String("VDO"))) {
      if (g_pAIS) g_pAIS->DecodeN0183(line);
    } else if (line.contains(QLatin1String("RMC"))) {
      handleRmc(line.split(QLatin1Char(',')));
    }
  }
}

}  // namespace ocpn::qtui
