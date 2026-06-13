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
#include <QTimer>
#include <QVector>

#include "layer.h"       // opencpn_qt_toolkit
#include "sg_builder.h"  // opencpn_qt_toolkit
#include "viewport.h"    // opencpn_qt_toolkit

#include "grib_color_maps.h"

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
    emit dirty();
  }
  void clearGrid() {
    m_grid = WindGrid{};
    emit dirty();
  }

  /** Scalar field grid (pressure, gust, rain...). */
  struct ScalarGrid {
    int ni = 0, nj = 0;
    double lon0 = 0, lat0 = 0, di = 0, dj = 0;
    QVector<float> v;  // row-major; NaN = undefined
  };
  void setIsobars(const ScalarGrid& g) {
    m_isobars = g;
    emit dirty();
  }
  void clearIsobars() {
    m_isobars = ScalarGrid{};
    emit dirty();
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
    emit dirty();
  }
  void clearArrows(const QString& key) {
    m_arrows.remove(key);
    emit dirty();
  }

  /** The colour-mapped overlay raster (one at a time, wx OverlayMap):
   *  the grid is rasterized at grid resolution and GPU linear filtering
   *  smooths it across the chart. Colours come from the wx grib_pi
   *  palettes -- `map` picks the ramp, [rampMin, rampMax] the value
   *  range it spans (the type's wx GetMin/GetMax, in the grid's own
   *  units). */
  void setOverlay(const ScalarGrid& g, gribmaps::Map map, double rampMin,
                  double rampMax) {
    m_overlay = g;
    m_overlay_map = map;
    m_overlay_min = rampMin;
    m_overlay_max = rampMax;
    m_overlay_dirty = true;
    emit dirty();
  }
  void clearOverlay() {
    m_overlay = ScalarGrid{};
    m_overlay_dirty = true;
    emit dirty();
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
    emit dirty();
  }
  void clearNumbers(const QString& key) {
    m_numbers.remove(key);
    emit dirty();
  }

  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

  /** Particle animation over the wind field (wx parity, tier 4): ~600
   *  particles advected by the grid, drawn as fading streaks; a 33 ms
   *  timer drives re-render while enabled. */
  void setParticlesEnabled(bool on);
  bool particlesEnabled() const { return m_particles_on; }
  void setParticleDensity(int d) {  // 1..10 -> 150..1500 particles
    m_particle_count = 150 * qBound(1, d, 10);
    m_particles.clear();
    emit dirty();
  }
  void setOverlayAlpha(int a) {
    m_overlay_alpha = qBound(0, a, 255);
    emit dirty();
  }

private:
  void drawIsobars(SgBuilder& b);
  double worldPerPx() const;
  void drawArrows(SgBuilder& b);
  void drawNumbers(SgBuilder& b);
  const Viewport* m_vp = nullptr;
  double m_last_scale = 0;
  WindGrid m_grid;
  ScalarGrid m_isobars;
  ScalarGrid m_overlay;
  gribmaps::Map m_overlay_map = gribmaps::Generic;
  double m_overlay_min = 0;
  double m_overlay_max = 0;
  bool m_overlay_dirty = false;
  QSGNode* m_overlay_node = nullptr;
  QMap<QString, ArrowField> m_arrows;
  QMap<QString, NumberField> m_numbers;
  QHash<QString, QImage> m_label_cache;  // numbers raster cache (bounded)

  struct Particle {
    double lon = 0, lat = 0;
    double plon = 0, plat = 0;  // previous position (trail)
    int age = 0;
  };
  void stepParticles();
  void drawParticles(SgBuilder& b);
  bool m_particles_on = false;
  int m_particle_count = 750;
  int m_overlay_alpha = 110;
  QVector<Particle> m_particles;
  QTimer* m_particle_timer = nullptr;
  QSGNode* m_root = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_GRIB_WIND_LAYER_H_
