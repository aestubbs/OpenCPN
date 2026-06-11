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
 * GribWindLayer (P4.5 v1): the 10 m wind field as world-anchored arrows --
 * the first plugin-contributed Layer through the opencpn_qt_toolkit.
 * Holds a U/V grid snapshot pushed by the GribContext per timestep;
 * arrows are speed-coloured (Beaufort-ish ramp) and grid-decimated by
 * zoom so density stays readable.
 */

#ifndef OCPN_QT_GRIB_WIND_LAYER_H_
#define OCPN_QT_GRIB_WIND_LAYER_H_

#include <QHash>
#include <QImage>
#include <QMap>
#include <QVector>

#include "layer.h"       // opencpn_qt_toolkit
#include "sg_builder.h"  // opencpn_qt_toolkit
#include "viewport.h"    // opencpn_qt_toolkit

namespace ocpn::qtui {

class GribWindLayer : public Layer {
  Q_OBJECT
public:
  using Layer::Layer;

  /** Wire the viewport: barbs render SCREEN-FIXED (rebuild on zoom,
   *  pure transform on pan -- the NavLayer pattern). */
  void setViewport(const Viewport* vp);
  QString id() const override { return QStringLiteral("plugin.grib.wind"); }
  QString name() const override { return QStringLiteral("GRIB wind"); }
  Anchor anchor() const override { return WorldAnchored; }

  struct WindGrid {
    int ni = 0, nj = 0;
    double lon0 = 0, lat0 = 0, di = 0, dj = 0;
    QVector<float> u, v;  // m/s, row-major (j * ni + i); NaN = undefined
  };
  void setGrid(const WindGrid& g) {
    m_grid = g;
    Q_EMIT dirty();
  }
  void clearGrid() {
    m_grid = WindGrid{};
    Q_EMIT dirty();
  }

  /** Scalar field grid (pressure, gust, rain...). */
  struct ScalarGrid {
    int ni = 0, nj = 0;
    double lon0 = 0, lat0 = 0, di = 0, dj = 0;
    QVector<float> v;  // row-major; NaN = undefined
  };
  void setIsobars(const ScalarGrid& g) {
    m_isobars = g;
    Q_EMIT dirty();
  }
  void clearIsobars() {
    m_isobars = ScalarGrid{};
    Q_EMIT dirty();
  }

  /** A direction-arrow vector field (waves, current): either u/v
   *  components or direction-degrees + magnitude. */
  struct ArrowField {
    WindGrid grid;          // u/v in grid.u/.v OR dir-deg in u, mag in v
    bool dirMag = false;    // true: u = FROM-direction degrees, v = magnitude
    QColor color;
    QString unitSuffix;     // for the magnitude number under the arrow
    double unitFactor = 1;  // applied to magnitude for display
    bool showNumber = true;
  };
  void setArrows(const QString& key, const ArrowField& f) {
    m_arrows[key] = f;
    Q_EMIT dirty();
  }
  void clearArrows(const QString& key) {
    m_arrows.remove(key);
    Q_EMIT dirty();
  }

  /** A scalar field rendered as NUMBERS at grid points. */
  struct NumberField {
    ScalarGrid grid;
    QColor color;
    QString suffix;
    double factor = 1;    // display = value*factor + offset
    double offset = 0;
    int decimals = 0;
  };
  void setNumbers(const QString& key, const NumberField& f) {
    m_numbers[key] = f;
    Q_EMIT dirty();
  }
  void clearNumbers(const QString& key) {
    m_numbers.remove(key);
    Q_EMIT dirty();
  }

  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

private:
  void drawIsobars(SgBuilder& b);
  double worldPerPx() const;
  void drawArrows(SgBuilder& b);
  void drawNumbers(SgBuilder& b);
  const Viewport* m_vp = nullptr;
  double m_last_scale = 0;
  WindGrid m_grid;
  ScalarGrid m_isobars;
  QMap<QString, ArrowField> m_arrows;
  QMap<QString, NumberField> m_numbers;
  QHash<QString, QImage> m_label_cache;  // numbers raster cache (bounded)
  QSGNode* m_root = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_GRIB_WIND_LAYER_H_
