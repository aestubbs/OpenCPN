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
#include <tuple>

#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
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
     QColor(200, 60, 60), "\xC2\xB0", 1.0, -273.15, 0},
    {"seatemp", "Sea temp", GRB_TEMP, LV_GND_SURF, 0, TypeSpec::Numbers,
     QColor(40, 140, 120), "\xC2\xB0", 1.0, -273.15, 0},
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
    m_grib_dir = st.value(QStringLiteral("gribDir")).toUrl();
    m_particle_density = st.value(QStringLiteral("particleDensity"), 5).toInt();
    m_interpolate = st.value(QStringLiteral("interpolate"), true).toBool();
    m_overlay_transparency =
        st.value(QStringLiteral("overlayTransparency"), 55).toInt();
    const QStringList unitKeys =
        st.value(QStringLiteral("unitKeys")).toStringList();
    for (const QString& uk : unitKeys)
      m_units[uk] = st.value(QStringLiteral("unit_") + uk).toString();
  }
  // Restore the last GRIB set on startup (wx parity): the timeline can
  // drive the weather immediately. lastFiles carries the merged set;
  // lastFile is the pre-multi-file key, kept as the fallback.
  {
    QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"));
    QStringList last = st.value(QStringLiteral("lastFiles")).toStringList();
    if (last.isEmpty()) {
      const QString single = st.value(QStringLiteral("lastFile")).toString();
      if (!single.isEmpty()) last << single;
    }
    if (qEnvironmentVariableIsSet("OCPN_GRIB_FILE"))  // ';' = merge list
      last = qEnvironmentVariable("OCPN_GRIB_FILE")
                 .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QStringList existing;
    for (const QString& p : last)
      if (!p.isEmpty() && QFile::exists(p)) existing << p;
    if (!existing.isEmpty())
      QMetaObject::invokeMethod(
          this,
          [this, existing] {
            m_paths = existing;
            reload();
          },
          Qt::QueuedConnection);
  }
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
    emit timeChanged();
  }
  // Tier 3: scrubs re-render with interpolation. Throttled: a full grid
  // push per 1 s live tick is wasted work -- re-push only when display
  // time moved >= 3 min from the last push (a scrub always qualifies).
  static qint64 s_last_push = 0;
  const qint64 e = displayEpoch();
  // Flat 30 s display-time threshold: scrubs render fluidly (any drag
  // crosses it instantly) and the 1 Hz live clock costs one grid
  // rebuild per half-minute.
  if (std::llabs(e - s_last_push) < 30) return;
  s_last_push = e;
  pushToLayer();
}

GribContext::~GribContext() { delete m_reader; }

void GribContext::openFile(const QUrl& url) {
  const QString path = url.toLocalFile();
  if (path.isEmpty()) return;
  m_paths = QStringList{path};
  reload();
}

void GribContext::addFile(const QUrl& url) {
  const QString path = url.toLocalFile();
  if (path.isEmpty() || m_paths.contains(path)) return;
  if (m_paths.isEmpty()) {  // nothing loaded: plain open
    openFile(url);
    return;
  }
  m_paths.append(path);
  reload();
}

