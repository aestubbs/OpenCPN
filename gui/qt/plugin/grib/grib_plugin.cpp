/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "grib_plugin.h"

#include <cmath>
#include <cstdlib>

#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QUrl>

#include "GribReader.h"  // vendored zyGrib decode core (grib_pi)
#include "GribRecord.h"
#include "grib_wind_layer.h"

GribContext::GribContext(QObject* timeline, QObject* parent)
    : QObject(parent), m_timeline(timeline) {
  // Restore the last GRIB on startup (wx parity): the timeline can
  // drive the weather immediately.
  const QString last =
      QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
          .value(QStringLiteral("lastFile"))
          .toString();
  if (!last.isEmpty() && QFile::exists(last))
    QMetaObject::invokeMethod(
        this, [this, last] { openFile(QUrl::fromLocalFile(last)); },
        Qt::QueuedConnection);
  // Follow the app time bar (wx parity: the GRIB rides the chart
  // timeline, not its own slider).
  if (m_timeline)
    connect(m_timeline, SIGNAL(timeChanged()), this,
            SLOT(onTimelineChanged()));
}

void GribContext::onTimelineChanged() {
  if (!m_timeline || m_step_times.isEmpty()) return;
  const QDateTime t =
      m_timeline->property("displayTime").toDateTime();
  if (!t.isValid()) return;
  const qint64 epoch = t.toSecsSinceEpoch();
  int best = 0;
  qint64 bestD = std::abs(m_step_times[0] - epoch);
  for (int i = 1; i < m_step_times.size(); ++i) {
    const qint64 d = std::abs(m_step_times[i] - epoch);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  setTimeIndex(best);
}

GribContext::~GribContext() { delete m_reader; }

void GribContext::openFile(const QUrl& url) {
  const QString path = url.toLocalFile();
  if (path.isEmpty()) return;
  delete m_reader;
  m_reader = new GribReader(wxString(path.toStdString()));
  m_steps.clear();
  m_step_times.clear();
  m_time_index = 0;
  if (!m_reader->isOk() || m_reader->getTotalNumberOfGribRecords() == 0) {
    m_status = tr("Not a readable GRIB file");
    m_file.clear();
    if (m_layer) m_layer->clearGrid();
    Q_EMIT gribChanged();
    return;
  }
  m_file = path.section('/', -1);
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
      .setValue(QStringLiteral("lastFile"), path);
  for (time_t t : m_reader->getListDates()) {
    m_step_times.append(static_cast<long long>(t));
    m_steps.append(QDateTime::fromSecsSinceEpoch(t)
                       .toString(QStringLiteral("ddd dd MMM hh:mm")));
  }
  m_status = tr("%1 records, %2 time steps")
                 .arg(m_reader->getTotalNumberOfGribRecords())
                 .arg(m_steps.size());
  Q_EMIT gribChanged();
  pushToLayer();
}

void GribContext::setTimeIndex(int i) {
  if (i == m_time_index || i < 0 || i >= m_step_times.size()) return;
  m_time_index = i;
  Q_EMIT timeChanged();
  pushToLayer();
}

void GribContext::setShowWind(bool on) {
  if (on == m_show_wind) return;
  m_show_wind = on;
  Q_EMIT timeChanged();
  pushToLayer();
}

void GribContext::setShowPressure(bool on) {
  if (on == m_show_pressure) return;
  m_show_pressure = on;
  Q_EMIT timeChanged();
  pushToLayer();
}

void GribContext::pushToLayer() {
  if (!m_layer) return;
  if (!m_reader || m_step_times.isEmpty()) {
    m_layer->clearGrid();
    m_layer->clearIsobars();
    return;
  }
  const time_t t = static_cast<time_t>(m_step_times[m_time_index]);
  // Mean-sea-level pressure -> 2 hPa isobars.
  GribRecord* rp =
      m_show_pressure
          ? m_reader->getGribRecord(GRB_PRESSURE, LV_MSL, 0, t)
          : nullptr;
  if (rp && rp->isOk()) {
    ocpn::qtui::GribWindLayer::ScalarGrid pg;
    pg.ni = rp->getNi();
    pg.nj = rp->getNj();
    pg.lon0 = rp->getX(0);
    pg.lat0 = rp->getY(0);
    pg.di = rp->getNi() > 1 ? rp->getX(1) - rp->getX(0) : 0;
    pg.dj = rp->getNj() > 1 ? rp->getY(1) - rp->getY(0) : 0;
    pg.v.resize(pg.ni * pg.nj);
    for (int j = 0; j < pg.nj; ++j)
      for (int i = 0; i < pg.ni; ++i)
        pg.v[j * pg.ni + i] =
            rp->isDefined(i, j)
                ? static_cast<float>(rp->getValue(i, j) / 100.0)  // Pa->hPa
                : NAN;
    m_layer->setIsobars(pg);
  } else {
    m_layer->clearIsobars();
  }
  if (!m_show_wind) {
    m_layer->clearGrid();
    return;
  }
  // 10 m wind components (the zyGrib constants the wx overlay uses).
  GribRecord* ru = m_reader->getGribRecord(GRB_WIND_VX, LV_ABOV_GND, 10, t);
  GribRecord* rv = m_reader->getGribRecord(GRB_WIND_VY, LV_ABOV_GND, 10, t);
  if (!ru || !rv || !ru->isOk() || !rv->isOk()) {
    m_layer->clearGrid();
    return;
  }
  ocpn::qtui::GribWindLayer::WindGrid g;
  g.ni = ru->getNi();
  g.nj = ru->getNj();
  g.lon0 = ru->getX(0);
  g.lat0 = ru->getY(0);
  g.di = ru->getNi() > 1 ? ru->getX(1) - ru->getX(0) : 0;
  g.dj = ru->getNj() > 1 ? ru->getY(1) - ru->getY(0) : 0;
  g.u.resize(g.ni * g.nj);
  g.v.resize(g.ni * g.nj);
  for (int j = 0; j < g.nj; ++j) {
    for (int i = 0; i < g.ni; ++i) {
      const int k = j * g.ni + i;
      g.u[k] = ru->isDefined(i, j) ? static_cast<float>(ru->getValue(i, j))
                                   : NAN;
      g.v[k] = rv->isDefined(i, j) ? static_cast<float>(rv->getValue(i, j))
                                   : NAN;
    }
  }
  m_layer->setGrid(g);
}

QString GribContext::readoutAt(double lat, double lon) const {
  if (!m_reader || m_step_times.isEmpty()) return {};
  const time_t t = static_cast<time_t>(m_step_times[m_time_index]);
  QStringList parts;
  GribRecord* ru = m_reader->getGribRecord(GRB_WIND_VX, LV_ABOV_GND, 10, t);
  GribRecord* rv = m_reader->getGribRecord(GRB_WIND_VY, LV_ABOV_GND, 10, t);
  if (ru && rv && ru->isOk() && rv->isOk()) {
    // getInterpolatedValue handles bilinear sampling + grid bounds.
    const double u = ru->getInterpolatedValue(lon, lat, true);
    const double v = rv->getInterpolatedValue(lon, lat, true);
    if (u != GRIB_NOTDEF && v != GRIB_NOTDEF) {
      const double kn = std::hypot(u, v) * 1.94384;
      double dir = std::atan2(-u, -v) * 180.0 / M_PI;  // FROM direction
      if (dir < 0) dir += 360.0;
      parts << QStringLiteral("%1 kn @ %2°")
                   .arg(kn, 0, 'f', 1)
                   .arg(qRound(dir));
    }
  }
  GribRecord* rp = m_reader->getGribRecord(GRB_PRESSURE, LV_MSL, 0, t);
  if (rp && rp->isOk()) {
    const double pa = rp->getInterpolatedValue(lon, lat, true);
    if (pa != GRIB_NOTDEF)
      parts << QStringLiteral("%1 hPa").arg(pa / 100.0, 0, 'f', 0);
  }
  return parts.join(QStringLiteral("   "));
}

QString GribContext::requestGrib(double north, double south, double east,
                                 double west, int days, bool wind,
                                 bool pressure, bool waves, bool precip) {
  // The classic saildocs GFS request line, e.g.
  //   send GFS:42N,38N,10W,2W|0.5,0.5|0,6..72|WIND,PRMSL
  auto coord = [](double v, char pos, char neg) {
    return QStringLiteral("%1%2")
        .arg(std::fabs(v), 0, 'f', 1)
        .arg(v >= 0 ? pos : neg);
  };
  QStringList params;
  if (wind) params << QStringLiteral("WIND");
  if (pressure) params << QStringLiteral("PRMSL");
  if (waves) params << QStringLiteral("HTSGW,WVDIR");
  if (precip) params << QStringLiteral("APCP");
  if (params.isEmpty()) params << QStringLiteral("WIND,PRMSL");
  const QString body =
      QStringLiteral("send GFS:%1,%2,%3,%4|0.5,0.5|0,6..%5|%6")
          .arg(coord(north, 'N', 'S'), coord(south, 'N', 'S'),
               coord(west, 'E', 'W'), coord(east, 'E', 'W'))
          .arg(qBound(24, days * 24, 192))
          .arg(params.join(','));
  QUrl mailto(QStringLiteral("mailto:query@saildocs.com"));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("subject"), QStringLiteral("GRIB request"));
  q.addQueryItem(QStringLiteral("body"), body);
  mailto.setQuery(q);
  QDesktopServices::openUrl(mailto);
  return body;
}

