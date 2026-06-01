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
 * TideGraphViewModel -- the selected tide/current station behind the docked
 * bottom graph drawer (P3.14 phase F). A chart click selects a station
 * (ChartCanvas::pickTideStationAt -> select()); the QML graph Canvas pulls the
 * station's curve for the current pan window via samples()/events(), and reads
 * the value under the fixed time marker via valueAtMarker (recomputed on
 * TimeController::timeChanged). Tide stations plot a height curve (HW/LW
 * markers); current stations a signed velocity curve (flood +/ebb -). Values
 * are returned in the user's chosen units (DisplayConfig).
 */

#ifndef OCPN_QT_TIDE_GRAPH_VIEW_MODEL_H_
#define OCPN_QT_TIDE_GRAPH_VIEW_MODEL_H_

#include <QObject>
#include <QString>
#include <QVariantList>

namespace ocpn::qtui {

class TideGraphViewModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool valid READ valid NOTIFY changed)
  Q_PROPERTY(QString stationName READ stationName NOTIFY changed)
  Q_PROPERTY(bool isCurrent READ isCurrent NOTIFY changed)
  Q_PROPERTY(QString unitLabel READ unitLabel NOTIFY changed)
  Q_PROPERTY(double minValue READ minValue NOTIFY changed)
  Q_PROPERTY(double maxValue READ maxValue NOTIFY changed)
  Q_PROPERTY(double valueAtMarker READ valueAtMarker NOTIFY markerChanged)
  Q_PROPERTY(
      QString valueAtMarkerText READ valueAtMarkerText NOTIFY markerChanged)

public:
  explicit TideGraphViewModel(QObject* parent = nullptr);

  bool valid() const { return m_idx >= 0; }
  QString stationName() const { return m_name; }
  bool isCurrent() const { return m_is_current; }
  QString unitLabel() const { return m_unit_label; }
  double minValue() const { return m_min; }
  double maxValue() const { return m_max; }
  double valueAtMarker() const { return m_marker_value; }
  QString valueAtMarkerText() const { return m_marker_text; }

  /** Select a station by its ptcmgr index (from a chart hit-test). */
  void select(int idx);
  Q_INVOKABLE void clear();

  /** n sampled values (user units) across [startMs, endMs] -- a flat
   *  QVariantList<double>; x is reconstructed from index by the caller. */
  Q_INVOKABLE QVariantList samples(double startMs, double endMs, int n) const;
  /** Turning-point events in the window as { t (ms), v (user units), type } --
   *  type "HW"/"LW" for tide, "Flood"/"Ebb" for current. */
  Q_INVOKABLE QVariantList events(double startMs, double endMs) const;
  /** Current set/drift arrows at stepMins intervals across [startMs, endMs] as
   *  { t (ms), v (signed user-unit speed -> the y on the curve), dir (compass
   *  set, degrees) }. Empty for tide stations. */
  Q_INVOKABLE QVariantList currentArrows(double startMs, double endMs,
                                         double stepMins) const;

Q_SIGNALS:
  void changed();        // station / unit / value range
  void markerChanged();  // value under the display-time marker

private:
  void recomputeRange();   // y-axis min/max (cached, stable across pans)
  void recomputeMarker();  // value at TimeController.displayTime
  double toUser(float raw) const;  // metres->height-unit or knots->speed-unit

  int m_idx = -1;
  QString m_name;
  bool m_is_current = false;
  QString m_unit_label;
  double m_min = 0.0;
  double m_max = 1.0;
  double m_marker_value = 0.0;
  QString m_marker_text;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_TIDE_GRAPH_VIEW_MODEL_H_