void GribContext::reload() {
  delete m_reader;
  // wx GRIBFile parity: ONE GribReader fed every file -- openFile
  // accumulates records across calls (clean_all_vectors stays off), so
  // a wave file layers over a wind file into one record set.
  m_reader = new GribReader();
  bool any_ok = false;
  for (const QString& p : m_paths) {
    m_reader->openFile(wxString(p.toStdString()));
    if (m_reader->isOk()) any_ok = true;
    if (qEnvironmentVariableIsSet("OCPN_GRIB_SELFTEST"))
      qWarning("grib: reload %s ok=%d total=%d", qPrintable(p),
               m_reader->isOk() ? 1 : 0,
               m_reader->getTotalNumberOfGribRecords());
  }
  m_steps.clear();
  m_step_times.clear();
  m_time_index = 0;
  if (!any_ok || m_reader->getTotalNumberOfGribRecords() == 0) {
    m_status = m_paths.size() > 1 ? tr("No readable GRIB files")
                                  : tr("Not a readable GRIB file");
    m_file.clear();
    m_paths.clear();
    if (m_layer) m_layer->clearGrid();
    emit gribChanged();
    return;
  }
  // wx GRIBFile post-load fixups (GribUIDialog.cpp:2051+): rain/cloud
  // accumulation normalization, then propagate cumulative + wave records
  // into steps that lack them (waves often ride a coarser cadence). Run
  // ONCE over the merged set, after every file is in (wx order).
  m_reader->computeAccumulationRecords(GRB_PRECIP_TOT, LV_GND_SURF, 0);
  m_reader->computeAccumulationRecords(GRB_PRECIP_RATE, LV_GND_SURF, 0);
  m_reader->computeAccumulationRecords(GRB_CLOUD_TOT, LV_ATMOS_ALL, 0);
  m_reader->copyFirstCumulativeRecord();
  m_reader->copyMissingWaveRecords();

  m_file = m_paths.first().section('/', -1);
  if (m_paths.size() > 1)
    m_file += tr(" +%1").arg(m_paths.size() - 1);
  {
    QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"));
    st.setValue(QStringLiteral("lastFiles"), m_paths);
    st.setValue(QStringLiteral("lastFile"), m_paths.first());
  }
  for (time_t t : m_reader->getListDates()) {
    m_step_times.append(static_cast<long long>(t));
    m_steps.append(QDateTime::fromSecsSinceEpoch(t)
                       .toString(QStringLiteral("ddd dd MMM hh:mm")));
  }
  if (qEnvironmentVariableIsSet("OCPN_GRIB_SELFTEST"))
    for (int i = 0; i < m_step_times.size(); ++i) {
      GribRecord* r = m_reader->getGribRecord(
          GRB_WIND_VX, LV_ABOV_GND, 10,
          static_cast<time_t>(m_step_times[i]));
      qWarning("grib: step %d (%s) u[0]=%s lon0=%.1f", i,
               qPrintable(m_steps[i]),
               r && r->isOk()
                   ? qPrintable(QString::number(r->getValue(0, 0), 'f', 2))
                   : "n/a",
               r && r->isOk() ? r->getX(0) : 0.0);
    }
  if (qEnvironmentVariableIsSet("OCPN_GRIB_SELFTEST") && m_timeline &&
      m_step_times.size() >= 2) {
    // Drive the REAL timeline (the same path a user scrub takes)
    // through steps and midpoints, 1.5 s apart.
    auto* seq = new QTimer(this);
    seq->setInterval(1500);
    auto idx = std::make_shared<int>(0);
    connect(seq, &QTimer::timeout, this, [this, seq, idx]() {
      QList<qint64> targets;
      for (int i = 0; i < m_step_times.size(); ++i) {
        targets << m_step_times[i];
        if (i + 1 < m_step_times.size())
          targets << (m_step_times[i] + m_step_times[i + 1]) / 2;
      }
      if (*idx >= targets.size()) {
        qWarning("grib-selftest: DONE");
        seq->stop();
        seq->deleteLater();
        return;
      }
      const QDateTime t = QDateTime::fromSecsSinceEpoch(targets[*idx]);
      qWarning("grib-selftest: setDisplayTime -> %s",
               qPrintable(t.toString(Qt::ISODate)));
      QMetaObject::invokeMethod(m_timeline, "setDisplayTime",
                                Q_ARG(QDateTime, t));
      ++(*idx);
    });
    seq->start();
  }
  // Pin the forecast steps + coverage span onto the chart time bar.
  if (m_timeline) {
    QVariantList marks;
    for (qint64 t : m_step_times) marks << t;
    QMetaObject::invokeMethod(m_timeline, "setMarks",
                              Q_ARG(QVariantList, marks));
    QMetaObject::invokeMethod(
        m_timeline, "setSpan", Q_ARG(double, double(m_step_times.first())),
        Q_ARG(double, double(m_step_times.last())));
    // If the bar is outside the forecast, snap it to the first step so
    // the loaded data is what the user is looking at.
    const QDateTime cur =
        m_timeline->property("displayTime").toDateTime();
    const qint64 e = cur.isValid() ? cur.toSecsSinceEpoch() : 0;
    if (e < m_step_times.first() || e > m_step_times.last())
      QMetaObject::invokeMethod(
          m_timeline, "setDisplayTime",
          Q_ARG(QDateTime,
                QDateTime::fromSecsSinceEpoch(m_step_times.first())));
  }
  m_status = tr("%1 records, %2 time steps")
                 .arg(m_reader->getTotalNumberOfGribRecords())
                 .arg(m_steps.size());
  emit gribChanged();
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
    emit typesChanged();
    return;
  }
  if (key == QLatin1String("pressure")) {
    setShowPressure(on);
    emit typesChanged();
    return;
  }
  m_type_shown[key] = on;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
      .setValue(QStringLiteral("show_") + key, on);
  emit typesChanged();
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
  if (!m_interpolate) return nullptr;  // nearest-step mode
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
  if (!m_interpolate) return;  // nearest-step mode
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
  emit typesChanged();
  pushToLayer();
}