bool GribPlugin::init(const ocpn::qtui::OcpnQtPluginHost& host) {
  m_ctx = new GribContext(host.timeline, this);
  if (host.registerLayer) {
    auto* layer = new ocpn::qtui::GribWindLayer();
    layer->setViewport(host.viewport);
    layer->setZOrder(1450);  // over charts/grid, under the nav overlays
    m_ctx->setLayer(layer);
    host.registerLayer(layer);  // compositor takes ownership
  }
  if (host.registerHud)
    host.registerHud(QUrl(QStringLiteral("qrc:/grib_plugin/CursorReadout.qml")),
                     m_ctx);
  if (host.registerHud)
    host.registerHud(QUrl(QStringLiteral("qrc:/grib_plugin/ControlBar.qml")),
                     m_ctx);
  if (host.registerToolbarAction)
    host.registerToolbarAction(
        QStringLiteral("🌬"), QStringLiteral("GRIB weather"),
        [this] { if (m_ctx) m_ctx->toggleControls(); });
  if (host.registerSettingsPage)
    host.registerSettingsPage(
        QStringLiteral("GRIB"),
        QUrl(QStringLiteral("qrc:/grib_plugin/Settings.qml")), m_ctx);
  return true;
}

void GribPlugin::deinit() {
  delete m_ctx;
  m_ctx = nullptr;
}
