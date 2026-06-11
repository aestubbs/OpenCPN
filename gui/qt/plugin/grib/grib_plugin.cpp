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

namespace {
// The tier-1 type catalog: key, label, GRIB record (type/levelType/level),
// rendering recipe. Wind + pressure keep their dedicated paths.
struct TypeSpec {
  const char* key;
  const char* label;
  int dataType, levelType, level;
  enum Kind { ArrowsUV, ArrowsDirMag, Numbers } kind;
  QColor color;
  const char* suffix;
  double factor, offset;
  int decimals;
};
const TypeSpec kTypes[] = {
    {"waves", "Waves", GRB_HTSGW, LV_GND_SURF, 0, TypeSpec::ArrowsDirMag,
     QColor(70, 160, 220), " m", 1.0, 0.0, 1},
    {"current", "Current", GRB_UOGRD, LV_GND_SURF, 0, TypeSpec::ArrowsUV,
     QColor(40, 120, 200), " kn", 1.94384, 0.0, 1},
    {"gust", "Gust", GRB_WIND_GUST, LV_GND_SURF, 0, TypeSpec::Numbers,
     QColor(220, 90, 40), " kn", 1.94384, 0.0, 0},
    {"rain", "Rain", GRB_PRECIP_TOT, LV_GND_SURF, 0, TypeSpec::Numbers,
     QColor(60, 110, 200), " mm", 1.0, 0.0, 1},
    {"cloud", "Cloud", GRB_CLOUD_TOT, LV_ATMOS_ALL, 0, TypeSpec::Numbers,
     QColor(120, 120, 130), " %", 1.0, 0.0, 0},
    {"airtemp", "Air temp", GRB_TEMP, LV_ABOV_GND, 2, TypeSpec::Numbers,
     QColor(200, 60, 60), QStringLiteral("°").toUtf8().constData(), 1.0,
     -273.15, 0},
    {"seatemp", "Sea temp", GRB_TEMP, LV_GND_SURF, 0, TypeSpec::Numbers,
     QColor(40, 140, 120), QStringLiteral("°").toUtf8().constData(), 1.0,
     -273.15, 0},
    {"cape", "CAPE", GRB_CAPE, LV_GND_SURF, 0, TypeSpec::Numbers,
     QColor(160, 60, 180), "", 1.0, 0.0, 0},
    {"refl", "Reflectivity", GRB_COMP_REFL, LV_ATMOS_ALL, 0,
     TypeSpec::Numbers, QColor(180, 140, 40), " dBZ", 1.0, 0.0, 0},
    {"humid", "Humidity", GRB_HUMID_REL, LV_ABOV_GND, 2, TypeSpec::Numbers,
     QColor(100, 100, 180), " %", 1.0, 0.0, 0},
};
}  // namespace