namespace {
// Unit tables per type kind. The FIRST entry is the default; factors
// convert from the GRIB native unit.
struct UnitDef { const char* name; double factor; double offset; };
const UnitDef kSpeedUnits[] = {{"kn", 1.94384, 0}, {"m/s", 1.0, 0},
                               {"km/h", 3.6, 0}, {"mph", 2.23694, 0}};
const UnitDef kTempUnits[] = {{"°C", 1.0, -273.15},
                              {"°F", 1.8, -459.67}};
const UnitDef kHeightUnits[] = {{"m", 1.0, 0}, {"ft", 3.28084, 0}};
const UnitDef kPressUnits[] = {{"hPa", 0.01, 0}, {"inHg", 0.0002953, 0}};

const UnitDef* unitTable(const QString& key, int* n) {
  if (key == QLatin1String("wind") || key == QLatin1String("gust") ||
      key == QLatin1String("current")) {
    *n = 4;
    return kSpeedUnits;
  }
  if (key == QLatin1String("airtemp") || key == QLatin1String("seatemp")) {
    *n = 2;
    return kTempUnits;
  }
  if (key == QLatin1String("waves")) {
    *n = 2;
    return kHeightUnits;
  }
  if (key == QLatin1String("pressure")) {
    *n = 2;
    return kPressUnits;
  }
  *n = 0;
  return nullptr;
}
}  // namespace

// The active unit's conversion for a type (native -> display).
static void activeUnit(const QString& key, const QString& chosen,
                       double* factor, double* offset, QString* suffix) {
  int n = 0;
  const UnitDef* t = unitTable(key, &n);
  for (int i = 0; i < n; ++i) {
    if (chosen == QString::fromUtf8(t[i].name) || (chosen.isEmpty() && i == 0)) {
      *factor = t[i].factor;
      *offset = t[i].offset;
      *suffix = QStringLiteral(" ") + QString::fromUtf8(t[i].name);
      return;
    }
  }
  if (n > 0) {
    *factor = t[0].factor;
    *offset = t[0].offset;
    *suffix = QStringLiteral(" ") + QString::fromUtf8(t[0].name);
  }
}

QStringList GribContext::unitOptions(const QString& key) const {
  int n = 0;
  const UnitDef* t = unitTable(key, &n);
  QStringList out;
  for (int i = 0; i < n; ++i) out << QString::fromUtf8(t[i].name);
  return out;
}

QString GribContext::unitFor(const QString& key) const {
  const QString u = m_units.value(key);
  if (!u.isEmpty()) return u;
  const QStringList opts = unitOptions(key);
  return opts.isEmpty() ? QString() : opts.first();
}

void GribContext::setUnitFor(const QString& key, const QString& unit) {
  if (m_units.value(key) == unit) return;
  m_units[key] = unit;
  QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"));
  st.setValue(QStringLiteral("unit_") + key, unit);
  st.setValue(QStringLiteral("unitKeys"), QStringList(m_units.keys()));
  emit typesChanged();
  pushToLayer();
}

