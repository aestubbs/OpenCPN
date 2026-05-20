/***************************************************************************
 *   Copyright (C) 2018 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 *\file
 *
 * Implement canvas_config.h -- canvas configuration
 */

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif  // precompiled headers

#include <QString>

#include "gl_headers.h"

#include "canvas_config.h"
#include "s52s57.h"

//----------------------------------------------------------------------------
//   constants
//----------------------------------------------------------------------------
#ifndef PI
#define PI 3.1415926535897931160E0 /* pi */
#endif

//------------------------------------------------------------------------------
// canvasConfig Implementation
//------------------------------------------------------------------------------

canvasConfig::canvasConfig(int index) {
  configIndex = index;
  canvas = NULL;
  GroupID = 0;
  iLat = 0.;
  iLon = 0.;
  iScale = .0003;  // decent initial value
  iRotation = 0.;
}

canvasConfig::~canvasConfig() {}

void canvasConfig::Reset() {
  bFollow = false;
  bShowTides = false;
  bShowCurrents = false;
  bCourseUp = false;
  bHeadUp = false;
  bLookahead = false;
  bShowAIS = true;
  bAttenAIS = false;
  bQuilt = true;
  nENCDisplayCategory = (int)(enum _DisCat)OTHER;
  bShowENCDataQuality = 0;
  bShowENCBuoyLabels = 0;
  bShowENCLightDescriptions = 1;
  bEnableBasemapTile = true;
}

void canvasConfig::LoadFromLegacyConfig(OcpnConfig *conf) {
  if (!conf) return;

  bFollow = false;
  bShowAIS = true;

  // S52 stuff
  conf->beginGroup("Settings/GlobalState");
  bShowENCText = conf->value("bShowS57Text", true).toBool();
  bShowENCLightDescriptions =
      conf->value("bShowLightDescription", true).toBool();
  nENCDisplayCategory =
      conf->value("nDisplayCategory", (int)(enum _DisCat)OTHER).toInt();
  bShowENCDepths = conf->value("bShowSoundg", true).toBool();
  bShowENCBuoyLabels = conf->value("bShowAtonText", false).toBool();
  conf->endGroup();
  bShowENCLights = true;
  bShowENCVisibleSectorLights = false;
  bShowENCAnchorInfo = false;
  bShowENCDataQuality = false;

  conf->beginGroup("Settings/AIS");
  bAttenAIS = conf->value("bShowScaledTargets", false).toBool();
  conf->endGroup();

  conf->beginGroup("Settings");
  bShowTides = conf->value("ShowTide", false).toBool();
  bShowCurrents = conf->value("ShowCurrent", false).toBool();
  bCourseUp = conf->value("CourseUpMode", false).toBool();
  bHeadUp = conf->value("HeadUpMode", false).toBool();
  bLookahead = conf->value("LookAheadMode", false).toBool();

  bShowGrid = conf->value("ShowGrid", false).toBool();
  bShowOutlines = conf->value("ShowChartOutlines", true).toBool();
  bShowDepthUnits = conf->value("ShowDepthUnits", true).toBool();
  bQuilt = conf->value("ChartQuilting", true).toBool();

  GroupID = conf->value("ActiveChartGroup", 0).toInt();
  DBindex = conf->value("InitialdBIndex", -1).toInt();
  conf->endGroup();

  conf->beginGroup("Settings/GlobalState");
  double st_view_scale, st_rotation;
  if (conf->contains("VPScale")) {
    const QString st = conf->value("VPScale").toString();
    sscanf(st.toUtf8().constData(), "%lf", &st_view_scale);
    //    Sanity check the scale
    st_view_scale = fmax(st_view_scale, .001 / 32);
    st_view_scale = fmin(st_view_scale, 4);
    iScale = st_view_scale;
  }

  if (conf->contains("VPRotation")) {
    const QString st = conf->value("VPRotation").toString();
    sscanf(st.toUtf8().constData(), "%lf", &st_rotation);
    //    Sanity check the rotation
    st_rotation = fmin(st_rotation, 360);
    st_rotation = fmax(st_rotation, 0);
    iRotation = st_rotation * PI / 180.;
  }

  double lat, lon;
  if (conf->contains("VPLatLon")) {
    const QString sll = conf->value("VPLatLon").toString();
    sscanf(sll.toUtf8().constData(), "%lf,%lf", &lat, &lon);

    //    Sanity check the lat/lon...both have to be reasonable.
    if (fabs(lon) < 360.) {
      while (lon < -180.) lon += 360.;

      while (lon > 180.) lon -= 360.;

      iLon = lon;
    }

    if (fabs(lat) < 90.0) iLat = lat;
  }
  conf->endGroup();
}