GribContext::GribContext(QObject* timeline, QObject* parent)
    : QObject(parent), m_timeline(timeline) {
  {
    QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"));
    for (const TypeSpec& t : kTypes)
      m_type_shown[QLatin1String(t.key)] =
          st.value(QStringLiteral("show_") + t.key, false).toBool();
    m_overlay_key = st.value(QStringLiteral("overlayKey")).toString();
  }
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
  if (best != m_time_index) {
    m_time_index = best;  // label tracking; push happens below anyway
    Q_EMIT timeChanged();
  }
  // Tier 3: every scrub re-renders -- fields interpolate between steps.
  pushToLayer();
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

QVariantList GribContext::dataTypes() const {
  QVariantList out;
  auto add = [&](const QString& key, const QString& label, bool avail,
                 bool shown) {
    QVariantMap m;
    m["key"] = key;
    m["label"] = label;
    m["available"] = avail;
    m["shown"] = shown;
    out.append(m);
  };
  add("wind", tr("Wind"), m_type_available.value("wind", false), m_show_wind);
  add("pressure", tr("Pressure"),
      m_type_available.value("pressure", false), m_show_pressure);
  for (const TypeSpec& t : kTypes)
    add(QLatin1String(t.key), tr(t.label),
        m_type_available.value(QLatin1String(t.key), false),
        m_type_shown.value(QLatin1String(t.key), false));
  return out;
}

void GribContext::setTypeShown(const QString& key, bool on) {
  if (key == QLatin1String("wind")) {
    setShowWind(on);
    Q_EMIT typesChanged();
    return;
  }
  if (key == QLatin1String("pressure")) {
    setShowPressure(on);
    Q_EMIT typesChanged();
    return;
  }
  m_type_shown[key] = on;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
      .setValue(QStringLiteral("show_") + key, on);
  Q_EMIT typesChanged();
  pushToLayer();
}

time_t GribContext::displayEpoch() const {
  // The timeline's exact time, clamped to the file's span; falls back to
  // the selected step when no timeline rides along.
  qint64 e = m_step_times.isEmpty() ? 0 : m_step_times[m_time_index];
  if (m_timeline) {
    const QDateTime t = m_timeline->property("displayTime").toDateTime();
    if (t.isValid()) e = t.toSecsSinceEpoch();
  }
  if (!m_step_times.isEmpty())
    e = qBound(m_step_times.first(), e, m_step_times.last());
  return static_cast<time_t>(e);
}

GribRecord* GribContext::recordAt(int dataType, int levelType, int level,
                                  bool* owned, bool directional) {
  *owned = false;
  const time_t T = displayEpoch();
  GribRecord* exact = m_reader->getGribRecord(dataType, levelType, level, T);
  if (exact && exact->isOk()) return exact;
  // Bracket T between steps and interpolate (tier 3).
  for (int i = 0; i + 1 < m_step_times.size(); ++i) {
    if (T > m_step_times[i] && T < m_step_times[i + 1]) {
      GribRecord* a = m_reader->getGribRecord(
          dataType, levelType, level, static_cast<time_t>(m_step_times[i]));
      GribRecord* b = m_reader->getGribRecord(
          dataType, levelType, level,
          static_cast<time_t>(m_step_times[i + 1]));
      if (a && b && a->isOk() && b->isOk()) {
        const double d =
            double(T - m_step_times[i]) /
            double(m_step_times[i + 1] - m_step_times[i]);
        GribRecord* r = GribRecord::InterpolatedRecord(*a, *b, d,
                                                       directional);
        if (r) *owned = true;
        return r;
      }
      break;
    }
  }
  return nullptr;
}

void GribContext::recordPairAt(int dtX, int dtY, int levelType, int level,
                               GribRecord** rx, GribRecord** ry,
                               bool* owned) {
  *owned = false;
  *rx = nullptr;
  *ry = nullptr;
  const time_t T = displayEpoch();
  GribRecord* ex = m_reader->getGribRecord(dtX, levelType, level, T);
  GribRecord* ey = m_reader->getGribRecord(dtY, levelType, level, T);
  if (ex && ey && ex->isOk() && ey->isOk()) {
    *rx = ex;
    *ry = ey;
    return;
  }
  for (int i = 0; i + 1 < m_step_times.size(); ++i) {
    if (T > m_step_times[i] && T < m_step_times[i + 1]) {
      const time_t t0 = static_cast<time_t>(m_step_times[i]);
      const time_t t1 = static_cast<time_t>(m_step_times[i + 1]);
      GribRecord* ax = m_reader->getGribRecord(dtX, levelType, level, t0);
      GribRecord* ay = m_reader->getGribRecord(dtY, levelType, level, t0);
      GribRecord* bx = m_reader->getGribRecord(dtX, levelType, level, t1);
      GribRecord* by = m_reader->getGribRecord(dtY, levelType, level, t1);
      if (ax && ay && bx && by && ax->isOk() && ay->isOk() && bx->isOk() &&
          by->isOk()) {
        const double d = double(T - t0) / double(t1 - t0);
        GribRecord* iy = nullptr;
        GribRecord* ix =
            GribRecord::Interpolated2DRecord(iy, *ax, *ay, *bx, *by, d);
        if (ix && iy) {
          *rx = ix;
          *ry = iy;
          *owned = true;
        } else {
          delete ix;
          delete iy;
        }
      }
      return;
    }
  }
}

void GribContext::setOverlayKey(const QString& k) {
  if (k == m_overlay_key) return;
  m_overlay_key = k;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
      .setValue(QStringLiteral("overlayKey"), k);
  Q_EMIT typesChanged();
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
  {
    GribRecord* rw =
        m_reader->getGribRecord(GRB_WIND_VX, LV_ABOV_GND, 10, t);
    m_type_available[QStringLiteral("wind")] = rw && rw->isOk();
    GribRecord* rpp = m_reader->getGribRecord(GRB_PRESSURE, LV_MSL, 0, t);
    m_type_available[QStringLiteral("pressure")] = rpp && rpp->isOk();
  }
  bool ownP = false;
  GribRecord* rp =
      m_show_pressure ? recordAt(GRB_PRESSURE, LV_MSL, 0, &ownP) : nullptr;
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
  if (ownP) delete rp;
  // Tier-1 fields: availability + per-type push.
  auto scalarFrom = [](GribRecord* r) {
    ocpn::qtui::GribWindLayer::ScalarGrid g;
    g.ni = r->getNi();
    g.nj = r->getNj();
    g.lon0 = r->getX(0);
    g.lat0 = r->getY(0);
    g.di = r->getNi() > 1 ? r->getX(1) - r->getX(0) : 0;
    g.dj = r->getNj() > 1 ? r->getY(1) - r->getY(0) : 0;
    g.v.resize(g.ni * g.nj);
    for (int j = 0; j < g.nj; ++j)
      for (int i = 0; i < g.ni; ++i)
        g.v[j * g.ni + i] = r->isDefined(i, j)
                                ? static_cast<float>(r->getValue(i, j))
                                : NAN;
    return g;
  };
  for (const TypeSpec& tspec : kTypes) {
    const QString key = QLatin1String(tspec.key);
    GribRecord* r1 =
        m_reader->getGribRecord(tspec.dataType, tspec.levelType,
                                tspec.level, t);
    const bool avail = r1 && r1->isOk();
    m_type_available[key] = avail;
    const bool shown = avail && m_type_shown.value(key, false);
    if (tspec.kind == TypeSpec::ArrowsUV ||
        tspec.kind == TypeSpec::ArrowsDirMag) {
      if (!shown) {
        m_layer->clearArrows(key);
        continue;
      }
      ocpn::qtui::GribWindLayer::ArrowField f;
      f.color = tspec.color;
      f.unitSuffix = QString::fromUtf8(tspec.suffix);
      f.unitFactor = tspec.factor;
      f.dirMag = (tspec.kind == TypeSpec::ArrowsDirMag);
      bool own1 = false, own2 = false;
      GribRecord* r2 = nullptr;
      if (f.dirMag) {  // magnitude + FROM-direction, both interpolated
        r1 = recordAt(tspec.dataType, tspec.levelType, tspec.level, &own1);
        r2 = recordAt(GRB_WVDIR, tspec.levelType, tspec.level, &own2, true);
      } else {  // u/v pair, vector-interpolated together
        recordPairAt(GRB_UOGRD, GRB_VOGRD, tspec.levelType, tspec.level,
                     &r1, &r2, &own1);
        own2 = false;  // pair ownership rides own1
      }
      if (!r1 || !r2 || !r1->isOk() || !r2->isOk()) {
        if (own1) { delete r1; if (!f.dirMag) delete r2; }
        if (own2) delete r2;
        m_layer->clearArrows(key);
        continue;
      }
      // Pack: u <- (dir or u-component), v <- (magnitude or v-component).
      f.grid.ni = r1->getNi();
      f.grid.nj = r1->getNj();
      f.grid.lon0 = r1->getX(0);
      f.grid.lat0 = r1->getY(0);
      f.grid.di = r1->getNi() > 1 ? r1->getX(1) - r1->getX(0) : 0;
      f.grid.dj = r1->getNj() > 1 ? r1->getY(1) - r1->getY(0) : 0;
      f.grid.u.resize(f.grid.ni * f.grid.nj);
      f.grid.v.resize(f.grid.ni * f.grid.nj);
      for (int j = 0; j < f.grid.nj; ++j)
        for (int i = 0; i < f.grid.ni; ++i) {
          const int k = j * f.grid.ni + i;
          const bool ok1 = r1->isDefined(i, j), ok2 = r2->isDefined(i, j);
          if (f.dirMag) {
            f.grid.u[k] = ok2 ? static_cast<float>(r2->getValue(i, j)) : NAN;
            f.grid.v[k] = ok1 ? static_cast<float>(r1->getValue(i, j)) : NAN;
          } else {
            f.grid.u[k] = ok1 ? static_cast<float>(r1->getValue(i, j)) : NAN;
            f.grid.v[k] = ok2 ? static_cast<float>(r2->getValue(i, j)) : NAN;
          }
        }
      m_layer->setArrows(key, f);
      if (own1) { delete r1; if (!f.dirMag) delete r2; }
      if (own2) delete r2;
    } else {  // Numbers
      if (!shown) {
        m_layer->clearNumbers(key);
        continue;
      }
      bool ownN = false;
      GribRecord* rn =
          recordAt(tspec.dataType, tspec.levelType, tspec.level, &ownN);
      if (!rn || !rn->isOk()) {
        if (ownN) delete rn;
        m_layer->clearNumbers(key);
        continue;
      }
      ocpn::qtui::GribWindLayer::NumberField f;
      f.grid = scalarFrom(rn);
      f.color = tspec.color;
      f.suffix = QString::fromUtf8(tspec.suffix);
      f.factor = tspec.factor;
      f.offset = tspec.offset;
      f.decimals = tspec.decimals;
      m_layer->setNumbers(key, f);
      if (ownN) delete rn;
    }
  }
  // Colour-mapped overlay (tier 2): one field at a time.
  if (m_overlay_key.isEmpty()) {
    m_layer->clearOverlay();
  } else if (m_overlay_key == QLatin1String("wind")) {
    bool ownW = false;
    GribRecord* ru = nullptr;
    GribRecord* rv = nullptr;
    recordPairAt(GRB_WIND_VX, GRB_WIND_VY, LV_ABOV_GND, 10, &ru, &rv,
                 &ownW);
    if (ru && rv && ru->isOk() && rv->isOk()) {
      auto g = scalarFrom(ru);  // reuse geometry; recompute as speed
      for (int j = 0; j < g.nj; ++j)
        for (int i = 0; i < g.ni; ++i) {
          const int k = j * g.ni + i;
          const bool ok = ru->isDefined(i, j) && rv->isDefined(i, j);
          g.v[k] = ok ? static_cast<float>(
                            std::hypot(ru->getValue(i, j),
                                       rv->getValue(i, j)))
                      : NAN;
        }
      m_layer->setOverlay(g, QStringLiteral("wind"), 0);
      if (ownW) { delete ru; delete rv; }
    } else {
      m_layer->clearOverlay();
    }
  } else {
    // Generic ramp over the chosen scalar's own range.
    const TypeSpec* spec = nullptr;
    for (const TypeSpec& ts : kTypes)
      if (m_overlay_key == QLatin1String(ts.key)) spec = &ts;
    bool ownO = false;
    GribRecord* r =
        spec ? recordAt(spec->dataType, spec->levelType, spec->level, &ownO)
             : nullptr;
    if (r && r->isOk()) {
      auto g = scalarFrom(r);
      float mx = 0;
      for (float v : g.v)
        if (!std::isnan(v)) mx = qMax(mx, v);
      m_layer->setOverlay(g, QStringLiteral("generic"), mx);
    } else {
      m_layer->clearOverlay();
    }
    if (ownO) delete r;
  }

  Q_EMIT typesChanged();

  if (!m_show_wind) {
    m_layer->clearGrid();
    return;
  }
  // 10 m wind, vector-interpolated to the exact display time (tier 3).
  bool ownWind = false;
  GribRecord* ru = nullptr;
  GribRecord* rv = nullptr;
  recordPairAt(GRB_WIND_VX, GRB_WIND_VY, LV_ABOV_GND, 10, &ru, &rv,
               &ownWind);
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
  if (ownWind) {
    delete ru;
    delete rv;
  }
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