void GribContext::setParticleDensity(int d) {
  d = qBound(1, d, 10);
  if (d == m_particle_density) return;
  m_particle_density = d;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
      .setValue(QStringLiteral("particleDensity"), d);
  if (m_layer) m_layer->setParticleDensity(d);
  emit typesChanged();
}

void GribContext::setInterpolate(bool on) {
  if (on == m_interpolate) return;
  m_interpolate = on;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
      .setValue(QStringLiteral("interpolate"), on);
  emit typesChanged();
  pushToLayer();
}

void GribContext::setOverlayTransparency(int pct) {
  pct = qBound(0, pct, 100);
  if (pct == m_overlay_transparency) return;
  m_overlay_transparency = pct;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
      .setValue(QStringLiteral("overlayTransparency"), pct);
  if (m_layer) m_layer->setOverlayAlpha(255 * (100 - pct) / 100 / 2);
  emit typesChanged();
  pushToLayer();
}

void GribContext::setMasterEnabled(bool on) {
  if (on == m_master_enabled) return;
  m_master_enabled = on;
  if (m_layer) m_layer->setVisible(on);
  if (notifyToolbar) notifyToolbar();
  // Weather needs the time bar (to scrub the forecast time): register/clear a
  // TimeController consumer so the bar auto-shows while GRIB is on.
  if (m_timeline)
    QMetaObject::invokeMethod(m_timeline, "setConsumer",
                              Q_ARG(QString, QStringLiteral("grib")),
                              Q_ARG(bool, on));
  emit controlsChanged();
}

