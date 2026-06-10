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

#include <QDateTime>
#include <QUrl>

#include "GribReader.h"  // vendored zyGrib decode core (grib_pi)
#include "GribRecord.h"
#include "grib_wind_layer.h"

GribContext::GribContext(QObject* parent) : QObject(parent) {}

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

bool GribPlugin::init(const ocpn::qtui::OcpnQtPluginHost& host) {
  m_ctx = new GribContext(this);
  if (host.registerLayer) {
    auto* layer = new ocpn::qtui::GribWindLayer();
    layer->setZOrder(1450);  // over charts/grid, under the nav overlays
    m_ctx->setLayer(layer);
    host.registerLayer(layer);  // compositor takes ownership
  }
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
