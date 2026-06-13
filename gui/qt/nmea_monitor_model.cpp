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
 * Implement nmea_monitor_model.h.
 */

#include "nmea_monitor_model.h"

#include <memory>

#include <QTime>

#include "model/comm_navmsg.h"
#include "model/data_monitor_src.h"

namespace ocpn::qtui {

namespace {
// A readable one-liner for a decoded message: the raw NMEA-0183 sentence
// where available, else the message's own stringification (PGN etc.).
QString formatMsg(const std::shared_ptr<const NavMsg>& msg) {
  if (!msg) return QString();
  QString text;
  if (auto n0183 = std::dynamic_pointer_cast<const Nmea0183Msg>(msg))
    text = QString::fromStdString(n0183->payload).trimmed();
  else
    text = QString::fromStdString(msg->to_string()).trimmed();
  return QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) +
         QStringLiteral("  ") + text;
}

// A stable source tag for filtering: the bus (N0183 / N2000 / SignalK) plus
// the interface (address:port for network drivers).
QString sourceOf(const std::shared_ptr<const NavMsg>& msg) {
  if (!msg || !msg->source) return QStringLiteral("(unknown)");
  const QString bus = QString::fromStdString(
      NavAddr::BusToString(msg->source->bus));
  const QString iface = QString::fromStdString(msg->source->iface).trimmed();
  return iface.isEmpty() ? bus : bus + QStringLiteral(" ") + iface;
}
}  // namespace

NmeaMonitorModel::NmeaMonitorModel(QObject* parent) : QObject(parent) {
  // The sink runs on the GUI thread (observable_qt delivery); emit straight
  // through to QML.
  m_src = std::make_unique<DataMonitorSrc>(
      [this](const std::shared_ptr<const NavMsg>& msg) {
        const QString source = sourceOf(msg);
        // Track sources even while paused, so the filter list stays complete.
        if (!m_sources.contains(source)) {
          m_sources.append(source);
          m_sources.sort();
          emit sourcesChanged();
        }
        if (m_paused) return;
        const QString line = formatMsg(msg);
        if (!line.isEmpty()) emit lineReceived(line, source);
      });
}

NmeaMonitorModel::~NmeaMonitorModel() = default;

void NmeaMonitorModel::setPaused(bool on) {
  if (on == m_paused) return;
  m_paused = on;
  emit pausedChanged();
}

}  // namespace ocpn::qtui