QStringList GribContext::cursorRows(double lat, double lon) const {
  // wx CursorData::UpdateTrackingControls port: every loaded type,
  // sampled at the cursor from the TIME-INTERPOLATED records (the same
  // ones the layer renders), formatted with the wx precisions through
  // the per-type units engine.
  QStringList rows;
  if (!m_reader || m_step_times.isEmpty()) return rows;
  auto* self = const_cast<GribContext*>(this);

  auto qlon = [&](const GribRecord* r) {  // 0-360-grid query longitude
    return (r->getX(0) >= 180.0 && lon < 0) ? lon + 360.0 : lon;
  };
  auto deg = [](double a) {  // wx "%03d°"
    int d = static_cast<int>(a) % 360;
    if (d < 0) d += 360;
    return QString::asprintf("%03d\xC2\xB0", d);
  };
  auto unit = [&](const char* key) {
    double f = 1, o = 0;
    QString s;
    activeUnit(QLatin1String(key), m_units.value(QLatin1String(key)), &f, &o,
               &s);
    return std::make_tuple(f, o, s);
  };
  // wx GetmstobfFactor thresholds (m/s upper bound per Beaufort force).
  auto beaufort = [](double ms) {
    static constexpr double kTop[] = {0.5,  2.1,  3.6,  5.7,  8.7,  11.3,
                                      14.4, 17.5, 21.1, 24.7, 28.8, 32.9};
    for (int b = 0; b < 12; ++b)
      if (ms < kTop[b]) return b;
    return 12;
  };
  auto scalar = [&](int dt, int lt, int lv, bool dir = false) -> double {
    bool own = false;
    GribRecord* r = self->recordAt(dt, lt, lv, &own, dir);
    if (!r || !r->isOk()) {
      if (own) delete r;
      return GRIB_NOTDEF;
    }
    const double v = r->getInterpolatedValue(qlon(r), lat, true, dir);
    if (own) delete r;
    return v;
  };

  // Wind at the selected altitude: speed + Beaufort + FROM-direction.
  {
    bool own = false;
    GribRecord* rx = nullptr;
    GribRecord* ry = nullptr;
    const int lt = m_wind_altitude > 0 ? LV_ISOBARIC : LV_ABOV_GND;
    const int lv = m_wind_altitude > 0 ? m_wind_altitude : 10;
    self->recordPairAt(GRB_WIND_VX, GRB_WIND_VY, lt, lv, &rx, &ry, &own);
    double vms = 0, ang = 0;
    if (rx && ry &&
        GribRecord::getInterpolatedValues(vms, ang, rx, ry, qlon(rx), lat)) {
      auto [f, o, s] = unit("wind");
      rows << tr("Wind\t%1%2 - %3 bf  %4")
                  .arg(qRound(vms * f + o))
                  .arg(s)
                  .arg(beaufort(vms))
                  .arg(deg(ang));
    }
    if (own) {
      delete rx;
      delete ry;
    }
  }
  const double gust = scalar(GRB_WIND_GUST, LV_GND_SURF, 0);
  if (gust != GRIB_NOTDEF) {
    auto [f, o, s] = unit("gust");
    rows << tr("Gust\t%1%2").arg(qRound(gust * f + o)).arg(s);
  }
  const double pa = scalar(GRB_PRESSURE, LV_MSL, 0);
  if (pa != GRIB_NOTDEF) {
    auto [f, o, s] = unit("pressure");
    rows << tr("Pressure\t%1%2")
                .arg(pa * f + o, 0, 'f', s.contains("inHg") ? 2 : 1)
                .arg(s);
  }
  // Waves: height, " - <period>s" when present, direction.
  const double hs = scalar(GRB_HTSGW, LV_GND_SURF, 0);
  if (hs != GRIB_NOTDEF) {
    auto [f, o, s] = unit("waves");
    QString w = tr("%1%2").arg(hs * f + o, 0, 'f', 1).arg(s);
    const double per = scalar(GRB_WVPER, LV_GND_SURF, 0);
    if (per != GRIB_NOTDEF) w += tr(" - %1s").arg(qRound(per));
    const double wdir = scalar(GRB_WVDIR, LV_GND_SURF, 0, true);
    if (wdir != GRIB_NOTDEF) w += QStringLiteral("  ") + deg(wdir);
    rows << tr("Waves\t") + w;
  }
  // Current: speed + flow (TO) direction -- wx flips the wind
  // convention by 180 degrees.
  {
    bool own = false;
    GribRecord* rx = nullptr;
    GribRecord* ry = nullptr;
    self->recordPairAt(GRB_UOGRD, GRB_VOGRD, LV_GND_SURF, 0, &rx, &ry, &own);
    double vms = 0, ang = 0;
    if (rx && ry &&
        GribRecord::getInterpolatedValues(vms, ang, rx, ry, qlon(rx), lat)) {
      ang += 180;
      if (ang >= 360) ang -= 360;
      auto [f, o, s] = unit("current");
      rows << tr("Current\t%1%2  %3")
                  .arg(vms * f + o, 0, 'f', 1)
                  .arg(s)
                  .arg(deg(ang));
    }
    if (own) {
      delete rx;
      delete ry;
    }
  }
  const double rn = scalar(GRB_PRECIP_TOT, LV_GND_SURF, 0);
  if (rn != GRIB_NOTDEF)  // wx precision: 2 below 10, 1 below 100, else 0
    rows << tr("Rain\t%1 mm").arg(rn, 0, 'f', rn < 10 ? 2 : rn < 100 ? 1 : 0);
  const double cl = scalar(GRB_CLOUD_TOT, LV_ATMOS_ALL, 0);
  if (cl != GRIB_NOTDEF) rows << tr("Cloud\t%1 %").arg(cl, 0, 'f', 0);
  const double at = scalar(GRB_TEMP, LV_ABOV_GND, 2);
  if (at != GRIB_NOTDEF) {
    auto [f, o, s] = unit("airtemp");
    rows << tr("Air temp\t%1%2").arg(at * f + o, 0, 'f', 1).arg(s);
  }
  const double st = scalar(GRB_TEMP, LV_GND_SURF, 0);
  if (st != GRIB_NOTDEF) {
    auto [f, o, s] = unit("seatemp");
    rows << tr("Sea temp\t%1%2").arg(st * f + o, 0, 'f', 1).arg(s);
  }
  const double cape = scalar(GRB_CAPE, LV_GND_SURF, 0);
  if (cape != GRIB_NOTDEF)
    rows << tr("CAPE\t%1 J/kg").arg(cape, 0, 'f', 0);
  const double refl = scalar(GRB_COMP_REFL, LV_ATMOS_ALL, 0);
  if (refl != GRIB_NOTDEF)
    rows << tr("Reflectivity\t%1 dBZ").arg(refl, 0, 'f', 0);
  const double hu = scalar(GRB_HUMID_REL, LV_ABOV_GND, 2);
  if (hu != GRIB_NOTDEF) rows << tr("Humidity\t%1 %").arg(hu, 0, 'f', 0);
  // Geopotential altitude of the selected isobaric level (wx shows the
  // altitude extras only when flying above the surface).
  if (m_wind_altitude > 0) {
    const double gp =
        scalar(GRB_GEOPOT_HGT, LV_ISOBARIC, m_wind_altitude);
    if (gp != GRIB_NOTDEF)
      rows << tr("Altitude\t%1 m").arg(gp, 0, 'f', 0);
  }
  return rows;
}

