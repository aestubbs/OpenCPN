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
 * Implement chart_config.h.
 */

#include "chart_config.h"

#include "config_store.h"

namespace ocpn::qtui {

ChartConfig& ChartConfig::instance() {
  static ChartConfig s;
  return s;
}

ChartConfig::ChartConfig() {
  ConfigStore& c = ConfigStore::instance();
  m_chart_info = c.getBool("vchart/chartInfo", m_chart_info);
  m_data_quality = c.getBool("vchart/dataQuality", m_data_quality);
  m_buoy_light_labels = c.getBool("vchart/buoyLightLabels", m_buoy_light_labels);
  m_light_descriptions =
      c.getBool("vchart/lightDescriptions", m_light_descriptions);
  m_ext_light_sectors = c.getBool("vchart/extLightSectors", m_ext_light_sectors);
  m_national_text = c.getBool("vchart/nationalText", m_national_text);
  m_important_text = c.getBool("vchart/importantTextOnly", m_important_text);
  m_declutter = c.getBool("vchart/declutterText", m_declutter);
  m_reduced_detail = c.getBool("vchart/reducedDetail", m_reduced_detail);
  m_super_scamin = c.getBool("vchart/superScamin", m_super_scamin);
  m_graphics_style = c.getInt("vchart/graphicsStyle", m_graphics_style);
  m_boundary_style = c.getInt("vchart/boundaryStyle", m_boundary_style);
  m_colour_count = c.getInt("vchart/colourCount", m_colour_count);
  m_shallow = c.getDouble("vchart/shallowContour", m_shallow);
  m_safety = c.getDouble("vchart/safetyContour", m_safety);
  m_deep = c.getDouble("vchart/deepContour", m_deep);
  m_cm93_detail = c.getInt("vchart/cm93Detail", m_cm93_detail);
  m_cm93_dx = c.getDouble("vchart/cm93OffsetX", m_cm93_dx);
  m_cm93_dy = c.getDouble("vchart/cm93OffsetY", m_cm93_dy);
  m_cull_factor = c.getDouble("vchart/chartCullFactor", m_cull_factor);
}

// guard, store, persist, notify.
#define OCPN_VC_SET(member, value, key, putter) \
  if ((member) == (value)) return;              \
  (member) = (value);                           \
  ConfigStore::instance().putter(key, value);   \
  emit changed();

void ChartConfig::setChartInfoObjects(bool v) {
  OCPN_VC_SET(m_chart_info, v, "vchart/chartInfo", setBool)
}
void ChartConfig::setDataQuality(bool v) {
  OCPN_VC_SET(m_data_quality, v, "vchart/dataQuality", setBool)
}
void ChartConfig::setBuoyLightLabels(bool v) {
  OCPN_VC_SET(m_buoy_light_labels, v, "vchart/buoyLightLabels", setBool)
}
void ChartConfig::setLightDescriptions(bool v) {
  OCPN_VC_SET(m_light_descriptions, v, "vchart/lightDescriptions", setBool)
}
void ChartConfig::setExtendedLightSectors(bool v) {
  OCPN_VC_SET(m_ext_light_sectors, v, "vchart/extLightSectors", setBool)
}
void ChartConfig::setNationalText(bool v) {
  OCPN_VC_SET(m_national_text, v, "vchart/nationalText", setBool)
}
void ChartConfig::setImportantTextOnly(bool v) {
  OCPN_VC_SET(m_important_text, v, "vchart/importantTextOnly", setBool)
}
void ChartConfig::setDeclutterText(bool v) {
  OCPN_VC_SET(m_declutter, v, "vchart/declutterText", setBool)
}
void ChartConfig::setReducedDetailSmallScale(bool v) {
  OCPN_VC_SET(m_reduced_detail, v, "vchart/reducedDetail", setBool)
}
void ChartConfig::setSuperScamin(bool v) {
  OCPN_VC_SET(m_super_scamin, v, "vchart/superScamin", setBool)
}
void ChartConfig::setGraphicsStyle(int v) {
  OCPN_VC_SET(m_graphics_style, v, "vchart/graphicsStyle", setInt)
}
void ChartConfig::setBoundaryStyle(int v) {
  OCPN_VC_SET(m_boundary_style, v, "vchart/boundaryStyle", setInt)
}
void ChartConfig::setColourCount(int v) {
  OCPN_VC_SET(m_colour_count, v, "vchart/colourCount", setInt)
}
void ChartConfig::setShallowContour(double v) {
  OCPN_VC_SET(m_shallow, v, "vchart/shallowContour", setDouble)
}
void ChartConfig::setSafetyContour(double v) {
  OCPN_VC_SET(m_safety, v, "vchart/safetyContour", setDouble)
}
void ChartConfig::setDeepContour(double v) {
  OCPN_VC_SET(m_deep, v, "vchart/deepContour", setDouble)
}
void ChartConfig::setCm93Detail(int v) {
  OCPN_VC_SET(m_cm93_detail, v, "vchart/cm93Detail", setInt)
}
void ChartConfig::setCm93OffsetX(double v) {
  OCPN_VC_SET(m_cm93_dx, v, "vchart/cm93OffsetX", setDouble)
}
void ChartConfig::setCm93OffsetY(double v) {
  OCPN_VC_SET(m_cm93_dy, v, "vchart/cm93OffsetY", setDouble)
}
void ChartConfig::setChartCullFactor(double v) {
  OCPN_VC_SET(m_cull_factor, v, "vchart/chartCullFactor", setDouble)
}

#undef OCPN_VC_SET

}  // namespace ocpn::qtui
