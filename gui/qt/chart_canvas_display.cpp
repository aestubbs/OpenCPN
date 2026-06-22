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
 * Zoom/scale, display-category and S-52 show-toggle settings for ChartCanvas.
 *
 * Part of the ChartCanvas implementation, split out of chart_canvas.cpp. See
 * chart_canvas_internal.h for the shared include block and rationale.
 */

#include "chart_canvas.h"

#include "chart_canvas_internal.h"

namespace ocpn::qtui {

void ChartCanvas::zoomIn() {
  const int w = static_cast<int>(width());
  const int h = static_cast<int>(height());
  m_viewport->zoomAt(w / 2.0, h / 2.0, 1.4, w, h);
}

void ChartCanvas::zoomOut() {
  const int w = static_cast<int>(width());
  const int h = static_cast<int>(height());
  m_viewport->zoomAt(w / 2.0, h / 2.0, 1.0 / 1.4, w, h);
}

void ChartCanvas::fitWorld() {
  if (!m_boundary_provider) return;
  const double n = m_boundary_provider->northLat();
  const double s = m_boundary_provider->southLat();
  const double e = m_boundary_provider->eastLon();
  const double w = m_boundary_provider->westLon();
  if (e <= w || n <= s) return;
  m_viewport->setCenter((n + s) / 2.0, (e + w) / 2.0);
  const double cw = width() > 0 ? width() : 1024.0;
  const double ch = height() > 0 ? height() : 720.0;
  m_viewport->setScale(std::min(cw / (e - w), ch / (n - s)) * 0.9);
}

void ChartCanvas::setScaleDenominator(double n) {
  if (!m_viewport) return;
  // Free-form like wx (mui_bar.cpp OnScaleSelected): clamp to a sane range.
  n = std::clamp(n, 1000.0, 3.0e6);
  // displayScaleN(scale, lat) = K / scale, so invert: scale = K / N, using the
  // same cos(centre-lat) factor so the entered 1:N matches the readout.
  const double clat =
      std::max(0.05, std::cos(m_viewport->centerLat() * M_PI / 180.0));
  const double scale = 111320.0 * clat * 3.78 * 1000.0 / n;
  m_viewport->setScale(scale);  // Viewport::changed -> update + viewChanged
}

void ChartCanvas::applyDisplaySettings(
    S52VectorChartProvider* provider) const {
  if (!provider) return;
  provider->setDisplayCategory(m_display_category);
  provider->setHiddenClasses(
      QSet<QString>(m_hidden_classes.cbegin(), m_hidden_classes.cend()));
  provider->setShowSoundings(m_show_soundings);
  provider->setShowText(m_show_text);
  provider->setShowLights(m_show_lights);
  provider->setShowBuoys(m_show_buoys);
  provider->setDetailScale(m_detail_scale);
  provider->setDeclutter(ChartConfig::instance().declutterText());  // P2.23a
  // Sounding display. The depth unit applies here so freshly-decoded SOUNDG
  // figures format correctly (a depth-unit *change* re-decodes via the
  // DisplayConfig handler, since obstruction soundings are baked by s52plib).
  // Safety-depth emphasis and the ENC sounding-size slider are live render-time
  // re-rasters; the slider's -5..+5 maps to 0.5x..1.5x (1 + 0.1*f), mirroring
  // wx m_SoundingsScaleFactor.
  provider->setDepthUnit(DisplayConfig::instance().depthUnit());
  provider->setSafetyDepth(OwnShipConfig::instance().safetyDepth());
  provider->setSoundingScale(
      1.0 + 0.1 * UIConfig::instance().encSoundingScaleFactor());
  // Over-scale hatch threshold: above the quilt's normal overzoom band
  // (m_overzoom_k), floored at wx's ~4x, so the hatch only marks genuine
  // overscale -- not the routine overzoom the quilt does after autoscaling.
  provider->setOverscaleThreshold(std::max(4.0, m_overzoom_k));
}

void ChartCanvas::setDetailScale(double n) {
  if (n <= 0.0 || n == m_detail_scale) return;
  m_detail_scale = n;
  ConfigStore::instance().setInt("display/detailScale", static_cast<int>(n));
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setDetailScale(n);
  emit detailScaleChanged();
  update();
}

void ChartCanvas::setOverzoomFactor(double k) {
  k = std::clamp(k, 1.0, 5.0);
  if (k == m_overzoom_k) return;
  m_overzoom_k = k;
  ConfigStore::instance().setDouble("display/overzoomFactor", k);
  // The over-zoom factor changes which charts the quilt selects, so re-run the
  // per-view selection (loads/evicts as needed). Cheap; only on a settings edit.
  // It also sets the over-scale hatch threshold (max(4,k)), so push that to the
  // resident providers too.
  const double thr = std::max(4.0, m_overzoom_k);
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setOverscaleThreshold(thr);
  if (!m_catalog.isEmpty()) updateVisibleCells();
  emit overzoomFactorChanged();
  update();
}

void ChartCanvas::setDisplayCategory(int cat) {
  if (cat == m_display_category) return;
  m_display_category = cat;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setDisplayCategory(cat);
  if (m_layer_config) m_layer_config->setValue("display/category", cat);
  emit displayCategoryChanged();
  update();
}

// The wx SetAnchorOn category set (s52plib.cpp:11270).
static const char* kAnchorClasses[] = {"ACHBRT", "ACHARE", "CBLSUB",
                                       "PIPARE", "PIPSOL", "TUNNEL",
                                       "SBDARE"};

void ChartCanvas::setShowEncAnchoring(bool on) {
  if (on == m_show_enc_anchoring) return;
  m_show_enc_anchoring = on;
  QStringList classes = m_hidden_classes;
  for (const char* c : kAnchorClasses) {
    if (on)
      classes.removeAll(QLatin1String(c));
    else if (!classes.contains(QLatin1String(c)))
      classes.append(QLatin1String(c));
  }
  if (m_layer_config)
    m_layer_config->setValue("display/showEncAnchoring", on);
  setHiddenObjectClasses(classes);  // re-renders + notifies
}

void ChartCanvas::scaleChartStep(int dir) {
  if (!m_viewport || dir == 0) return;
  const double clat = m_viewport->centerLat();
  const double clon = m_viewport->centerLon();
  const double curN = displayScaleN(m_viewport->scale(), clat);
  // Distinct native scales of catalogued cells charting the view centre.
  QList<int> scales;
  for (auto it = m_catalog.cbegin(); it != m_catalog.cend(); ++it) {
    const CellExtent& c = it.value();
    if (c.nativeScale > 0 && c.navFeatures > 0 && c.covers(clat, clon) &&
        !scales.contains(c.nativeScale))
      scales.append(c.nativeScale);
  }
  if (scales.isEmpty()) return;
  std::sort(scales.begin(), scales.end());
  int target = 0;
  if (dir > 0) {  // larger scale = finer = smaller 1:N
    for (int i = scales.size() - 1; i >= 0; --i)
      if (scales[i] < curN * 0.9) { target = scales[i]; break; }
  } else {  // smaller scale = coarser = bigger 1:N
    for (int i = 0; i < scales.size(); ++i)
      if (scales[i] > curN * 1.1) { target = scales[i]; break; }
  }
  if (target <= 0) return;
  // Same autoscale math as selectChart (scale = K*cos(lat)/N).
  constexpr double kK = 111320.0 * 3.78 * 1000.0;
  const double cl = std::max(0.05, std::cos(clat * M_PI / 180.0));
  m_viewport->setScale(kK * cl / target);
  emit viewChanged();
  update();
}

void ChartCanvas::setHiddenObjectClasses(const QStringList& classes) {
  QStringList norm = classes;
  norm.removeDuplicates();
  norm.sort();
  if (norm == m_hidden_classes) return;
  m_hidden_classes = norm;
  const QSet<QString> hidden(norm.cbegin(), norm.cend());
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setHiddenClasses(hidden);
  if (m_layer_config)
    m_layer_config->setValue("display/hiddenClasses", norm.join(','));
  emit hiddenObjectClassesChanged();
  update();
}

QVariantList ChartCanvas::s57ClassCatalog() const {
  QVariantList out;
  const S57Dictionary& dict = S57Dictionary::instance();
  for (const QString& acr : dict.classAcronyms()) {
    QVariantMap row;
    row["acronym"] = acr;
    const QString desc = dict.className(acr);
    row["description"] = desc.isEmpty() ? acr : desc;
    out.append(row);
  }
  return out;
}

void ChartCanvas::setShowSoundings(bool on) {
  if (on == m_show_soundings) return;
  m_show_soundings = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowSoundings(on);
  if (m_layer_config) m_layer_config->setValue("display/soundings", on);
  emit showSoundingsChanged();
  update();
}

void ChartCanvas::setShowText(bool on) {
  if (on == m_show_text) return;
  m_show_text = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowText(on);
  if (m_layer_config) m_layer_config->setValue("display/text", on);
  emit showTextChanged();
  update();
}

void ChartCanvas::setShowLights(bool on) {
  if (on == m_show_lights) return;
  m_show_lights = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowLights(on);
  if (m_layer_config) m_layer_config->setValue("display/lights", on);
  emit showLightsChanged();
  update();
}

void ChartCanvas::setShowBuoys(bool on) {
  if (on == m_show_buoys) return;
  m_show_buoys = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowBuoys(on);
  if (m_layer_config) m_layer_config->setValue("display/buoys", on);
  emit showBuoysChanged();
  update();
}

void ChartCanvas::setDemoMode(bool on) {
  if (on == m_demo_mode) return;
  m_demo_mode = on;
  ConfigStore::instance().setBool("display/demoMode", on);  // opt-in, persisted
  // Demo = the Hakefjord NMEA-log replay (a self-contained sample feed);
  // live = the real model fed by user connections. The two are mutually
  // exclusive: in demo the live model poll is stopped (real feeds are not
  // shown); in live the replay is stopped (no Hakefjord).
  if (m_nav_provider) m_nav_provider->setLive(!on);
  if (m_demo_provider) m_demo_provider->setRunning(on);
  if (m_model_provider) m_model_provider->setModelPolling(!on);
  m_live_centered = false;  // recentre on the new source's first fix
  // Track recording is left to the user (toolbar toggle, #29); it records
  // off whichever own-ship fix is active.
  emit demoModeChanged();
  update();
}


}  // namespace ocpn::qtui