void GribContext::stepTimeline(int delta) {
  if (m_step_times.isEmpty()) return;
  const int i = qBound(0, m_time_index + delta, int(m_step_times.size() - 1));
  const QDateTime t = QDateTime::fromSecsSinceEpoch(m_step_times[i]);
  if (m_timeline)
    QMetaObject::invokeMethod(m_timeline, "setDisplayTime",
                              Q_ARG(QDateTime, t));
  else
    setTimeIndex(i);
}

void GribContext::setGribDir(const QUrl& d) {
  if (d == m_grib_dir) return;
  m_grib_dir = d;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("grib-plugin"))
      .setValue(QStringLiteral("gribDir"), d);
  emit gribChanged();
}

QVariantList GribContext::dirFiles() const {
  QVariantList out;
  const QString dir = m_grib_dir.isLocalFile()
                          ? m_grib_dir.toLocalFile()
                          : QStandardPaths::writableLocation(
                                QStandardPaths::DownloadLocation);
  QDir qd(dir);
  const auto entries = qd.entryInfoList(
      {QStringLiteral("*.grb"), QStringLiteral("*.grb2"),
       QStringLiteral("*.grib"), QStringLiteral("*.grib2"),
       QStringLiteral("*.bz2"), QStringLiteral("*.gz")},
      QDir::Files, QDir::Time);  // newest first (wx parity)
  for (const QFileInfo& fi : entries) {
    QVariantMap m;
    m["name"] = fi.fileName();
    m["path"] = QUrl::fromLocalFile(fi.absoluteFilePath());
    m["date"] = fi.lastModified().toString(QStringLiteral("dd MMM hh:mm"));
    out.append(m);
  }
  return out;
}

QVariantList GribContext::altitudes() const {
  QVariantList out;
  auto add = [&](int hpa, const QString& label, bool avail) {
    QVariantMap m;
    m["hpa"] = hpa;
    m["label"] = label;
    m["available"] = avail;
    out.append(m);
  };
  add(0, tr("Surface"), true);
  if (m_reader && !m_step_times.isEmpty()) {
    const time_t t = static_cast<time_t>(m_step_times[m_time_index]);
    for (int hpa : {850, 700, 500, 300}) {
      GribRecord* r =
          m_reader->getGribRecord(GRB_WIND_VX, LV_ISOBARIC, hpa, t);
      add(hpa, QStringLiteral("%1 hPa").arg(hpa), r && r->isOk());
    }
  }
  return out;
}

void GribContext::setParticles(bool on) {
  if (on == m_particles) return;
  m_particles = on;
  if (m_layer) m_layer->setParticlesEnabled(on);
  emit typesChanged();
}

void GribContext::setWindAltitude(int hpa) {
  if (hpa == m_wind_altitude) return;
  m_wind_altitude = hpa;
  emit typesChanged();
  pushToLayer();
}

void GribContext::setTimeIndex(int i) {
  if (i == m_time_index || i < 0 || i >= m_step_times.size()) return;
  m_time_index = i;
  emit timeChanged();
  pushToLayer();
}

