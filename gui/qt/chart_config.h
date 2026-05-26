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
 * ChartConfig -- the extended S-52 vector-chart display preferences from the
 * wx Options > Charts > Vector Chart Display sub-panel that the Qt s52
 * provider does not yet consume. A QML_SINGLETON, persisted via ConfigStore,
 * referenced in QML as `ChartConfig`.
 *
 * The four toggles the provider DOES honour today (soundings / text / lights /
 * buoys) and the display category stay on ChartCanvas (the `chart` object),
 * wired live. Everything here is persisted now and applied once the provider /
 * s52plib viewing-group plumbing lands (see the P3.6 inventory).
 */

#ifndef OCPN_QT_CHART_CONFIG_H_
#define OCPN_QT_CHART_CONFIG_H_

#include <QObject>
#include <QQmlEngine>

namespace ocpn::qtui {

class ChartConfig : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  // Cartography / text detail.
  Q_PROPERTY(bool chartInfoObjects READ chartInfoObjects WRITE
                 setChartInfoObjects NOTIFY changed)
  Q_PROPERTY(bool buoyLightLabels READ buoyLightLabels WRITE setBuoyLightLabels
                 NOTIFY changed)
  Q_PROPERTY(bool lightDescriptions READ lightDescriptions WRITE
                 setLightDescriptions NOTIFY changed)
  Q_PROPERTY(bool extendedLightSectors READ extendedLightSectors WRITE
                 setExtendedLightSectors NOTIFY changed)
  Q_PROPERTY(bool nationalText READ nationalText WRITE setNationalText NOTIFY
                 changed)
  Q_PROPERTY(bool importantTextOnly READ importantTextOnly WRITE
                 setImportantTextOnly NOTIFY changed)
  Q_PROPERTY(bool declutterText READ declutterText WRITE setDeclutterText
                 NOTIFY changed)
  Q_PROPERTY(bool reducedDetailSmallScale READ reducedDetailSmallScale WRITE
                 setReducedDetailSmallScale NOTIFY changed)
  Q_PROPERTY(bool superScamin READ superScamin WRITE setSuperScamin NOTIFY
                 changed)

  // Cartographic style.
  // 0 = paper chart, 1 = simplified.
  Q_PROPERTY(int graphicsStyle READ graphicsStyle WRITE setGraphicsStyle
                 NOTIFY changed)
  // 0 = plain boundaries, 1 = symbolised.
  Q_PROPERTY(int boundaryStyle READ boundaryStyle WRITE setBoundaryStyle NOTIFY
                 changed)
  // 0 = four-colour depth shading, 1 = two-colour.
  Q_PROPERTY(int colourCount READ colourCount WRITE setColourCount NOTIFY
                 changed)

  // Depth contours (metres) -- shallow < safety < deep.
  Q_PROPERTY(double shallowContour READ shallowContour WRITE setShallowContour
                 NOTIFY changed)
  Q_PROPERTY(double safetyContour READ safetyContour WRITE setSafetyContour
                 NOTIFY changed)
  Q_PROPERTY(double deepContour READ deepContour WRITE setDeepContour NOTIFY
                 changed)

  // CM93 composite-chart controls.
  Q_PROPERTY(int cm93Detail READ cm93Detail WRITE setCm93Detail NOTIFY changed)
  Q_PROPERTY(double cm93OffsetX READ cm93OffsetX WRITE setCm93OffsetX NOTIFY
                 changed)
  Q_PROPERTY(double cm93OffsetY READ cm93OffsetY WRITE setCm93OffsetY NOTIFY
                 changed)

public:
  static ChartConfig& instance();
  static ChartConfig* create(QQmlEngine*, QJSEngine*) {
    ChartConfig* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  bool chartInfoObjects() const { return m_chart_info; }
  void setChartInfoObjects(bool v);
  bool buoyLightLabels() const { return m_buoy_light_labels; }
  void setBuoyLightLabels(bool v);
  bool lightDescriptions() const { return m_light_descriptions; }
  void setLightDescriptions(bool v);
  bool extendedLightSectors() const { return m_ext_light_sectors; }
  void setExtendedLightSectors(bool v);
  bool nationalText() const { return m_national_text; }
  void setNationalText(bool v);
  bool importantTextOnly() const { return m_important_text; }
  void setImportantTextOnly(bool v);
  bool declutterText() const { return m_declutter; }
  void setDeclutterText(bool v);
  bool reducedDetailSmallScale() const { return m_reduced_detail; }
  void setReducedDetailSmallScale(bool v);
  bool superScamin() const { return m_super_scamin; }
  void setSuperScamin(bool v);

  int graphicsStyle() const { return m_graphics_style; }
  void setGraphicsStyle(int v);
  int boundaryStyle() const { return m_boundary_style; }
  void setBoundaryStyle(int v);
  int colourCount() const { return m_colour_count; }
  void setColourCount(int v);

  double shallowContour() const { return m_shallow; }
  void setShallowContour(double v);
  double safetyContour() const { return m_safety; }
  void setSafetyContour(double v);
  double deepContour() const { return m_deep; }
  void setDeepContour(double v);

  int cm93Detail() const { return m_cm93_detail; }
  void setCm93Detail(int v);
  double cm93OffsetX() const { return m_cm93_dx; }
  void setCm93OffsetX(double v);
  double cm93OffsetY() const { return m_cm93_dy; }
  void setCm93OffsetY(double v);

Q_SIGNALS:
  void changed();

private:
  ChartConfig();  // loads from the config store

  bool m_chart_info = false;
  bool m_buoy_light_labels = true;
  bool m_light_descriptions = false;
  bool m_ext_light_sectors = true;
  bool m_national_text = false;
  bool m_important_text = false;
  bool m_declutter = false;
  bool m_reduced_detail = true;
  bool m_super_scamin = false;

  int m_graphics_style = 0;
  int m_boundary_style = 0;
  int m_colour_count = 0;

  double m_shallow = 2.0;
  double m_safety = 5.0;
  double m_deep = 10.0;

  int m_cm93_detail = 0;
  double m_cm93_dx = 0.0;
  double m_cm93_dy = 0.0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_CONFIG_H_
