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
 * The GRIB Qt plugin (P4.5, v1 of the grib_pi port): open a local GRIB
 * 1/2 file through the vendored zyGrib decode core (GribReader --
 * compiled as-is, wx-string surface and all), step its timeline, and
 * render the 10 m wind field as a world-anchored arrow Layer through the
 * opencpn_qt_toolkit -- the first Layer-contributing plugin. v1 scope:
 * wind arrows only; pressure isobars / precip / waves / the request
 * builder follow.
 */

#ifndef OCPN_QT_GRIB_PLUGIN_H_
#define OCPN_QT_GRIB_PLUGIN_H_

#include <QObject>
#include <QString>
#include <QMap>
#include <QStringList>
#include <QVariantList>
#include <QUrl>

#include "../ocpn_qt_plugin.h"

class GribReader;

namespace ocpn::qtui {
class GribWindLayer;
}

class GribContext : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString fileName READ fileName NOTIFY gribChanged)
  Q_PROPERTY(QStringList timeSteps READ timeSteps NOTIFY gribChanged)
  Q_PROPERTY(int timeIndex READ timeIndex WRITE setTimeIndex NOTIFY
                 timeChanged)
  Q_PROPERTY(bool showWind READ showWind WRITE setShowWind NOTIFY timeChanged)
  Q_PROPERTY(bool showPressure READ showPressure WRITE setShowPressure NOTIFY
                 timeChanged)
  // Per-type display catalog: [{key, label, available, shown}] -- the
  // control bar builds its toggles from this (wx 13-type set, tier 1).
  Q_PROPERTY(QVariantList dataTypes READ dataTypes NOTIFY typesChanged)
  // The colour-mapped overlay field key ("" = none; wx OverlayMap).
  Q_PROPERTY(QString overlayKey READ overlayKey WRITE setOverlayKey NOTIFY
                 typesChanged)
  Q_PROPERTY(QString status READ status NOTIFY gribChanged)
  // The on-canvas control bar's visibility (toolbar 🌬 toggles it).
  Q_PROPERTY(bool controlsVisible READ controlsVisible WRITE
                 setControlsVisible NOTIFY controlsChanged)

public:
  explicit GribContext(QObject* timeline = nullptr,
                       QObject* parent = nullptr);
  ~GribContext() override;

  QString fileName() const { return m_file; }
  QStringList timeSteps() const { return m_steps; }
  int timeIndex() const { return m_time_index; }
  void setTimeIndex(int i);
  bool showWind() const { return m_show_wind; }
  void setShowWind(bool on);
  bool showPressure() const { return m_show_pressure; }
  void setShowPressure(bool on);
  QString status() const { return m_status; }

  Q_INVOKABLE void openFile(const QUrl& url);
  bool controlsVisible() const { return m_controls_visible; }
  void setControlsVisible(bool on) {
    if (on == m_controls_visible) return;
    m_controls_visible = on;
    Q_EMIT controlsChanged();
  }
  void toggleControls() { setControlsVisible(!m_controls_visible); }

  /** Wind/pressure at a position for the cursor readout: a formatted
   *  one-liner ("12.4 kn @ 215°   1013 hPa"), empty when off-grid or no
   *  file is loaded. Bilinear interpolation over the current timestep. */
  Q_INVOKABLE QString readoutAt(double lat, double lon) const;
  QVariantList dataTypes() const;
  Q_INVOKABLE void setTypeShown(const QString& key, bool on);
  QString overlayKey() const { return m_overlay_key; }
  void setOverlayKey(const QString& k);

  /** Compose a saildocs GRIB request for the given bounds and open the
   *  user's mail client (mailto:). Returns the request body line. */
  Q_INVOKABLE QString requestGrib(double north, double south, double east,
                                  double west, int days, bool wind,
                                  bool pressure, bool waves, bool precip);

  /** The Layer (owned by the compositor once registered). */
  ocpn::qtui::GribWindLayer* layer() const { return m_layer; }
  void setLayer(ocpn::qtui::GribWindLayer* l) { m_layer = l; }

Q_SIGNALS:
  void gribChanged();
  void timeChanged();
  void controlsChanged();
  void typesChanged();

private Q_SLOTS:
  // The app time bar moved: snap to the nearest GRIB timestep.
  void onTimelineChanged();

private:
  void pushToLayer();

  GribReader* m_reader = nullptr;
  ocpn::qtui::GribWindLayer* m_layer = nullptr;  // compositor-owned
  QString m_file;
  QStringList m_steps;
  QList<long long> m_step_times;
  int m_time_index = 0;
  bool m_show_wind = true;
  bool m_show_pressure = true;
  QString m_overlay_key;
  QMap<QString, bool> m_type_shown;     // persisted per-type toggles
  QMap<QString, bool> m_type_available; // present in the loaded file
  bool m_controls_visible = false;
  QObject* m_timeline = nullptr;
  QString m_status;
};

class GribPlugin : public QObject, public ocpn::qtui::OcpnQtPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID OcpnQtPlugin_iid)
  Q_INTERFACES(ocpn::qtui::OcpnQtPlugin)

public:
  QString name() const override { return QStringLiteral("GRIB"); }
  QString version() const override { return QStringLiteral("1.0"); }
  QString description() const override {
    return QStringLiteral("GRIB 1/2 weather files: wind-field overlay (v1).");
  }
  bool init(const ocpn::qtui::OcpnQtPluginHost& host) override;
  void deinit() override;

private:
  GribContext* m_ctx = nullptr;
};

#endif  // OCPN_QT_GRIB_PLUGIN_H_