void GribContext::setShowWind(bool on) {
  if (on == m_show_wind) return;
  m_show_wind = on;
  emit timeChanged();
  pushToLayer();
}

void GribContext::setShowPressure(bool on) {
  if (on == m_show_pressure) return;
  m_show_pressure = on;
  emit timeChanged();
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
    if (pg.lon0 >= 180.0) pg.lon0 -= 360.0;
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
    if (g.lon0 >= 180.0) g.lon0 -= 360.0;  // GRIB 0..360 convention
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
      {  // honour the user's unit choice when the type has one
        double uf = f.unitFactor, uo = 0;
        QString us = f.unitSuffix;
        activeUnit(key, m_units.value(key), &uf, &uo, &us);
        f.unitFactor = uf;
        f.unitSuffix = us;
      }
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
      if (f.grid.lon0 >= 180.0) f.grid.lon0 -= 360.0;
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
      {
        double uf = f.factor, uo = f.offset;
        QString us = f.suffix;
        activeUnit(key, m_units.value(key), &uf, &uo, &us);
        f.factor = uf;
        f.offset = uo;
        f.suffix = us;
      }
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
    const int olt = m_wind_altitude > 0 ? LV_ISOBARIC : LV_ABOV_GND;
    const int olv = m_wind_altitude > 0 ? m_wind_altitude : 10;
    recordPairAt(GRB_WIND_VX, GRB_WIND_VY, olt, olv, &ru, &rv, &ownW);
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
      m_layer->setOverlay(g, ocpn::qtui::gribmaps::Wind, 0, 40);  // m/s
      if (ownW) { delete ru; delete rv; }
    } else {
      m_layer->clearOverlay();
    }
  } else {
    // The type's wx palette + normalization range (grib_color_maps).
    const TypeSpec* spec = nullptr;
    for (const TypeSpec& ts : kTypes)
      if (m_overlay_key == QLatin1String(ts.key)) spec = &ts;
    bool ownO = false;
    GribRecord* r =
        spec ? recordAt(spec->dataType, spec->levelType, spec->level, &ownO)
             : nullptr;
    if (r && r->isOk()) {
      auto g = scalarFrom(r);
      ocpn::qtui::gribmaps::Map map = ocpn::qtui::gribmaps::Generic;
      double mn = 0, mx = 0;
      if (!ocpn::qtui::gribmaps::rampForKey(m_overlay_key, &map, &mn, &mx)) {
        // Unknown key: generic ramp over the field's own range.
        float top = 0;
        for (float v : g.v)
          if (!std::isnan(v)) top = qMax(top, v);
        mx = top;
      }
      m_layer->setOverlay(g, map, mn, mx);
    } else {
      m_layer->clearOverlay();
    }
    if (ownO) delete r;
  }

  emit typesChanged();

  if (!m_show_wind) {
    m_layer->clearGrid();
    return;
  }
  // 10 m wind, vector-interpolated to the exact display time (tier 3).
  bool ownWind = false;
  GribRecord* ru = nullptr;
  GribRecord* rv = nullptr;
  const int wlt = m_wind_altitude > 0 ? LV_ISOBARIC : LV_ABOV_GND;
  const int wlv = m_wind_altitude > 0 ? m_wind_altitude : 10;
  recordPairAt(GRB_WIND_VX, GRB_WIND_VY, wlt, wlv, &ru, &rv, &ownWind);
  if (!ru || !rv || !ru->isOk() || !rv->isOk()) {
    m_layer->clearGrid();
    return;
  }
  ocpn::qtui::GribWindLayer::WindGrid g;
  g.ni = ru->getNi();
  g.nj = ru->getNj();
  g.lon0 = ru->getX(0);
  if (g.lon0 >= 180.0) g.lon0 -= 360.0;
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
  // Self-test fingerprint: the interpolated grid's mean U proves each
  // timeline move pushes genuinely different data (the scrub-saga proof).
  if (qEnvironmentVariableIsSet("OCPN_GRIB_SELFTEST")) {
    double sum = 0;
    int n = 0;
    for (float u : g.u)
      if (!std::isnan(u)) { sum += u; ++n; }
    qWarning("grib: PUSH epoch=%lld lon0=%.1f meanU=%.3f n=%d",
             (long long)displayEpoch(), g.lon0, n ? sum / n : 0.0, n);
  }
  {
    // Beyond-forecast feedback: clamped (stale) data renders dimmed.
    qint64 raw = m_step_times.isEmpty() ? 0 : m_step_times[m_time_index];
    if (m_timeline) {
      const QDateTime t2 = m_timeline->property("displayTime").toDateTime();
      if (t2.isValid()) raw = t2.toSecsSinceEpoch();
    }
    const bool beyond = !m_step_times.isEmpty() &&
                        (raw < m_step_times.first() - 1800 ||
                         raw > m_step_times.last() + 1800);
    m_layer->setOpacity(beyond ? 0.35 : 1.0);
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
    const double qlon =
        (ru->getX(0) >= 180.0 && lon < 0) ? lon + 360.0 : lon;
    // getInterpolatedValue handles bilinear sampling + grid bounds.
    const double u = ru->getInterpolatedValue(qlon, lat, true);
    const double v = rv->getInterpolatedValue(qlon, lat, true);
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

QString GribContext::requestGrib(const QString& model, double resolution,
                                 int intervalHours, double north,
                                 double south, double east, double west,
                                 int days, bool wind, bool pressure,
                                 bool waves, bool precip) {
  // The saildocs request line (wx request-dialog matrix, tier 6), e.g.
  //   send GFS:42N,38N,10W,2W|0.25,0.25|0,3..96|WIND,PRMSL
  auto coord = [](double v, char pos, char neg) {
    return QStringLiteral("%1%2")
        .arg(std::fabs(v), 0, 'f', 1)
        .arg(v >= 0 ? pos : neg);
  };
  QStringList params;
  if (wind) params << QStringLiteral("WIND");
  if (pressure) params << QStringLiteral("PRMSL");
  // Waves/rain ride GFS only on saildocs.
  if (waves && model == QLatin1String("GFS"))
    params << QStringLiteral("HTSGW,WVDIR");
  if (precip && model == QLatin1String("GFS"))
    params << QStringLiteral("APCP");
  if (params.isEmpty()) params << QStringLiteral("WIND,PRMSL");
  const QString res = QString::number(resolution, 'f', 2);
  const QString body =
      QStringLiteral("send %1:%2,%3,%4,%5|%6,%6|0,%7..%8|%9")
          .arg(model, coord(north, 'N', 'S'), coord(south, 'N', 'S'),
               coord(west, 'E', 'W'), coord(east, 'E', 'W'), res)
          .arg(intervalHours)
          .arg(qBound(24, days * 24, 384))
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
    layer->setVisible(true);  // recover any persisted-hidden state
    layer->setZOrder(1450);  // over charts/grid, under the nav overlays
    m_ctx->setLayer(layer);
    host.registerLayer(layer);  // compositor takes ownership
  }
  if (host.registerHud)
    host.registerHud(
        QUrl(QStringLiteral("qrc:/grib_plugin/CursorDataHud.qml")), m_ctx);
  m_ctx->notifyToolbar = host.toolbarStateChanged;
  if (host.registerToolbarAction)
    host.registerToolbarAction(
        QStringLiteral("🌬"),
        QStringLiteral("GRIB weather  (hold for controls)"),
        [this] {  // click: weather on/off, state shown on the chip
          if (m_ctx) m_ctx->setMasterEnabled(!m_ctx->masterEnabled());
        },
        nullptr,  // flyout is hosted in the toolbar bulge (registerToolbarFlyout)
        [this] { return m_ctx && m_ctx->masterEnabled(); });
  // The flyout (toggle chips) grows out of the 🌬 tool's own backdrop.
  if (host.registerToolbarFlyout)
    host.registerToolbarFlyout(
        QUrl(QStringLiteral("qrc:/grib_plugin/Flyout.qml")), m_ctx);
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
