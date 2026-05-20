/**************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
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
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 **************************************************************************/

/**
 * \file
 *
 *  Implement nav_util.h -- Utility Functions
 */

#include "gl_headers.h"  // Must be included before anything using GL stuff

#include <wx/wxprec.h>

#ifdef __MINGW32__
#undef IPV6STRICT  // mingw FTBS fix:  missing struct ip_mreq
#include <windows.h>
#endif

#include <algorithm>
#include <stdlib.h>
#include <time.h>
#include <locale>
#include <list>
#include <limits>
#include <string>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif  // precompiled headers

#include <wx/bmpcbox.h>
#include <wx/dir.h>
#include "wx/dirctrl.h"
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/image.h>
#include <wx/listbook.h>
#include <wx/listimpl.cpp>
#include <wx/progdlg.h>
#include <wx/sstream.h>
#include <wx/tglbtn.h>
#include <wx/timectrl.h>
#include <wx/tokenzr.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

#include "o_sound/o_sound.h"

#include "model/ais_decoder.h"
#include "model/ais_state_vars.h"
#include "model/cmdline.h"
#include "model/config_vars.h"
#include "model/conn_params.h"
#include "model/wx_qt_ui_types.h"
#include "model/cutil.h"
#include "model/geodesic.h"
#include "model/georef.h"
#include "model/gui_vars.h"
#include "model/idents.h"
#include "model/multiplexer.h"
#include "model/nav_object_database.h"
#include "model/navutil_base.h"
#include "model/navobj_db.h"
#include "model/wx_qt_string.h"
#include "model/own_ship.h"
#include "model/plugin_comm.h"
#include "model/route.h"
#include "model/routeman.h"
#include "model/select.h"
#include "model/track.h"

#include "ais.h"
#include "canvas_config.h"
#include "chartbase.h"
#include "chartdb.h"
#include "chcanv.h"
#include "cm93.h"
#include "config.h"
#include "config_compat_helpers.h"
#include "config_mgr.h"
#include "displays.h"
#include "dychart.h"
#include "font_mgr.h"
#include "layer.h"
#include "navutil.h"
#include "nmea0183.h"
#include "observable_globvar.h"
#include "ocpndc.h"
#include "ocpn_plugin.h"
#include "ocpn_platform.h"
#include "s52plib.h"
#include "s52utils.h"
#include "s57_load.h"
#include "snd_config.h"
#include "styles.h"
#include "top_frame.h"
#include "user_colors.h"

#ifdef ocpnUSE_GL
#include "gl_chart_canvas.h"
#endif

#ifdef __ANDROID__
#include "androidUTIL.h"
#endif

MyConfig *pConfig;  ///< Global instance
static bool g_bLayersLoaded;

#ifdef ocpnUSE_GL
extern ocpnGLOptions g_GLOptions;
#endif

#if !defined(NAN)
static const long long lNaN = 0xfff8000000000000;
#define NAN (*(double *)&lNaN)
#endif

namespace navutil {

QStringList *pMessageOnceArray;

void InitGlobals() { pMessageOnceArray = new QStringList(); }

void DeinitGlobals() {
  delete pMessageOnceArray;
  pMessageOnceArray = nullptr;
}

}  // namespace navutil

// Layer helper function

wxString GetLayerName(int id) {
  wxString name("unknown layer");
  if (id <= 0) return (name);
  LayerList::iterator it;
  int index = 0;
  for (it = (*pLayerList).begin(); it != (*pLayerList).end(); ++it, ++index) {
    Layer *lay = (Layer *)(*it);
    if (lay->m_LayerID == id) return (lay->m_LayerName);
  }
  return (name);
}

// Helper conditional file name dir slash
void appendOSDirSlash(wxString *pString);

//-----------------------------------------------------------------------------
//          MyConfig Implementation
//-----------------------------------------------------------------------------
//

MyConfig::MyConfig(const wxString &LocalFileName)
    : OcpnConfig(wxString_to_QString(LocalFileName)) {}

MyConfig::~MyConfig() {}

unsigned MyConfig::ReadUnsigned(const wxString &key, unsigned default_val) {
  const QString qk = wxString_to_QString(key);
  if (!contains(qk)) return default_val;
  const std::string s = value(qk).toString().toStdString();
  unsigned long v = 0;
  try {
    v = std::stoul(s);
  } catch (std::logic_error &) {
    return default_val;
  }
  if (v > std::numeric_limits<unsigned>::max()) return default_val;
  return static_cast<unsigned>(v);
}

int MyConfig::LoadMyConfig() {
  int display_width, display_height;
  display_width = g_monitor_info[g_current_monitor].width;
  display_height = g_monitor_info[g_current_monitor].height;

  //  Set up any defaults not set elsewhere
  g_useMUI = true;
  g_TalkerIdText = "EC";
  g_maxWPNameLength = 6;
  g_NMEAAPBPrecision = 3;

#ifdef ocpnUSE_GL
  g_GLOptions.m_bUseAcceleratedPanning = true;
  g_GLOptions.m_GLPolygonSmoothing = true;
  g_GLOptions.m_GLLineSmoothing = true;
  g_GLOptions.m_iTextureDimension = 512;
  g_GLOptions.m_iTextureMemorySize = 128;
  if (!g_bGLexpert) {
    g_GLOptions.m_iTextureMemorySize =
        wxMax(128, g_GLOptions.m_iTextureMemorySize);
    g_GLOptions.m_bTextureCompressionCaching =
        g_GLOptions.m_bTextureCompression;
  }
#endif

  g_maintoolbar_orient = wxTB_HORIZONTAL;
  g_iENCToolbarPosX = -1;
  g_iENCToolbarPosY = -1;
  g_restore_dbindex = -1;
  g_ChartNotRenderScaleFactor = 1.5;
  g_detailslider_dialog_x = 200L;
  g_detailslider_dialog_y = 200L;
  g_SENC_LOD_pixels = 2;
  g_SkewCompUpdatePeriod = 10;

  g_bShowStatusBar = 1;
  g_bShowCompassWin = 1;
  g_iSoundDeviceIndex = -1;
  g_bFullscreenToolbar = 1;
  g_bTransparentToolbar = 0;
  g_bShowLayers = 1;
  g_bShowDepthUnits = 1;
  g_bShowActiveRouteHighway = 1;
  g_bShowChartBar = 1;
  g_defaultBoatSpeed = 6.0;
  g_ownship_predictor_minutes = 5;
  g_cog_predictor_style = 105;
  g_cog_predictor_color = "rgb(255,0,0)";
  g_cog_predictor_endmarker = 1;
  g_ownship_HDTpredictor_style = 105;
  g_ownship_HDTpredictor_color = "rgb(255,0,0)";
  g_ownship_HDTpredictor_endmarker = 1;
  g_ownship_HDTpredictor_width = 0;
  g_cog_predictor_width = 3;
  g_ownship_HDTpredictor_miles = 1;
  g_n_ownship_min_mm = 2;
  g_bFullScreenQuilt = 1;
  g_track_rotate_time_type = TIME_TYPE_COMPUTER;
  g_bHighliteTracks = 1;
  g_bPreserveScaleOnX = 1;
  g_navobjbackups = 5;
  g_benableAISNameCache = true;
  g_n_arrival_circle_radius = 0.05;
  g_plus_minus_zoom_factor = 2.0;
  g_mouse_zoom_sensitivity = 1.5;
  g_datetime_format = "UTC";

  g_AISShowTracks_Mins = 20;
  g_AISShowTracks_Limit = 300.0;
  g_ShowScaled_Num = 10;
  g_ScaledNumWeightSOG = 50;
  g_ScaledNumWeightCPA = 60;
  g_ScaledNumWeightTCPA = 25;
  g_ScaledSizeMinimal = 50;
  g_ScaledNumWeightRange = 75;
  g_ScaledNumWeightSizeOfT = 25;
  g_Show_Target_Name_Scale = 250000;
  g_bWplUsePosition = 0;
  g_WplAction = 0;
  g_ais_cog_predictor_width = 3;
  g_ais_alert_dialog_sx = 200;
  g_ais_alert_dialog_sy = 200;
  g_ais_alert_dialog_x = 200;
  g_ais_alert_dialog_y = 200;
  g_ais_query_dialog_x = 200;
  g_ais_query_dialog_y = 200;
  g_AisTargetList_range = 40;
  g_AisTargetList_sortColumn = 2;  // Column #2 is MMSI
  g_S57_dialog_sx = 400;
  g_S57_dialog_sy = 400;
  g_S57_extradialog_sx = 400;
  g_S57_extradialog_sy = 400;

  //    Reasonable starting point
  vLat = START_LAT;  // display viewpoint
  vLon = START_LON;
  gLat = START_LAT;  // GPS position, as default
  gLon = START_LON;
  g_maxzoomin = 800;

  g_iNavAidRadarRingsNumberVisible = 0;
  g_bNavAidRadarRingsShown = false;
  g_fNavAidRadarRingsStep = 1.0;
  g_pNavAidRadarRingsStepUnits = 0;
  g_colourOwnshipRangeRingsColour = *wxRED;
  g_iWaypointRangeRingsNumber = 0;
  g_fWaypointRangeRingsStep = 1.0;
  g_iWaypointRangeRingsStepUnits = 0;
  g_colourWaypointRangeRingsColour = QColor(255, 0, 0);
  g_bConfirmObjectDelete = true;

  g_TrackIntervalSeconds = 60.0;
  g_TrackDeltaDistance = 0.10;
  g_route_line_width = 2;
  g_track_line_width = 2;
  g_colourTrackLineColour = wxColour(243, 229, 47);  // Yellow

  g_tcwin_scale = 100;
  g_default_wp_icon = "triangle";
  g_default_routepoint_icon = "diamond";

  g_nAWDefault = 50;
  g_nAWMax = 1852;
  g_ObjQFileExt = "txt,rtf,png,html,gif,tif,jpg";

  // Load the raw value, with no defaults, and no processing
  int ret_Val = LoadMyConfigRaw();

  //  Perform any required post processing and validation
  if (!ret_Val) {
    g_ChartScaleFactorExp =
        g_Platform->GetChartScaleFactorExp(g_ChartScaleFactor);
    g_ShipScaleFactorExp =
        g_Platform->GetChartScaleFactorExp(g_ShipScaleFactor);
    g_MarkScaleFactorExp =
        g_Platform->GetMarkScaleFactorExp(g_ChartScaleFactor);

    g_COGFilterSec = wxMin(g_COGFilterSec, kMaxCogsogFilterSeconds);
    g_COGFilterSec = wxMax(g_COGFilterSec, 1);
    g_SOGFilterSec = g_COGFilterSec;

    if (!g_bShowTrue && !g_bShowMag) g_bShowTrue = true;
    g_COGAvgSec =
        wxMin(g_COGAvgSec, kMaxCogAverageSeconds);  // Bound the array size

    if (g_bInlandEcdis) g_bLookAhead = 1;

    if (g_bdisable_opengl) g_bopengl = false;

#ifdef ocpnUSE_GL
    if (!g_bGLexpert) {
      g_GLOptions.m_iTextureMemorySize =
          wxMax(128, g_GLOptions.m_iTextureMemorySize);
      g_GLOptions.m_bTextureCompressionCaching =
          g_GLOptions.m_bTextureCompression;
    }
#endif

    g_chart_zoom_modifier_raster = wxMin(g_chart_zoom_modifier_raster, 5);
    g_chart_zoom_modifier_raster = wxMax(g_chart_zoom_modifier_raster, -5);
    g_chart_zoom_modifier_vector = wxMin(g_chart_zoom_modifier_vector, 5);
    g_chart_zoom_modifier_vector = wxMax(g_chart_zoom_modifier_vector, -5);
    g_cm93_zoom_factor = wxMin(g_cm93_zoom_factor, CM93_ZOOM_FACTOR_MAX_RANGE);
    g_cm93_zoom_factor =
        wxMax(g_cm93_zoom_factor, (-CM93_ZOOM_FACTOR_MAX_RANGE));

    g_tile_basemap_zoom_factor = 4.0;

    if ((g_detailslider_dialog_x < 0) ||
        (g_detailslider_dialog_x > display_width))
      g_detailslider_dialog_x = 5;
    if ((g_detailslider_dialog_y < 0) ||
        (g_detailslider_dialog_y > display_height))
      g_detailslider_dialog_y = 5;

    g_defaultBoatSpeedUserUnit = toUsrSpeed(g_defaultBoatSpeed, -1);
    g_n_ownship_min_mm = wxMax(g_n_ownship_min_mm, 2);

    if (g_navobjbackups > 99) g_navobjbackups = 99;
    if (g_navobjbackups < 0) g_navobjbackups = 0;
    g_n_arrival_circle_radius = wxClip(g_n_arrival_circle_radius, 0.001, 0.6);

    g_selection_radius_mm = wxMax(g_selection_radius_mm, 0.5);
    g_selection_radius_touch_mm = wxMax(g_selection_radius_touch_mm, 1.0);

    g_Show_Target_Name_Scale = wxMax(5000, g_Show_Target_Name_Scale);

    if ((g_ais_alert_dialog_x < 0) || (g_ais_alert_dialog_x > display_width))
      g_ais_alert_dialog_x = 5;
    if ((g_ais_alert_dialog_y < 0) || (g_ais_alert_dialog_y > display_height))
      g_ais_alert_dialog_y = 5;
    if ((g_ais_query_dialog_x < 0) || (g_ais_query_dialog_x > display_width))
      g_ais_query_dialog_x = 5;
    if ((g_ais_query_dialog_y < 0) || (g_ais_query_dialog_y > display_height))
      g_ais_query_dialog_y = 5;

    SwitchInlandEcdisMode(g_bInlandEcdis);
    if (g_bInlandEcdis)
      global_color_scheme =
          GLOBAL_COLOR_SCHEME_DUSK;  // startup in duskmode if inlandEcdis

    //    Multicanvas Settings
    LoadCanvasConfigs();
  }

  return ret_Val;
}

int MyConfig::LoadMyConfigRaw(bool bAsTemplate) {
  int read_int;
  wxString val;

  int display_width, display_height;
  display_width = g_monitor_info[g_current_monitor].width;
  display_height = g_monitor_info[g_current_monitor].height;

  //    Global options and settings
  endAllGroups();
  beginGroup("Settings");
  CfgRead(*this, "ActiveRoute", &g_active_route);
  CfgRead(*this, "PersistActiveRoute", &g_persist_active_route);
  CfgRead(*this, "AlwaysSendRmbRmc", &g_always_send_rmb_rmc);
  CfgRead(*this, "LastAppliedTemplate", &g_lastAppliedTemplateGUID);
  CfgRead(*this, "CompatOS", &g_compatOS);
  CfgRead(*this, "CompatOsVersion", &g_compatOsVersion);

  // Some undocumented values
  CfgRead(*this, "ConfigVersionString", &g_config_version_string);
  CfgRead(*this, "CmdSoundString", &g_CmdSoundString, wxString(OCPN_SOUND_CMD));
  if (wxIsEmpty(g_CmdSoundString)) g_CmdSoundString = wxString(OCPN_SOUND_CMD);
  CfgRead(*this, "NavMessageShown", &n_NavMessageShown);

  CfgRead(*this, "AndroidVersionCode", &g_AndroidVersionCode);

  CfgRead(*this, "UIexpert", &g_bUIexpert);

  CfgRead(*this, "UIStyle", &g_uiStyle);

  CfgRead(*this, "NCacheLimit", &g_nCacheLimit);

  CfgRead(*this, "InlandEcdis",
          &g_bInlandEcdis);  // First read if in iENC mode as this will override
                             // some config settings

  CfgRead(*this, "SpaceDropMark", &g_bSpaceDropMark);

  int mem_limit = 0;
  CfgRead(*this, "MEMCacheLimit", &mem_limit);
  if (mem_limit > 0)
    g_memCacheLimit = mem_limit * 1024;  // convert from MBytes to kBytes

  CfgRead(*this, "UseModernUI5", &g_useMUI);

  CfgRead(*this, "NCPUCount", &g_nCPUCount);

  CfgRead(*this, "DebugGDAL", &g_bGDAL_Debug);
  CfgRead(*this, "DebugNMEA", &g_nNMEADebug);
  CfgRead(*this, "AnchorWatchDefault", &g_nAWDefault);
  CfgRead(*this, "AnchorWatchMax", &g_nAWMax);
  CfgRead(*this, "GPSDogTimeout", &gps_watchdog_timeout_ticks);
  CfgRead(*this, "DebugCM93", &g_bDebugCM93);
  CfgRead(*this, "DebugS57",
          &g_bDebugS57);  // Show LUP and Feature info in object query
  CfgRead(*this, "DebugBSBImg", &g_BSBImgDebug);
  CfgRead(*this, "DebugGPSD", &g_bDebugGPSD);
  CfgRead(*this, "MaxZoomScale", &g_maxzoomin);
  g_maxzoomin = wxMax(g_maxzoomin, 50);

  CfgRead(*this, "DefaultFontSize", &g_default_font_size);
  CfgRead(*this, "DefaultFontFacename", &g_default_font_facename);

  CfgRead(*this, "UseGreenShipIcon", &g_bUseGreenShip);

  CfgRead(*this, "AutoHideToolbar", &g_bAutoHideToolbar);
  CfgRead(*this, "AutoHideToolbarSecs", &g_nAutoHideToolbar);

  CfgRead(*this, "UseSimplifiedScalebar", &g_bsimplifiedScalebar);
  CfgRead(*this, "ShowTide", &g_bShowTide);
  CfgRead(*this, "ShowCurrent", &g_bShowCurrent);

  wxString size_mm;
  CfgRead(*this, "DisplaySizeMM", &size_mm);

  CfgRead(*this, "SelectionRadiusMM", &g_selection_radius_mm);
  CfgRead(*this, "SelectionRadiusTouchMM", &g_selection_radius_touch_mm);

  if (!bAsTemplate) {
    g_config_display_size_mm.clear();
    wxStringTokenizer tokenizer(size_mm, ",");
    while (tokenizer.HasMoreTokens()) {
      wxString token = tokenizer.GetNextToken();
      int size;
      try {
        size = std::stoi(token.ToStdString());
      } catch (std::invalid_argument &e) {
        size = 0;
      }
      if (size > 100 && size < 2000) {
        g_config_display_size_mm.push_back(size);
      } else {
        g_config_display_size_mm.push_back(0);
      }
    }
    CfgRead(*this, "DisplaySizeManual", &g_config_display_size_manual);
  }

  CfgRead(*this, "GUIScaleFactor", &g_GUIScaleFactor);

  CfgRead(*this, "ChartObjectScaleFactor", &g_ChartScaleFactor);
  CfgRead(*this, "ShipScaleFactor", &g_ShipScaleFactor);
  CfgRead(*this, "ENCSoundingScaleFactor", &g_ENCSoundingScaleFactor);
  CfgRead(*this, "ENCTextScaleFactor", &g_ENCTextScaleFactor);
  CfgRead(*this, "ObjQueryAppendFilesExt", &g_ObjQFileExt);

  // Plugin catalog handler persistent variables.
  CfgRead(*this, "CatalogCustomURL", &g_catalog_custom_url);
  CfgRead(*this, "CatalogChannel", &g_catalog_channel);

  CfgRead(*this, "NetmaskBits", &g_netmask_bits);

  //  NMEA connection options.
  if (!bAsTemplate) {
    CfgRead(*this, "FilterNMEA_Avg", &g_bfilter_cogsog);
    CfgRead(*this, "FilterNMEA_Sec", &g_COGFilterSec);
    CfgRead(*this, "GPSIdent", &g_GPS_Ident);
    CfgRead(*this, "UseGarminHostUpload", &g_bGarminHostUpload);
    CfgRead(*this, "UseNMEA_GLL", &g_bUseGLL);
    CfgRead(*this, "UseMagAPB", &g_bMagneticAPB);
    CfgRead(*this, "TrackContinuous", &g_btrackContinuous, false);
    CfgRead(*this, "FilterTrackDropLargeJump", &g_trackFilterMax, 1000);
  }

  CfgRead(*this, "ShowTrue", &g_bShowTrue);
  CfgRead(*this, "ShowMag", &g_bShowMag);

  wxString umv;
  CfgRead(*this, "UserMagVariation", &umv);
  if (umv.Len()) umv.ToDouble(&g_UserVar);

  CfgRead(*this, "ScreenBrightness", &g_nbrightness);

  CfgRead(*this, "MemFootprintTargetMB", &g_MemFootMB);

  CfgRead(*this, "WindowsComPortMax", &g_nCOMPortCheck);

  CfgRead(*this, "ChartQuilting", &g_bQuiltEnable);
  CfgRead(*this, "ChartQuiltingInitial", &g_bQuiltStart);

  CfgRead(*this, "CourseUpMode", &g_bCourseUp);
  CfgRead(*this, "COGUPAvgSeconds", &g_COGAvgSec);
  CfgRead(*this, "LookAheadMode", &g_bLookAhead);
  CfgRead(*this, "SkewToNorthUp", &g_bskew_comp);
  CfgRead(*this, "TenHzUpdate", &g_btenhertz, 0);
  CfgRead(*this, "DeclutterAnchorage", &g_declutter_anchorage, 0);

  CfgRead(*this, "NMEAAPBPrecision", &g_NMEAAPBPrecision);

  CfgRead(*this, "TalkerIdText", &g_TalkerIdText);
  CfgRead(*this, "MaxWaypointNameLength", &g_maxWPNameLength);
  CfgRead(*this, "MbtilesMaxLayers", &g_mbtilesMaxLayers);

  CfgRead(*this, "ShowTrackPointTime", &g_bShowTrackPointTime, true);
  /* opengl options */
#ifdef ocpnUSE_GL
  if (!bAsTemplate) {
    CfgRead(*this, "OpenGLExpert", &g_bGLexpert, false);
    CfgRead(*this, "UseAcceleratedPanning",
            &g_GLOptions.m_bUseAcceleratedPanning, true);
    CfgRead(*this, "GPUTextureCompression",
            &g_GLOptions.m_bTextureCompression);
    CfgRead(*this, "GPUTextureCompressionCaching",
            &g_GLOptions.m_bTextureCompressionCaching);
    CfgRead(*this, "PolygonSmoothing", &g_GLOptions.m_GLPolygonSmoothing);
    CfgRead(*this, "LineSmoothing", &g_GLOptions.m_GLLineSmoothing);
    CfgRead(*this, "GPUTextureDimension", &g_GLOptions.m_iTextureDimension);
    CfgRead(*this, "GPUTextureMemSize", &g_GLOptions.m_iTextureMemorySize);
    CfgRead(*this, "DebugOpenGL", &g_bDebugOGL);
    CfgRead(*this, "OpenGL", &g_bopengl);
    CfgRead(*this, "OpenGLFinishNeeded", &g_b_needFinish);
    CfgRead(*this, "SoftwareGL", &g_bSoftwareGL);
  }
#endif

  CfgRead(*this, "SmoothPanZoom", &g_bsmoothpanzoom);

  CfgRead(*this, "ToolbarX", &g_maintoolbar_x);
  CfgRead(*this, "ToolbarY", &g_maintoolbar_y);
  CfgRead(*this, "ToolbarOrient", &g_maintoolbar_orient);
  CfgRead(*this, "GlobalToolbarConfig", &g_toolbarConfig);

  CfgRead(*this, "iENCToolbarX", &g_iENCToolbarPosX);
  CfgRead(*this, "iENCToolbarY", &g_iENCToolbarPosY);

  CfgRead(*this, "AnchorWatch1GUID", &g_AW1GUID);
  CfgRead(*this, "AnchorWatch2GUID", &g_AW2GUID);

  CfgRead(*this, "InitialStackIndex", &g_restore_stackindex);
  CfgRead(*this, "InitialdBIndex", &g_restore_dbindex);

  CfgRead(*this, "ChartNotRenderScaleFactor", &g_ChartNotRenderScaleFactor);

  CfgRead(*this, "MobileTouch", &g_btouch);

//  "Responsive graphics" option deprecated in O58+
//  CfgReadStr(*this, "ResponsiveGraphics", &g_bresponsive);
#ifdef __ANDROID__
  g_bresponsive = true;
#else
  g_bresponsive = false;
#endif

  CfgRead(*this, "EnableRolloverBlock", &g_bRollover);

  CfgRead(*this, "ZoomDetailFactor", &g_chart_zoom_modifier_raster);
  CfgRead(*this, "ZoomDetailFactorVector", &g_chart_zoom_modifier_vector);
  CfgRead(*this, "PlusMinusZoomFactor", &g_plus_minus_zoom_factor, 2.0);
  CfgRead(*this, "MouseZoomSensitivity", &g_mouse_zoom_sensitivity, 1.3);
  g_mouse_zoom_sensitivity_ui =
      MouseZoom::config_to_ui(g_mouse_zoom_sensitivity);
  CfgRead(*this, "CM93DetailFactor", &g_cm93_zoom_factor);
  CfgRead(*this, "TileBasemapZoomFactor", &g_tile_basemap_zoom_factor);

  CfgRead(*this, "CM93DetailZoomPosX", &g_detailslider_dialog_x);
  CfgRead(*this, "CM93DetailZoomPosY", &g_detailslider_dialog_y);
  CfgRead(*this, "ShowCM93DetailSlider", &g_bShowDetailSlider);

  CfgRead(*this, "SENC_LOD_Pixels", &g_SENC_LOD_pixels);

  CfgRead(*this, "SkewCompUpdatePeriod", &g_SkewCompUpdatePeriod);

  CfgRead(*this, "SetSystemTime", &s_bSetSystemTime);
  CfgRead(*this, "EnableKioskStartup", &g_kiosk_startup);
  CfgRead(*this, "DisableNotifications", &g_disableNotifications, 0);
  CfgRead(*this, "ShowStatusBar", &g_bShowStatusBar);
#ifndef __WXOSX__
  CfgRead(*this, "ShowMenuBar", &g_bShowMenuBar);
#endif
  CfgRead(*this, "Fullscreen", &g_bFullscreen);
  CfgRead(*this, "ShowCompassWindow", &g_bShowCompassWin);
  CfgRead(*this, "ShowGrid", &g_bDisplayGrid);
  CfgRead(*this, "PlayShipsBells", &g_bPlayShipsBells);
  CfgRead(*this, "SoundDeviceIndex", &g_iSoundDeviceIndex);
  CfgRead(*this, "FullscreenToolbar", &g_bFullscreenToolbar);
  CfgRead(*this, "PermanentMOBIcon", &g_bPermanentMOBIcon);
  CfgRead(*this, "ShowLayers", &g_bShowLayers);
  CfgRead(*this, "ShowDepthUnits", &g_bShowDepthUnits);
  CfgRead(*this, "AutoAnchorDrop", &g_bAutoAnchorMark);
  CfgRead(*this, "ShowChartOutlines", &g_bShowOutlines);
  CfgRead(*this, "ShowActiveRouteHighway", &g_bShowActiveRouteHighway);
  CfgRead(*this, "ShowActiveRouteTotal", &g_bShowRouteTotal);
  CfgRead(*this, "MostRecentGPSUploadConnection", &g_uploadConnection);
  CfgRead(*this, "ShowChartBar", &g_bShowChartBar);
  CfgRead(*this, "SDMMFormat",
       &g_iSDMMFormat);  // 0 = "Degrees, Decimal minutes"), 1 = "Decimal
                         // degrees", 2 = "Degrees,Minutes, Seconds"

  CfgRead(*this, "DistanceFormat",
       &g_iDistanceFormat);  // 0 = "Nautical miles"), 1 = "Statute miles", 2 =
                             // "Kilometers", 3 = "Meters"
  CfgRead(*this, "SpeedFormat",
       &g_iSpeedFormat);  // 0 = "kts"), 1 = "mph", 2 = "km/h", 3 = "m/s"
  CfgRead(*this, "WindSpeedFormat",
       &g_iWindSpeedFormat);  // 0 = "knots"), 1 = "m/s", 2 = "Mph", 3 = "km/h"
  CfgRead(*this, "TemperatureFormat", &g_iTempFormat);  // 0 = C, 1 = F, 2 = K
  CfgRead(*this, "HeightFormat", &g_iHeightFormat);     // 0 = M, 1 = FT

  // LIVE ETA OPTION
  CfgRead(*this, "LiveETA", &g_bShowLiveETA);
  CfgRead(*this, "DefaultBoatSpeed", &g_defaultBoatSpeed);

  CfgRead(*this, "OwnshipCOGPredictorMinutes", &g_ownship_predictor_minutes);
  CfgRead(*this, "OwnshipCOGPredictorStyle", &g_cog_predictor_style);
  CfgRead(*this, "OwnshipCOGPredictorColor", &g_cog_predictor_color);
  CfgRead(*this, "OwnshipCOGPredictorEndmarker", &g_cog_predictor_endmarker);
  CfgRead(*this, "OwnshipCOGPredictorWidth", &g_cog_predictor_width);
  CfgRead(*this, "OwnshipHDTPredictorStyle", &g_ownship_HDTpredictor_style);
  CfgRead(*this, "OwnshipHDTPredictorColor", &g_ownship_HDTpredictor_color);
  CfgRead(*this, "OwnshipHDTPredictorEndmarker", &g_ownship_HDTpredictor_endmarker);
  CfgRead(*this, "OwnshipHDTPredictorWidth", &g_ownship_HDTpredictor_width);
  CfgRead(*this, "OwnshipHDTPredictorMiles", &g_ownship_HDTpredictor_miles);
  int mmsi;
  CfgRead(*this, "OwnShipMMSINumber", &mmsi);
  g_OwnShipmmsi = mmsi >= 0 ? static_cast<unsigned>(mmsi) : 0;
  CfgRead(*this, "OwnShipIconType", &g_OwnShipIconType);
  CfgRead(*this, "OwnShipLength", &g_n_ownship_length_meters);
  CfgRead(*this, "OwnShipWidth", &g_n_ownship_beam_meters);
  CfgRead(*this, "OwnShipGPSOffsetX", &g_n_gps_antenna_offset_x);
  CfgRead(*this, "OwnShipGPSOffsetY", &g_n_gps_antenna_offset_y);
  CfgRead(*this, "OwnShipMinSize", &g_n_ownship_min_mm);
  CfgRead(*this, "ShowDirectRouteLine", &g_bShowShipToActive);
  CfgRead(*this, "DirectRouteLineStyle", &g_shipToActiveStyle);
  CfgRead(*this, "DirectRouteLineColor", &g_shipToActiveColor);

  wxString racr;
  CfgRead(*this, "RouteArrivalCircleRadius", &racr);
  if (racr.Len()) racr.ToDouble(&g_n_arrival_circle_radius);

  CfgRead(*this, "FullScreenQuilt", &g_bFullScreenQuilt);

  CfgRead(*this, "StartWithTrackActive", &g_bTrackCarryOver);
  CfgRead(*this, "AutomaticDailyTracks", &g_bTrackDaily);
  CfgRead(*this, "TrackRotateAt", &g_track_rotate_time);
  CfgRead(*this, "TrackRotateTimeType", &g_track_rotate_time_type);
  CfgRead(*this, "HighlightTracks", &g_bHighliteTracks);

  CfgRead(*this, "DateTimeFormat", &g_datetime_format);

  wxString stps;
  CfgRead(*this, "PlanSpeed", &stps);
  if (!stps.IsEmpty()) stps.ToDouble(&g_PlanSpeed);

  CfgRead(*this, "VisibleLayers", &g_VisibleLayers);
  CfgRead(*this, "InvisibleLayers", &g_InvisibleLayers);
  CfgRead(*this, "VisNameInLayers", &g_VisiNameinLayers);
  CfgRead(*this, "InvisNameInLayers", &g_InVisiNameinLayers);

  CfgRead(*this, "PreserveScaleOnX", &g_bPreserveScaleOnX);

  CfgRead(*this, "ShowMUIZoomButtons", &g_bShowMuiZoomButtons);

  CfgRead(*this, "Locale", &g_locale);
  CfgRead(*this, "LocaleOverride", &g_localeOverride);

  // We allow 0-99 backups ov navobj.xml
  CfgRead(*this, "KeepNavobjBackups", &g_navobjbackups);

  // Boolean to cater for legacy Input COM Port filer behaviour, i.e. show msg
  // filtered but put msg on bus.
  CfgRead(*this, "LegacyInputCOMPortFilterBehaviour", &g_b_legacy_input_filter_behaviour);

  // Boolean to cater for sailing when not approaching waypoint
  CfgRead(*this, "AdvanceRouteWaypointOnArrivalOnly",
       &g_bAdvanceRouteWaypointOnArrivalOnly);
  CfgRead(*this, "EnableRootMenuDebug", &g_enable_root_menu_debug);

  CfgRead(*this, "EnableRotateKeys", &g_benable_rotate);
  CfgRead(*this, "EmailCrashReport", &g_bEmailCrashReport);

  g_benableAISNameCache = true;
  CfgRead(*this, "EnableAISNameCache", &g_benableAISNameCache);

  CfgRead(*this, "EnableUDPNullHeader", &g_benableUDPNullHeader);

  endAllGroups();
  beginGroup("Settings/GlobalState");

  CfgRead(*this, "FrameWinX", &g_nframewin_x);
  CfgRead(*this, "FrameWinY", &g_nframewin_y);
  CfgRead(*this, "FrameWinPosX", &g_nframewin_posx);
  CfgRead(*this, "FrameWinPosY", &g_nframewin_posy);
  CfgRead(*this, "FrameMax", &g_bframemax);

  CfgRead(*this, "ClientPosX", &g_lastClientRectx);
  CfgRead(*this, "ClientPosY", &g_lastClientRecty);
  CfgRead(*this, "ClientSzX", &g_lastClientRectw);
  CfgRead(*this, "ClientSzY", &g_lastClientRecth);

  CfgRead(*this, "RoutePropSizeX", &g_route_prop_sx);
  CfgRead(*this, "RoutePropSizeY", &g_route_prop_sy);
  CfgRead(*this, "RoutePropPosX", &g_route_prop_x);
  CfgRead(*this, "RoutePropPosY", &g_route_prop_y);

  CfgRead(*this, "AllowArbitrarySystemPlugins", &g_allow_arb_system_plugin);

  read_int = -1;
  CfgRead(*this, "S52_DEPTH_UNIT_SHOW", &read_int);  // default is metres
  if (read_int >= 0) {
    read_int = wxMax(read_int, 0);  // qualify value
    read_int = wxMin(read_int, 2);
    g_nDepthUnitDisplay = read_int;
  }

  // Sounds
  endAllGroups();
  beginGroup("Settings/Audio");

  // Set reasonable defaults
  wxString sound_dir = g_Platform->GetSharedDataDir();
  sound_dir.Append("sounds");
  sound_dir.Append(QString_to_wxString(QString(QDir::separator())));

  g_AIS_sound_file = sound_dir + "beep_ssl.wav";
  g_DSC_sound_file = sound_dir + "phonering1.wav";
  g_SART_sound_file = sound_dir + "beep3.wav";
  g_anchorwatch_sound_file = sound_dir + "beep1.wav";

  CfgRead(*this, "AISAlertSoundFile", &g_AIS_sound_file);
  CfgRead(*this, "DSCAlertSoundFile", &g_DSC_sound_file);
  CfgRead(*this, "SARTAlertSoundFile", &g_SART_sound_file);
  CfgRead(*this, "AnchorAlarmSoundFile", &g_anchorwatch_sound_file);

  CfgRead(*this, "bAIS_GCPA_AlertAudio", &g_bAIS_GCPA_Alert_Audio);
  CfgRead(*this, "bAIS_SART_AlertAudio", &g_bAIS_SART_Alert_Audio);
  CfgRead(*this, "bAIS_DSC_AlertAudio", &g_bAIS_DSC_Alert_Audio);
  CfgRead(*this, "bAnchorAlertAudio", &g_bAnchor_Alert_Audio);

  //    AIS
  wxString s;
  endAllGroups();
  beginGroup("Settings/AIS");

  g_bUseOnlyConfirmedAISName = false;
  CfgRead(*this, "UseOnlyConfirmedAISName", &g_bUseOnlyConfirmedAISName);

  CfgRead(*this, "bNoCPAMax", &g_bCPAMax);

  CfgRead(*this, "NoCPAMaxNMi", &s);
  s.ToDouble(&g_CPAMax_NM);

  CfgRead(*this, "bCPAWarn", &g_bCPAWarn);

  CfgRead(*this, "CPAWarnNMi", &s);
  s.ToDouble(&g_CPAWarn_NM);

  CfgRead(*this, "bTCPAMax", &g_bTCPA_Max);

  CfgRead(*this, "TCPAMaxMinutes", &s);
  s.ToDouble(&g_TCPA_Max);

  CfgRead(*this, "bMarkLostTargets", &g_bMarkLost);

  CfgRead(*this, "MarkLost_Minutes", &s);
  s.ToDouble(&g_MarkLost_Mins);

  CfgRead(*this, "bRemoveLostTargets", &g_bRemoveLost);

  CfgRead(*this, "RemoveLost_Minutes", &s);
  s.ToDouble(&g_RemoveLost_Mins);

  CfgRead(*this, "bShowCOGArrows", &g_bShowCOG);

  CfgRead(*this, "bSyncCogPredictors", &g_bSyncCogPredictors);

  CfgRead(*this, "CogArrowMinutes", &s);
  s.ToDouble(&g_ShowCOG_Mins);

  CfgRead(*this, "bShowTargetTracks", &g_bAISShowTracks);

  if (CfgReadIf(*this, "TargetTracksLimit", &s)) {
    s.ToDouble(&g_AISShowTracks_Limit);
    g_AISShowTracks_Limit = wxMax(300.0, g_AISShowTracks_Limit);
  }
  if (CfgReadIf(*this, "TargetTracksMinutes", &s)) {
    s.ToDouble(&g_AISShowTracks_Mins);
    g_AISShowTracks_Mins = wxMax(1.0, g_AISShowTracks_Mins);
    g_AISShowTracks_Mins = wxMin(g_AISShowTracks_Limit, g_AISShowTracks_Mins);
  }

  CfgRead(*this, "bHideMooredTargets", &g_bHideMoored);
  if (CfgReadIf(*this, "MooredTargetMaxSpeedKnots", &s)) s.ToDouble(&g_ShowMoored_Kts);

  g_SOGminCOG_kts = 0.2;
  if (CfgReadIf(*this, "SOGMinimumForCOGDisplay", &s)) s.ToDouble(&g_SOGminCOG_kts);

  CfgRead(*this, "bShowScaledTargets", &g_bAllowShowScaled);
  CfgRead(*this, "AISScaledNumber", &g_ShowScaled_Num);
  CfgRead(*this, "AISScaledNumberWeightSOG", &g_ScaledNumWeightSOG);
  CfgRead(*this, "AISScaledNumberWeightCPA", &g_ScaledNumWeightCPA);
  CfgRead(*this, "AISScaledNumberWeightTCPA", &g_ScaledNumWeightTCPA);
  CfgRead(*this, "AISScaledNumberWeightRange", &g_ScaledNumWeightRange);
  CfgRead(*this, "AISScaledNumberWeightSizeOfTarget", &g_ScaledNumWeightSizeOfT);
  CfgRead(*this, "AISScaledSizeMinimal", &g_ScaledSizeMinimal);
  CfgRead(*this, "AISShowScaled", &g_bShowScaled);

  CfgRead(*this, "bShowAreaNotices", &g_bShowAreaNotices);
  CfgRead(*this, "bDrawAISSize", &g_bDrawAISSize);
  CfgRead(*this, "bDrawAISRealtime", &g_bDrawAISRealtime);
  CfgRead(*this, "bShowAISName", &g_bShowAISName);
  CfgRead(*this, "AISRealtimeMinSpeedKnots", &g_AIS_RealtPred_Kts, 0.7);
  CfgRead(*this, "bAISAlertDialog", &g_bAIS_CPA_Alert);
  CfgRead(*this, "ShowAISTargetNameScale", &g_Show_Target_Name_Scale);
  CfgRead(*this, "bWplIsAprsPositionReport", &g_bWplUsePosition);
  CfgRead(*this, "WplSelAction", &g_WplAction);
  CfgRead(*this, "AISCOGPredictorWidth", &g_ais_cog_predictor_width);

  CfgRead(*this, "bAISAlertAudio", &g_bAIS_CPA_Alert_Audio);
  CfgRead(*this, "AISAlertAudioFile", &g_sAIS_Alert_Sound_File);
  CfgRead(*this, "bAISAlertSuppressMoored", &g_bAIS_CPA_Alert_Suppress_Moored);

  CfgRead(*this, "bAISAlertAckTimeout", &g_bAIS_ACK_Timeout);
  if (CfgReadIf(*this, "AlertAckTimeoutMinutes", &s)) s.ToDouble(&g_AckTimeout_Mins);

  CfgRead(*this, "AlertDialogSizeX", &g_ais_alert_dialog_sx);
  CfgRead(*this, "AlertDialogSizeY", &g_ais_alert_dialog_sy);
  CfgRead(*this, "AlertDialogPosX", &g_ais_alert_dialog_x);
  CfgRead(*this, "AlertDialogPosY", &g_ais_alert_dialog_y);
  CfgRead(*this, "QueryDialogPosX", &g_ais_query_dialog_x);
  CfgRead(*this, "QueryDialogPosY", &g_ais_query_dialog_y);

  CfgRead(*this, "AISTargetListPerspective", &g_AisTargetList_perspective);
  CfgRead(*this, "AISTargetListRange", &g_AisTargetList_range);
  CfgRead(*this, "AISTargetListSortColumn", &g_AisTargetList_sortColumn);
  CfgRead(*this, "bAISTargetListSortReverse", &g_bAisTargetList_sortReverse);
  CfgRead(*this, "AISTargetListColumnSpec", &g_AisTargetList_column_spec);
  CfgRead(*this, "AISTargetListColumnOrder", &g_AisTargetList_column_order);

  CfgRead(*this, "bAISRolloverShowClass", &g_bAISRolloverShowClass);
  CfgRead(*this, "bAISRolloverShowCOG", &g_bAISRolloverShowCOG);
  CfgRead(*this, "bAISRolloverShowCPA", &g_bAISRolloverShowCPA);
  CfgRead(*this, "AISAlertDelay", &g_AIS_alert_delay);

  CfgRead(*this, "S57QueryDialogSizeX", &g_S57_dialog_sx);
  CfgRead(*this, "S57QueryDialogSizeY", &g_S57_dialog_sy);
  CfgRead(*this, "S57QueryExtraDialogSizeX", &g_S57_extradialog_sx);
  CfgRead(*this, "S57QueryExtraDialogSizeY", &g_S57_extradialog_sy);

  wxString strpres("PresentationLibraryData");
  wxString valpres;
  endAllGroups();
  beginGroup("Directories");
  CfgRead(*this, strpres, &valpres);  // Get the File name
  if (!valpres.IsEmpty()) g_UserPresLibData = valpres;

  wxString strs("SENCFileLocation");
  endAllGroups();
  beginGroup("Directories");
  wxString vals;
  CfgRead(*this, strs, &vals);  // Get the Directory name
  if (!vals.IsEmpty()) g_SENCPrefix = vals;

  endAllGroups();
  beginGroup("Directories");
  wxString vald;
  CfgRead(*this, "InitChartDir", &vald);  // Get the Directory name

  wxString dirnamed(vald);
  if (!dirnamed.IsEmpty()) {
    if (pInit_Chart_Dir->IsEmpty())  // on second pass, don't overwrite
    {
      pInit_Chart_Dir->Clear();
      pInit_Chart_Dir->Append(vald);
    }
  }

  CfgRead(*this, "GPXIODir", &g_gpx_path);     // Get the Directory name
  CfgRead(*this, "TCDataDir", &g_TCData_Dir);  // Get the Directory name
  CfgRead(*this, "BasemapDir", &gWorldMapLocation);
  CfgRead(*this, "BaseShapefileDir", &gWorldShapefileLocation);
  CfgRead(*this, "pluginInstallDir", &g_winPluginDir);
  wxLogMessage("winPluginDir, read from ini file: %s",
               g_winPluginDir.mb_str().data());

  endAllGroups();
  beginGroup("Settings/GlobalState");

  if (CfgReadIf(*this, "nColorScheme", &read_int))
    global_color_scheme = (ColorScheme)read_int;

  if (!bAsTemplate) {
    endAllGroups();
    beginGroup("Settings/NMEADataSource");

    TheConnectionParams().clear();
    wxString connectionconfigs;
    CfgRead(*this, "DataConnections", &connectionconfigs);
    if (!connectionconfigs.IsEmpty()) {
      QStringList confs = wxString_to_QString(connectionconfigs)
                              .split(QChar('|'), Qt::SkipEmptyParts);
      for (const QString &conf : confs) {
        ConnectionParams *prm = new ConnectionParams(QString_to_wxString(conf));
        if (!prm->Valid) {
          wxLogMessage("Skipped invalid DataStream config");
          delete prm;
          continue;
        }
        TheConnectionParams().push_back(prm);
      }
    }
  }

  endAllGroups();
  beginGroup("Settings/GlobalState");
  wxString st;

  double st_lat, st_lon;
  if (CfgReadIf(*this, "VPLatLon", &st)) {
    sscanf(st.mb_str(wxConvUTF8), "%lf,%lf", &st_lat, &st_lon);

    //    Sanity check the lat/lon...both have to be reasonable.
    if (fabs(st_lon) < 360.) {
      while (st_lon < -180.) st_lon += 360.;

      while (st_lon > 180.) st_lon -= 360.;

      vLon = st_lon;
    }

    if (fabs(st_lat) < 90.0) vLat = st_lat;

    s.Printf("Setting Viewpoint Lat/Lon %g, %g", vLat, vLon);
    wxLogMessage(s);
  }

  double st_view_scale, st_rotation;
  if (CfgReadIf(*this, wxString("VPScale"), &st)) {
    sscanf(st.mb_str(wxConvUTF8), "%lf", &st_view_scale);
    //    Sanity check the scale
    st_view_scale = fmax(st_view_scale, .001 / 32);
    st_view_scale = fmin(st_view_scale, 4);
  }

  if (CfgReadIf(*this, wxString("VPRotation"), &st)) {
    sscanf(st.mb_str(wxConvUTF8), "%lf", &st_rotation);
    //    Sanity check the rotation
    st_rotation = fmin(st_rotation, 360);
    st_rotation = fmax(st_rotation, 0);
  }

  wxString sll;
  double lat, lon;
  if (CfgReadIf(*this, "OwnShipLatLon", &sll)) {
    sscanf(sll.mb_str(wxConvUTF8), "%lf,%lf", &lat, &lon);

    //    Sanity check the lat/lon...both have to be reasonable.
    if (fabs(lon) < 360.) {
      while (lon < -180.) lon += 360.;

      while (lon > 180.) lon -= 360.;

      gLon = lon;
    }

    if (fabs(lat) < 90.0) gLat = lat;

    s.Printf("Setting Ownship Lat/Lon %g, %g", gLat, gLon);
    wxLogMessage(s);
  }

  //    Fonts

  //  Load the persistent Auxiliary Font descriptor Keys
  endAllGroups();
  beginGroup("Settings/AuxFontKeys");

  {
    wxString kval;
    bool bNewKey = false;
    // Snapshot the child keys so deletions during the loop are safe.
    const QStringList aux_keys = childKeys();
    for (const QString &qstrk : aux_keys) {
      wxString strk = QString_to_wxString(qstrk);
      CfgRead(*this, strk, &kval);
      bNewKey = FontMgr::Get().AddAuxKey(kval);
      if (!bAsTemplate && !bNewKey) {
        CfgDelete(*this, strk);
      }
    }
  }

#ifdef __WXX11__
  endAllGroups();
  beginGroup("Settings/X11Fonts");
#endif

#ifdef __WXGTK__
  endAllGroups();
  beginGroup("Settings/GTKFonts");
#endif

#ifdef __WXMSW__
  endAllGroups();
  beginGroup("Settings/MSWFonts");
#endif

#ifdef __WXMAC__
  endAllGroups();
  beginGroup("Settings/MacFonts");
#endif

#ifdef __WXQT__
  endAllGroups();
  beginGroup("Settings/QTFonts");
#endif

  wxString pval;
  QStringList deleteList;

  for (const QString &qstrk : childKeys()) {
    wxString str = QString_to_wxString(qstrk);
    pval = CfgReadStr(*this, str);

    if (str.StartsWith("Font")) {
      // Convert pre 3.1 setting. Can't delete old entries from inside the
      // loop body, so we collect them and delete after the loop completes.
      deleteList.append(wxString_to_QString(str));
      wxString oldKey = pval.BeforeFirst(_T(':'));
      str = FontMgr::GetFontConfigKey(oldKey);
    }

    if (pval.IsEmpty() || pval.StartsWith(":")) {
      deleteList.append(wxString_to_QString(str));
    } else
      FontMgr::Get().LoadFontNative(&str, &pval);
  }

  for (const QString &s : deleteList) {
    CfgDelete(*this, QString_to_wxString(s));
  }
  deleteList.clear();

  //  Tide/Current Data Sources
  endAllGroups();
  beginGroup("TideCurrentDataSources");
  if (childKeys().size()) {
    TideCurrentDataSet.clear();
    for (const QString &qstrk : childKeys()) {
      wxString str = QString_to_wxString(qstrk);
      wxString val;
      CfgRead(*this, str, &val);  // Get a file name and add it to the list
      // We have seen duplication of dataset entries in
      // https://github.com/OpenCPN/OpenCPN/issues/3042, this effectively gets
      // rid of them.
      if (std::find(TideCurrentDataSet.begin(), TideCurrentDataSet.end(),
                    val.ToStdString()) == TideCurrentDataSet.end()) {
        TideCurrentDataSet.push_back(val.ToStdString());
      }
    }
  }

  //    Groups
  LoadConfigGroups(g_pGroupArray);

  //     //    Multicanvas Settings
  //     LoadCanvasConfigs();

  endAllGroups();
  beginGroup("Settings/Others");

  // Radar rings
  CfgRead(*this, "RadarRingsNumberVisible", &val);
  if (val.Length() > 0) g_iNavAidRadarRingsNumberVisible = atoi(val.mb_str());
  g_bNavAidRadarRingsShown = g_iNavAidRadarRingsNumberVisible > 0;

  CfgRead(*this, "RadarRingsStep", &val);
  if (val.Length() > 0) g_fNavAidRadarRingsStep = atof(val.mb_str());

  CfgRead(*this, "RadarRingsStepUnits", &g_pNavAidRadarRingsStepUnits);

  wxString l_wxsOwnshipRangeRingsColour;
  CfgRead(*this, "RadarRingsColour", &l_wxsOwnshipRangeRingsColour);
  if (l_wxsOwnshipRangeRingsColour.Length())
    g_colourOwnshipRangeRingsColour.Set(l_wxsOwnshipRangeRingsColour);

  // Waypoint Radar rings
  CfgRead(*this, "WaypointRangeRingsNumber", &val);
  if (val.Length() > 0) g_iWaypointRangeRingsNumber = atoi(val.mb_str());

  CfgRead(*this, "WaypointRangeRingsStep", &val);
  if (val.Length() > 0) g_fWaypointRangeRingsStep = atof(val.mb_str());

  CfgRead(*this, "WaypointRangeRingsStepUnits", &g_iWaypointRangeRingsStepUnits);

  wxString l_wxsWaypointRangeRingsColour;
  CfgRead(*this, "WaypointRangeRingsColour", &l_wxsWaypointRangeRingsColour);
  g_colourWaypointRangeRingsColour =
      QColor(wxString_to_QString(l_wxsWaypointRangeRingsColour));

  if (!CfgReadIf(*this, "WaypointUseScaMin", &g_bUseWptScaMin)) g_bUseWptScaMin = false;
  if (!CfgReadIf(*this, "WaypointScaMinValue", &g_iWpt_ScaMin)) g_iWpt_ScaMin = 2147483646;
  if (!CfgReadIf(*this, "WaypointScaMaxValue", &g_iWpt_ScaMax)) g_iWpt_ScaMax = 0;
  if (!CfgReadIf(*this, "WaypointUseScaMinOverrule", &g_bOverruleScaMin))
    g_bOverruleScaMin = false;
  if (!CfgReadIf(*this, "WaypointsShowName", &g_bShowWptName)) g_bShowWptName = true;
  if (!CfgReadIf(*this, "UserIconsFirst", &g_bUserIconsFirst)) g_bUserIconsFirst = true;

  //  Support Version 3.0 and prior config setting for Radar Rings
  bool b300RadarRings = true;
  if (CfgReadIf(*this, "ShowRadarRings", &b300RadarRings)) {
    if (!b300RadarRings) g_iNavAidRadarRingsNumberVisible = 0;
  }

  CfgRead(*this, "ConfirmObjectDeletion", &g_bConfirmObjectDelete);

  // Waypoint dragging with mouse
  g_bWayPointPreventDragging = false;
  CfgRead(*this, "WaypointPreventDragging", &g_bWayPointPreventDragging);

  g_bEnableZoomToCursor = false;
  CfgRead(*this, "EnableZoomToCursor", &g_bEnableZoomToCursor);

  val.Clear();
  CfgRead(*this, "TrackIntervalSeconds", &val);
  if (val.Length() > 0) {
    double tval = atof(val.mb_str());
    if (tval >= 2.) g_TrackIntervalSeconds = tval;
  }

  val.Clear();
  CfgRead(*this, "TrackDeltaDistance", &val);
  if (val.Length() > 0) {
    double tval = atof(val.mb_str());
    if (tval >= 0.05) g_TrackDeltaDistance = tval;
  }

  CfgRead(*this, "TrackPrecision", &g_nTrackPrecision);

  CfgRead(*this, "RouteLineWidth", &g_route_line_width);
  CfgRead(*this, "TrackLineWidth", &g_track_line_width);

  wxString l_wxsTrackLineColour;
  if (CfgReadIf(*this, "TrackLineColour", &l_wxsTrackLineColour))
    g_colourTrackLineColour.Set(l_wxsTrackLineColour);

  CfgRead(*this, "TideCurrentWindowScale", &g_tcwin_scale);
  CfgRead(*this, "DefaultWPIcon", &g_default_wp_icon);
  CfgRead(*this, "DataMonitorLogfile", &g_dm_logfile);
  CfgRead(*this, "DefaultRPIcon", &g_default_routepoint_icon);

  endAllGroups();
  beginGroup("MmsiProperties");
  int iPMax = childKeys().size();
  if (iPMax) {
    g_MMSI_Props_Array.clear();
    for (const QString &qstrk : pConfig->childKeys()) {
      wxString str = QString_to_wxString(qstrk);
      wxString val;
      CfgRead(*pConfig, str, &val);  // Get an entry

      MmsiProperties *pProps = new MmsiProperties(val);
      g_MMSI_Props_Array.append(pProps);
    }
  }

  endAllGroups();
  beginGroup("DataMonitor");
  g_dm_ok = ReadUnsigned("colors.ok", kUndefinedColor);
  g_dm_dropped = ReadUnsigned("colors.dropped", kUndefinedColor);
  g_dm_filtered = ReadUnsigned("colors.filtered", kUndefinedColor);
  g_dm_input = ReadUnsigned("colors.input", kUndefinedColor);
  g_dm_output = ReadUnsigned("colors.output", kUndefinedColor);
  g_dm_not_ok = ReadUnsigned("colors.not-ok", kUndefinedColor);

  return 0;
}

void MyConfig::LoadS57Config() {
  if (!ps52plib) return;

  int read_int;
  double dval;
  endAllGroups();
  beginGroup("Settings/GlobalState");

  CfgRead(*this, "bShowS57Text", &read_int, 1);
  ps52plib->SetShowS57Text(!(read_int == 0));

  CfgRead(*this, "bShowS57ImportantTextOnly", &read_int, 0);
  ps52plib->SetShowS57ImportantTextOnly(!(read_int == 0));

  CfgRead(*this, "bShowLightDescription", &read_int, 0);
  ps52plib->SetShowLdisText(!(read_int == 0));

  CfgRead(*this, "bExtendLightSectors", &read_int, 0);
  ps52plib->SetExtendLightSectors(!(read_int == 0));

  CfgRead(*this, "nDisplayCategory", &read_int, (enum _DisCat)STANDARD);
  ps52plib->SetDisplayCategory((enum _DisCat)read_int);

  CfgRead(*this, "nSymbolStyle", &read_int, (enum _LUPname)PAPER_CHART);
  ps52plib->m_nSymbolStyle = (LUPname)read_int;

  CfgRead(*this, "nBoundaryStyle", &read_int, PLAIN_BOUNDARIES);
  ps52plib->m_nBoundaryStyle = (LUPname)read_int;

  CfgRead(*this, "bShowSoundg", &read_int, 1);
  ps52plib->m_bShowSoundg = !(read_int == 0);

  CfgRead(*this, "bShowMeta", &read_int, 0);
  ps52plib->m_bShowMeta = !(read_int == 0);

  CfgRead(*this, "bUseSCAMIN", &read_int, 1);
  ps52plib->m_bUseSCAMIN = !(read_int == 0);

  CfgRead(*this, "bUseSUPER_SCAMIN", &read_int, 0);
  ps52plib->m_bUseSUPER_SCAMIN = !(read_int == 0);

  CfgRead(*this, "bShowAtonText", &read_int, 1);
  ps52plib->m_bShowAtonText = !(read_int == 0);

  CfgRead(*this, "bDeClutterText", &read_int, 0);
  ps52plib->m_bDeClutterText = !(read_int == 0);

  CfgRead(*this, "bShowNationalText", &read_int, 0);
  ps52plib->m_bShowNationalTexts = !(read_int == 0);

  CfgRead(*this, "ENCSoundingScaleFactor", &read_int, 0);
  ps52plib->m_nSoundingFactor = read_int;

  CfgRead(*this, "ENCTextScaleFactor", &read_int, 0);
  ps52plib->m_nTextFactor = read_int;

  if (CfgReadIf(*this, "S52_MAR_SAFETY_CONTOUR", &dval, 3.0)) {
    S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR, dval);
    S52_setMarinerParam(S52_MAR_SAFETY_DEPTH,
                        dval);  // Set safety_contour and safety_depth the same
  }

  if (CfgReadIf(*this, "S52_MAR_SHALLOW_CONTOUR", &dval, 2.0))
    S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, dval);

  if (CfgReadIf(*this, "S52_MAR_DEEP_CONTOUR", &dval, 6.0))
    S52_setMarinerParam(S52_MAR_DEEP_CONTOUR, dval);

  if (CfgReadIf(*this, "S52_MAR_TWO_SHADES", &dval, 0.0))
    S52_setMarinerParam(S52_MAR_TWO_SHADES, dval);

  ps52plib->UpdateMarinerParams();

  endAllGroups();
  beginGroup("Settings/GlobalState");
  CfgRead(*this, "S52_DEPTH_UNIT_SHOW", &read_int, 1);  // default is metres
  read_int = wxMax(read_int, 0);              // qualify value
  read_int = wxMin(read_int, 2);
  ps52plib->m_nDepthUnitDisplay = read_int;
  g_nDepthUnitDisplay = read_int;

  //    S57 Object Class Visibility

  OBJLElement *pOLE;

  endAllGroups();
  beginGroup("Settings/ObjectFilter");

  int iOBJMax = childKeys().size();
  if (iOBJMax) {
    wxString sObj;

    for (const QString &qstrk : pConfig->childKeys()) {
      wxString str = QString_to_wxString(qstrk);
      long val = 0;
      CfgRead(*pConfig, str, &val);  // Get an Object Viz

      bool bNeedNew = true;

      if (str.StartsWith("viz", &sObj)) {
        for (unsigned int iPtr = 0; iPtr < ps52plib->pOBJLArray->GetCount();
             iPtr++) {
          pOLE = (OBJLElement *)(ps52plib->pOBJLArray->Item(iPtr));
          if (!strncmp(pOLE->OBJLName, sObj.mb_str(), 6)) {
            pOLE->nViz = val;
            bNeedNew = false;
            break;
          }
        }

        if (bNeedNew) {
          pOLE = (OBJLElement *)calloc(sizeof(OBJLElement), 1);
          memcpy(pOLE->OBJLName, sObj.mb_str(), OBJL_NAME_LEN);
          pOLE->nViz = 1;

          ps52plib->pOBJLArray->Add((void *)pOLE);
        }
      }
    }
  }
}

bool MyConfig::LoadLayers(wxString &path) {
  wxArrayString file_array;
  Layer *l;
  QDir dir(wxString_to_QString(path));
  if (dir.exists()) {
    const QStringList entries = dir.entryList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
      file_array.Clear();
      QString full = dir.absoluteFilePath(entry);
      wxString filename = QString_to_wxString(full);
      QFileInfo f(full);
      if (f.suffix().compare("gpx", Qt::CaseInsensitive) == 0) {
        file_array.Add(filename);  // single-gpx-file layer
      } else {
        QDir subdir(full);
        if (subdir.exists()) {
          QDirIterator it(full, {"*.gpx"}, QDir::Files,
                          QDirIterator::Subdirectories);
          while (it.hasNext()) {
            file_array.Add(QString_to_wxString(it.next()));
          }
        }
      }

      if (file_array.GetCount()) {
        l = new Layer();
        l->m_LayerID = ++g_LayerIdx;
        l->m_LayerFileName = file_array[0];
        if (file_array.GetCount() <= 1)
          l->m_LayerName = QString_to_wxString(
              QFileInfo(wxString_to_QString(file_array[0])).completeBaseName());
        else
          l->m_LayerName =
              QString_to_wxString(QFileInfo(full).completeBaseName());

        bool bLayerViz = g_bShowLayers;

        if (g_VisibleLayers.Contains(l->m_LayerName)) bLayerViz = true;
        if (g_InvisibleLayers.Contains(l->m_LayerName)) bLayerViz = false;

        l->m_bHasVisibleNames = wxCHK_UNDETERMINED;
        if (g_VisiNameinLayers.Contains(l->m_LayerName))
          l->m_bHasVisibleNames = wxCHK_CHECKED;
        if (g_InVisiNameinLayers.Contains(l->m_LayerName))
          l->m_bHasVisibleNames = wxCHK_UNCHECKED;

        l->m_bIsVisibleOnChart = bLayerViz;

        wxString laymsg;
        laymsg.Printf("New layer %d: %s", l->m_LayerID, l->m_LayerName.c_str());
        wxLogMessage(laymsg);

        pLayerList->insert(pLayerList->begin(), l);

        //  Load the entire file array as a single layer

        for (unsigned int i = 0; i < file_array.GetCount(); i++) {
          wxString file_path = file_array[i];

          if (QFile::exists(wxString_to_QString(file_path))) {
            NavObjectCollection1 *pSet = new NavObjectCollection1;
            pugi::xml_parse_result result = pSet->load_file(file_path.fn_str());
            if (!result) {
              wxLogMessage("Error loading GPX file " + file_path);
              wxMessageBox(
                  wxString::Format(
                      _("Error loading GPX file %s, %s at character %d"),
                      file_path, result.description(), result.offset),
                  _("Import GPX File"));
              pSet->reset();
            }
            long nItems = pSet->LoadAllGPXObjectsAsLayer(
                l->m_LayerID, bLayerViz, l->m_bHasVisibleNames);
            l->m_NoOfItems += nItems;
            l->m_LayerType = _("Persistent");

            wxString objmsg;
            objmsg.Printf("Loaded GPX file %s with %ld items.",
                          file_path.c_str(), nItems);
            wxLogMessage(objmsg);

            delete pSet;
          }
        }
      }
    }
  }
  g_bLayersLoaded = true;

  return true;
}

bool MyConfig::LoadChartDirArray(ArrayOfCDI &ChartDirArray) {
  //    Chart Directories
  endAllGroups();
  beginGroup("ChartDirectories");
  int iDirMax = childKeys().size();
  if (iDirMax) {
    ChartDirArray.clear();
    int nAdjustChartDirs = 0;
    int iDir = 0;
    for (const QString &qstrk : pConfig->childKeys()) {
      wxString str = QString_to_wxString(qstrk);
      wxString val;
      CfgRead(*pConfig, str, &val);  // Get a Directory name

      wxString dirname(val);
      if (!dirname.IsEmpty()) {
        /*     Special case for first time run after Windows install with sample
         chart data... We desire that the sample configuration file opencpn.ini
         should not contain any installation dependencies, so... Detect and
         update the sample [ChartDirectories] entries to point to the Shared
         Data directory For instance, if the (sample) opencpn.ini file should
         contain shortcut coded entries like:

         [ChartDirectories]
         ChartDir1=SampleCharts\\MaptechRegion7

         then this entry will be updated to be something like:
         ChartDir1=c:\Program Files\opencpn\SampleCharts\\MaptechRegion7

         */
        if (dirname.Find("SampleCharts") ==
            0)  // only update entries starting with "SampleCharts"
        {
          nAdjustChartDirs++;

          CfgDelete(*pConfig, str);
          wxString new_dir = dirname.Mid(dirname.Find("SampleCharts"));
          new_dir.Prepend(g_Platform->GetSharedDataDir());
          dirname = new_dir;
        }

        ChartDirInfo cdi;
        cdi.fullpath = dirname.BeforeFirst('^');
        cdi.magic_number = dirname.AfterFirst('^');

        ChartDirArray.append(cdi);
        iDir++;
      }
    }

    if (nAdjustChartDirs) pConfig->UpdateChartDirs(ChartDirArray);
  }

  return true;
}

bool MyConfig::UpdateChartDirs(ArrayOfCDI &dir_array) {
  wxString key, dir;
  wxString str_buf;

  endAllGroups();
  beginGroup("ChartDirectories");
  int iDirMax = childKeys().size();
  if (iDirMax) {
    // Snapshot the existing keys so we can safely delete during iteration.
    const QStringList existing = childKeys();
    for (const QString &qk : existing) {
      CfgDelete(*this, QString_to_wxString(qk));
    }
  }

  iDirMax = dir_array.size();

  for (int iDir = 0; iDir < iDirMax; iDir++) {
    ChartDirInfo cdi = dir_array[iDir];

    wxString dirn = cdi.fullpath;
    dirn.Append("^");
    dirn.Append(cdi.magic_number);

    str_buf.Printf("ChartDir%d", iDir + 1);

    CfgWrite(*this, str_buf, dirn);
  }

// Avoid nonsense log errors...
#ifdef __ANDROID__
  wxLogNull logNo;
#endif

  sync();
  return true;
}

void MyConfig::CreateConfigGroups(ChartGroupArray *pGroupArray) {
  if (!pGroupArray) return;

  endAllGroups();
  beginGroup("Groups");
  CfgWrite(*this, "GroupCount", (int)pGroupArray->size());

  for (unsigned int i = 0; i < pGroupArray->size(); i++) {
    ChartGroup *pGroup = pGroupArray->at(i);
    endAllGroups();
    beginGroup(QStringLiteral("Groups/Group%1").arg(i + 1));

    CfgWrite(*this, "GroupName", pGroup->m_group_name);
    CfgWrite(*this, "GroupItemCount", (int)pGroup->m_element_array.size());

    for (unsigned int j = 0; j < pGroup->m_element_array.size(); j++) {
      endAllGroups();
      beginGroup(
          QStringLiteral("Groups/Group%1/Item%2").arg(i + 1).arg(j));
      CfgWrite(*this, "IncludeItem", pGroup->m_element_array[j].m_element_name);

      wxString t;
      const QStringList& u = pGroup->m_element_array[j].m_missing_name_array;
      if (!u.isEmpty()) {
        for (const QString& s : u) {
          t += QString_to_wxString(s);
          t += ";";
        }
        CfgWrite(*this, "ExcludeItems", t);
      }
    }
  }
}

void MyConfig::DestroyConfigGroups() {
  endAllGroups();
  remove("Groups");  // zap
}

void MyConfig::LoadConfigGroups(ChartGroupArray *pGroupArray) {
  endAllGroups();
  beginGroup("Groups");
  unsigned int group_count;
  CfgRead(*this, "GroupCount", (int *)&group_count, 0);

  for (unsigned int i = 0; i < group_count; i++) {
    ChartGroup *pGroup = new ChartGroup;
    endAllGroups();
    beginGroup(QStringLiteral("Groups/Group%1").arg(i + 1));

    wxString t;
    CfgRead(*this, "GroupName", &t);
    pGroup->m_group_name = t;

    unsigned int item_count;
    CfgRead(*this, "GroupItemCount", (int *)&item_count);
    for (unsigned int j = 0; j < item_count; j++) {
      endAllGroups();
      beginGroup(QStringLiteral("Groups/Group%1/Item%2").arg(i + 1).arg(j));

      wxString v;
      CfgRead(*this, "IncludeItem", &v);

      ChartGroupElement pelement{v};
      wxString u;
      if (CfgReadIf(*this, "ExcludeItems", &u)) {
        if (!u.IsEmpty()) {
          QStringList tokens = wxString_to_QString(u).split(
              QChar(';'), Qt::SkipEmptyParts);
          for (const QString& token : tokens) {
            pelement.m_missing_name_array.append(token);
          }
        }
      }
      pGroup->m_element_array.push_back(std::move(pelement));
    }
    pGroupArray->append(pGroup);
  }
}

void MyConfig::LoadCanvasConfigs(bool bApplyAsTemplate) {
  wxString s;
  canvasConfig *pcc;
  auto &config_array = ConfigMgr::Get().GetCanvasConfigArray();

  endAllGroups();
  beginGroup("Canvas");

  //  If the canvas config has never been set/persisted, use the global settings
  if (!CfgHasEntry(*this, "CanvasConfig")) {
    pcc = new canvasConfig(0);
    pcc->LoadFromLegacyConfig(this);
    config_array.append(pcc);

    return;
  }

  CfgRead(*this, "CanvasConfig", (int *)&g_canvasConfig, 0);

  // Do not recreate canvasConfigs when applying config dynamically
  if (config_array.size() == 0) {  // This is initial load from startup
    endAllGroups();
    beginGroup("Canvas/CanvasConfig1");
    canvasConfig *pcca = new canvasConfig(0);
    LoadConfigCanvas(pcca, bApplyAsTemplate);
    config_array.append(pcca);

    endAllGroups();
    beginGroup("Canvas/CanvasConfig2");
    pcca = new canvasConfig(1);
    LoadConfigCanvas(pcca, bApplyAsTemplate);
    config_array.append(pcca);
  } else {  // This is a dynamic (i.e. Template) load
    canvasConfig *pcca = config_array[0];
    endAllGroups();
    beginGroup("Canvas/CanvasConfig1");
    LoadConfigCanvas(pcca, bApplyAsTemplate);

    if (config_array.size() > 1) {
      canvasConfig *pcca = config_array[1];
      endAllGroups();
      beginGroup("Canvas/CanvasConfig2");
      LoadConfigCanvas(pcca, bApplyAsTemplate);
    } else {
      endAllGroups();
      beginGroup("Canvas/CanvasConfig2");
      pcca = new canvasConfig(1);
      LoadConfigCanvas(pcca, bApplyAsTemplate);
      config_array.append(pcca);
    }
  }
}

void MyConfig::LoadConfigCanvas(canvasConfig *cConfig, bool bApplyAsTemplate) {
  wxString st;
  double st_lat, st_lon;

  if (!bApplyAsTemplate) {
    //    Reasonable starting point
    cConfig->iLat = START_LAT;  // display viewpoint
    cConfig->iLon = START_LON;

    if (CfgReadIf(*this, "canvasVPLatLon", &st)) {
      sscanf(st.mb_str(wxConvUTF8), "%lf,%lf", &st_lat, &st_lon);

      //    Sanity check the lat/lon...both have to be reasonable.
      if (fabs(st_lon) < 360.) {
        while (st_lon < -180.) st_lon += 360.;

        while (st_lon > 180.) st_lon -= 360.;

        cConfig->iLon = st_lon;
      }

      if (fabs(st_lat) < 90.0) cConfig->iLat = st_lat;
    }

    cConfig->iScale = .0003;  // decent initial value
    cConfig->iRotation = 0;

    double st_view_scale;
    if (CfgReadIf(*this, wxString("canvasVPScale"), &st)) {
      sscanf(st.mb_str(wxConvUTF8), "%lf", &st_view_scale);
      //    Sanity check the scale
      st_view_scale = fmax(st_view_scale, .001 / 32);
      st_view_scale = fmin(st_view_scale, 4);
      cConfig->iScale = st_view_scale;
    }

    double st_rotation;
    if (CfgReadIf(*this, wxString("canvasVPRotation"), &st)) {
      sscanf(st.mb_str(wxConvUTF8), "%lf", &st_rotation);
      //    Sanity check the rotation
      st_rotation = fmin(st_rotation, 360);
      st_rotation = fmax(st_rotation, 0);
      cConfig->iRotation = st_rotation * PI / 180.;
    }

    CfgRead(*this, "canvasInitialdBIndex", &cConfig->DBindex, 0);
    CfgRead(*this, "canvasbFollow", &cConfig->bFollow, 0);

    CfgRead(*this, "canvasCourseUp", &cConfig->bCourseUp, 0);
    CfgRead(*this, "canvasHeadUp", &cConfig->bHeadUp, 0);
    CfgRead(*this, "canvasLookahead", &cConfig->bLookahead, 0);
  }

  CfgRead(*this, "ActiveChartGroup", &cConfig->GroupID, 0);

  // Special check for group selection when applied as template
  if (cConfig->GroupID && bApplyAsTemplate) {
    if (cConfig->GroupID > (int)g_pGroupArray->size()) cConfig->GroupID = 0;
  }

  CfgRead(*this, "canvasShowTides", &cConfig->bShowTides, 0);
  CfgRead(*this, "canvasShowCurrents", &cConfig->bShowCurrents, 0);

  CfgRead(*this, "canvasEnableBasemapTile", &cConfig->bEnableBasemapTile, 1);

  CfgRead(*this, "canvasQuilt", &cConfig->bQuilt, 1);
  CfgRead(*this, "canvasShowGrid", &cConfig->bShowGrid, 0);
  CfgRead(*this, "canvasShowOutlines", &cConfig->bShowOutlines, 0);
  CfgRead(*this, "canvasShowDepthUnits", &cConfig->bShowDepthUnits, 0);

  CfgRead(*this, "canvasShowAIS", &cConfig->bShowAIS, 1);
  CfgRead(*this, "canvasAttenAIS", &cConfig->bAttenAIS, 0);

  // ENC options
  CfgRead(*this, "canvasShowENCText", &cConfig->bShowENCText, 1);
  CfgRead(*this, "canvasENCDisplayCategory", &cConfig->nENCDisplayCategory, STANDARD);
  CfgRead(*this, "canvasENCShowDepths", &cConfig->bShowENCDepths, 1);
  CfgRead(*this, "canvasENCShowBuoyLabels", &cConfig->bShowENCBuoyLabels, 1);
  CfgRead(*this, "canvasENCShowLightDescriptions", &cConfig->bShowENCLightDescriptions,
       1);
  CfgRead(*this, "canvasENCShowLights", &cConfig->bShowENCLights, 1);
  CfgRead(*this, "canvasENCShowVisibleSectorLights",
       &cConfig->bShowENCVisibleSectorLights, 0);
  CfgRead(*this, "canvasENCShowAnchorInfo", &cConfig->bShowENCAnchorInfo, 0);
  CfgRead(*this, "canvasENCShowDataQuality", &cConfig->bShowENCDataQuality, 0);

  int sx, sy;
  CfgRead(*this, "canvasSizeX", &sx, 0);
  CfgRead(*this, "canvasSizeY", &sy, 0);
  cConfig->canvasSize = wxSize(sx, sy);
}

void MyConfig::SaveCanvasConfigs() {
  auto &config_array = ConfigMgr::Get().GetCanvasConfigArray();

  endAllGroups();
  beginGroup("Canvas");
  CfgWrite(*this, "CanvasConfig", (int)g_canvasConfig);

  canvasConfig *pcc;

  switch (g_canvasConfig) {
    case 0:
    default:

      endAllGroups();
      beginGroup("Canvas/CanvasConfig1");

      if (config_array.size() > 0) {
        pcc = config_array.at(0);
        if (pcc) {
          SaveConfigCanvas(pcc);
        }
      }
      break;

    case 1:

      if (config_array.size() > 1) {
        endAllGroups();
        beginGroup("Canvas/CanvasConfig1");
        pcc = config_array.at(0);
        if (pcc) {
          SaveConfigCanvas(pcc);
        }

        endAllGroups();
        beginGroup("Canvas/CanvasConfig2");
        pcc = config_array.at(1);
        if (pcc) {
          SaveConfigCanvas(pcc);
        }
      }
      break;
  }
}

void MyConfig::SaveConfigCanvas(canvasConfig *cConfig) {
  wxString st1;

  if (cConfig->canvas) {
    ViewPort vp = cConfig->canvas->GetVP();

    if (vp.IsValid()) {
      st1.Printf("%10.4f,%10.4f", vp.clat, vp.clon);
      CfgWrite(*this, "canvasVPLatLon", st1);
      st1.Printf("%g", vp.view_scale_ppm);
      CfgWrite(*this, "canvasVPScale", st1);
      st1.Printf("%i", ((int)(vp.rotation * 180 / PI)) % 360);
      CfgWrite(*this, "canvasVPRotation", st1);
    }

    int restore_dbindex = 0;
    ChartStack *pcs = cConfig->canvas->GetpCurrentStack();
    if (pcs) restore_dbindex = pcs->GetCurrentEntrydbIndex();
    if (cConfig->canvas->GetQuiltMode())
      restore_dbindex = cConfig->canvas->GetQuiltReferenceChartIndex();
    CfgWrite(*this, "canvasInitialdBIndex", restore_dbindex);

    CfgWrite(*this, "canvasbFollow", cConfig->canvas->m_bFollow);
    CfgWrite(*this, "ActiveChartGroup", cConfig->canvas->m_groupIndex);

    CfgWrite(*this, "canvasQuilt", cConfig->canvas->GetQuiltMode());
    CfgWrite(*this, "canvasShowGrid", cConfig->canvas->GetShowGrid());
    CfgWrite(*this, "canvasShowOutlines", cConfig->canvas->GetShowOutlines());
    CfgWrite(*this, "canvasShowDepthUnits", cConfig->canvas->GetShowDepthUnits());

    CfgWrite(*this, "canvasShowAIS", cConfig->canvas->GetShowAIS());
    CfgWrite(*this, "canvasAttenAIS", cConfig->canvas->GetAttenAIS());

    CfgWrite(*this, "canvasShowTides", cConfig->canvas->GetbShowTide());
    CfgWrite(*this, "canvasShowCurrents", cConfig->canvas->GetbShowCurrent());

    CfgWrite(*this, "canvasEnableBasemapTile", cConfig->canvas->GetbEnableBasemapTile());

    // ENC options
    CfgWrite(*this, "canvasShowENCText", cConfig->canvas->GetShowENCText());
    CfgWrite(*this, "canvasENCDisplayCategory", cConfig->canvas->GetENCDisplayCategory());
    CfgWrite(*this, "canvasENCShowDepths", cConfig->canvas->GetShowENCDepth());
    CfgWrite(*this, "canvasENCShowBuoyLabels", cConfig->canvas->GetShowENCBuoyLabels());
    CfgWrite(*this, "canvasENCShowLightDescriptions",
          cConfig->canvas->GetShowENCLightDesc());
    CfgWrite(*this, "canvasENCShowLights", cConfig->canvas->GetShowENCLights());
    CfgWrite(*this, "canvasENCShowVisibleSectorLights",
          cConfig->canvas->GetShowVisibleSectors());
    CfgWrite(*this, "canvasENCShowAnchorInfo", cConfig->canvas->GetShowENCAnchor());
    CfgWrite(*this, "canvasENCShowDataQuality", cConfig->canvas->GetShowENCDataQual());
    CfgWrite(*this, "canvasCourseUp", cConfig->canvas->GetUpMode() == COURSE_UP_MODE);
    CfgWrite(*this, "canvasHeadUp", cConfig->canvas->GetUpMode() == HEAD_UP_MODE);
    CfgWrite(*this, "canvasLookahead", cConfig->canvas->GetLookahead());

    int width = cConfig->canvas->GetSize().x;
    //         if(cConfig->canvas->IsPrimaryCanvas()){
    //             width = wxMax(width, gFrame->GetClientSize().x / 10);
    //         }
    //         else{
    //             width = wxMin(width, gFrame->GetClientSize().x  * 9 / 10);
    //         }

    CfgWrite(*this, "canvasSizeX", width);
    CfgWrite(*this, "canvasSizeY", cConfig->canvas->GetSize().y);
  }
}

void MyConfig::UpdateSettings() {
  //  Temporarily suppress logging of trivial non-fatal wxLogSysError() messages
  //  provoked by Android security...
#ifdef __ANDROID__
  wxLogNull logNo;
#endif

  //    Global options and settings
  endAllGroups();
  beginGroup("Settings");

  CfgWrite(*this, "LastAppliedTemplate", g_lastAppliedTemplateGUID);
  CfgWrite(*this, "CompatOS", g_compatOS);
  CfgWrite(*this, "CompatOsVersion", g_compatOsVersion);
  CfgWrite(*this, "ConfigVersionString", g_config_version_string);
  if (wxIsEmpty(g_CmdSoundString)) g_CmdSoundString = wxString(OCPN_SOUND_CMD);
  CfgWrite(*this, "CmdSoundString", g_CmdSoundString);
  CfgWrite(*this, "NavMessageShown", n_NavMessageShown);
  CfgWrite(*this, "InlandEcdis", g_bInlandEcdis);

  CfgWrite(*this, "AndroidVersionCode", g_AndroidVersionCode);

  CfgWrite(*this, "UIexpert", g_bUIexpert);
  CfgWrite(*this, "SpaceDropMark", g_bSpaceDropMark);
  //    CfgReadStr(*this, "UIStyle", g_StyleManager->GetStyleNextInvocation());
  //    //Not desired for O5 MUI

  CfgWrite(*this, "ShowStatusBar", g_bShowStatusBar);
#ifndef __WXOSX__
  CfgWrite(*this, "ShowMenuBar", g_bShowMenuBar);
#endif
  CfgWrite(*this, "DefaultFontSize", g_default_font_size);
  CfgWrite(*this, "DefaultFontFacename", g_default_font_facename);

  CfgWrite(*this, "Fullscreen", g_bFullscreen);
  CfgWrite(*this, "ShowCompassWindow", g_bShowCompassWin);
  CfgWrite(*this, "SetSystemTime", s_bSetSystemTime);
  CfgWrite(*this, "ShowGrid", g_bDisplayGrid);
  CfgWrite(*this, "PlayShipsBells", g_bPlayShipsBells);
  CfgWrite(*this, "SoundDeviceIndex", g_iSoundDeviceIndex);
  CfgWrite(*this, "FullscreenToolbar", g_bFullscreenToolbar);
  CfgWrite(*this, "TransparentToolbar", g_bTransparentToolbar);
  CfgWrite(*this, "PermanentMOBIcon", g_bPermanentMOBIcon);
  CfgWrite(*this, "ShowLayers", g_bShowLayers);
  CfgWrite(*this, "AutoAnchorDrop", g_bAutoAnchorMark);
  CfgWrite(*this, "ShowChartOutlines", g_bShowOutlines);
  CfgWrite(*this, "ShowActiveRouteTotal", g_bShowRouteTotal);
  CfgWrite(*this, "ShowActiveRouteHighway", g_bShowActiveRouteHighway);
  CfgWrite(*this, "SDMMFormat", g_iSDMMFormat);
  CfgWrite(*this, "MostRecentGPSUploadConnection", g_uploadConnection);
  CfgWrite(*this, "ShowChartBar", g_bShowChartBar);

  CfgWrite(*this, "GUIScaleFactor", g_GUIScaleFactor);
  CfgWrite(*this, "ChartObjectScaleFactor", g_ChartScaleFactor);
  CfgWrite(*this, "ShipScaleFactor", g_ShipScaleFactor);
  CfgWrite(*this, "ENCSoundingScaleFactor", g_ENCSoundingScaleFactor);
  CfgWrite(*this, "ENCTextScaleFactor", g_ENCTextScaleFactor);
  CfgWrite(*this, "ObjQueryAppendFilesExt", g_ObjQFileExt);

  // Plugin catalog persistent values.
  CfgWrite(*this, "CatalogCustomURL", g_catalog_custom_url);
  CfgWrite(*this, "CatalogChannel", g_catalog_channel);

  CfgWrite(*this, "NetmaskBits", g_netmask_bits);
  CfgWrite(*this, "FilterNMEA_Avg", g_bfilter_cogsog);
  CfgWrite(*this, "FilterNMEA_Sec", g_COGFilterSec);

  CfgWrite(*this, "TrackContinuous", g_btrackContinuous);

  CfgWrite(*this, "ShowTrue", g_bShowTrue);
  CfgWrite(*this, "ShowMag", g_bShowMag);
  CfgWrite(*this, "UserMagVariation", wxString::Format("%.2f", g_UserVar));

  CfgWrite(*this, "CM93DetailFactor", g_cm93_zoom_factor);
  CfgWrite(*this, "CM93DetailZoomPosX", g_detailslider_dialog_x);
  CfgWrite(*this, "CM93DetailZoomPosY", g_detailslider_dialog_y);
  CfgWrite(*this, "ShowCM93DetailSlider", g_bShowDetailSlider);

  CfgWrite(*this, "SkewToNorthUp", g_bskew_comp);
  if (!g_bdisable_opengl) {  // Only modify the saved value if OpenGL is not
                             // force-disabled from the command line
    CfgWrite(*this, "OpenGL", g_bopengl);
  }
  CfgWrite(*this, "SoftwareGL", g_bSoftwareGL);

  CfgWrite(*this, "ZoomDetailFactor", g_chart_zoom_modifier_raster);
  CfgWrite(*this, "ZoomDetailFactorVector", g_chart_zoom_modifier_vector);

  CfgWrite(*this, "FogOnOverzoom", g_fog_overzoom);
  CfgWrite(*this, "OverzoomVectorScale", g_oz_vector_scale);
  CfgWrite(*this, "OverzoomEmphasisBase", g_overzoom_emphasis_base);
  CfgWrite(*this, "PlusMinusZoomFactor", g_plus_minus_zoom_factor);
  CfgWrite(*this, "MouseZoomSensitivity",
        MouseZoom::ui_to_config(g_mouse_zoom_sensitivity_ui));
  CfgWrite(*this, "ShowMUIZoomButtons", g_bShowMuiZoomButtons);

#ifdef ocpnUSE_GL
  /* opengl options */
  CfgWrite(*this, "UseAcceleratedPanning", g_GLOptions.m_bUseAcceleratedPanning);

  CfgWrite(*this, "GPUTextureCompression", g_GLOptions.m_bTextureCompression);
  CfgWrite(*this, "GPUTextureCompressionCaching",
        g_GLOptions.m_bTextureCompressionCaching);
  CfgWrite(*this, "GPUTextureDimension", g_GLOptions.m_iTextureDimension);
  CfgWrite(*this, "GPUTextureMemSize", g_GLOptions.m_iTextureMemorySize);
  CfgWrite(*this, "PolygonSmoothing", g_GLOptions.m_GLPolygonSmoothing);
  CfgWrite(*this, "LineSmoothing", g_GLOptions.m_GLLineSmoothing);
#endif
  CfgWrite(*this, "SmoothPanZoom", g_bsmoothpanzoom);

  CfgWrite(*this, "CourseUpMode", g_bCourseUp);
  if (!g_bInlandEcdis) CfgWrite(*this, "LookAheadMode", g_bLookAhead);
  CfgWrite(*this, "TenHzUpdate", g_btenhertz);

  CfgWrite(*this, "COGUPAvgSeconds", g_COGAvgSec);
  CfgWrite(*this, "UseMagAPB", g_bMagneticAPB);

  CfgWrite(*this, "OwnshipCOGPredictorMinutes", g_ownship_predictor_minutes);
  CfgWrite(*this, "OwnshipCOGPredictorStyle", g_cog_predictor_style);
  CfgWrite(*this, "OwnshipCOGPredictorColor", g_cog_predictor_color);
  CfgWrite(*this, "OwnshipCOGPredictorEndmarker", g_cog_predictor_endmarker);
  CfgWrite(*this, "OwnshipCOGPredictorWidth", g_cog_predictor_width);
  CfgWrite(*this, "OwnshipHDTPredictorStyle", g_ownship_HDTpredictor_style);
  CfgWrite(*this, "OwnshipHDTPredictorColor", g_ownship_HDTpredictor_color);
  CfgWrite(*this, "OwnshipHDTPredictorEndmarker", g_ownship_HDTpredictor_endmarker);
  CfgWrite(*this, "OwnShipMMSINumber", g_OwnShipmmsi);
  CfgWrite(*this, "OwnshipHDTPredictorWidth", g_ownship_HDTpredictor_width);
  CfgWrite(*this, "OwnshipHDTPredictorMiles", g_ownship_HDTpredictor_miles);

  CfgWrite(*this, "OwnShipIconType", g_OwnShipIconType);
  CfgWrite(*this, "OwnShipLength", g_n_ownship_length_meters);
  CfgWrite(*this, "OwnShipWidth", g_n_ownship_beam_meters);
  CfgWrite(*this, "OwnShipGPSOffsetX", g_n_gps_antenna_offset_x);
  CfgWrite(*this, "OwnShipGPSOffsetY", g_n_gps_antenna_offset_y);
  CfgWrite(*this, "OwnShipMinSize", g_n_ownship_min_mm);
  CfgWrite(*this, "ShowDirectRouteLine", g_bShowShipToActive);
  CfgWrite(*this, "DirectRouteLineStyle", g_shipToActiveStyle);
  CfgWrite(*this, "DirectRouteLineColor", g_shipToActiveColor);

  wxString racr;
  //   racr.Printf( "%g", g_n_arrival_circle_radius );
  //   CfgReadStr(*this, "RouteArrivalCircleRadius", racr);
  CfgWrite(*this, "RouteArrivalCircleRadius",
        wxString::Format("%.2f", g_n_arrival_circle_radius));

  CfgWrite(*this, "ChartQuilting", g_bQuiltEnable);

  CfgWrite(*this, "PreserveScaleOnX", g_bPreserveScaleOnX);

  CfgWrite(*this, "StartWithTrackActive", g_bTrackCarryOver);
  CfgWrite(*this, "AutomaticDailyTracks", g_bTrackDaily);
  CfgWrite(*this, "TrackRotateAt", g_track_rotate_time);
  CfgWrite(*this, "TrackRotateTimeType", g_track_rotate_time_type);
  CfgWrite(*this, "HighlightTracks", g_bHighliteTracks);

  CfgWrite(*this, "DateTimeFormat", g_datetime_format);
  CfgWrite(*this, "InitialStackIndex", g_restore_stackindex);
  CfgWrite(*this, "InitialdBIndex", g_restore_dbindex);

  CfgWrite(*this, "NMEAAPBPrecision", g_NMEAAPBPrecision);

  CfgWrite(*this, "TalkerIdText", g_TalkerIdText);
  CfgWrite(*this, "ShowTrackPointTime", g_bShowTrackPointTime);

  CfgWrite(*this, "AnchorWatch1GUID", g_AW1GUID);
  CfgWrite(*this, "AnchorWatch2GUID", g_AW2GUID);

  CfgWrite(*this, "ToolbarX", g_maintoolbar_x);
  CfgWrite(*this, "ToolbarY", g_maintoolbar_y);
  // CfgReadStr(*this, "ToolbarOrient", g_maintoolbar_orient);

  CfgWrite(*this, "iENCToolbarX", g_iENCToolbarPosX);
  CfgWrite(*this, "iENCToolbarY", g_iENCToolbarPosY);

  if (!g_bInlandEcdis) {
    CfgWrite(*this, "GlobalToolbarConfig", g_toolbarConfig);
    CfgWrite(*this, "DistanceFormat", g_iDistanceFormat);
    CfgWrite(*this, "SpeedFormat", g_iSpeedFormat);
    CfgWrite(*this, "WindSpeedFormat", g_iWindSpeedFormat);
    CfgWrite(*this, "ShowDepthUnits", g_bShowDepthUnits);
    CfgWrite(*this, "TemperatureFormat", g_iTempFormat);
    CfgWrite(*this, "HeightFormat", g_iHeightFormat);
  }
  CfgWrite(*this, "GPSIdent", g_GPS_Ident);
  CfgWrite(*this, "ActiveRoute", g_active_route);
  CfgWrite(*this, "PersistActiveRoute", g_persist_active_route);
  CfgWrite(*this, "AlwaysSendRmbRmc", g_always_send_rmb_rmc);

  CfgWrite(*this, "UseGarminHostUpload", g_bGarminHostUpload);

  CfgWrite(*this, "MobileTouch", g_btouch);
  CfgWrite(*this, "ResponsiveGraphics", g_bresponsive);
  CfgWrite(*this, "EnableRolloverBlock", g_bRollover);

  CfgWrite(*this, "AutoHideToolbar", g_bAutoHideToolbar);
  CfgWrite(*this, "AutoHideToolbarSecs", g_nAutoHideToolbar);

  wxString st0;
  for (const auto &mm : g_config_display_size_mm) {
    st0.Append(wxString::Format("%zu,", mm));
  }
  st0.RemoveLast();  // Strip last comma
  CfgWrite(*this, "DisplaySizeMM", st0);
  CfgWrite(*this, "DisplaySizeManual", g_config_display_size_manual);

  CfgWrite(*this, "SelectionRadiusMM", g_selection_radius_mm);
  CfgWrite(*this, "SelectionRadiusTouchMM", g_selection_radius_touch_mm);

  st0.Printf("%g", g_PlanSpeed);
  CfgWrite(*this, "PlanSpeed", st0);

  if (g_bLayersLoaded) {
    wxString vis, invis, visnames, invisnames;
    LayerList::iterator it;
    int index = 0;
    for (it = (*pLayerList).begin(); it != (*pLayerList).end(); ++it, ++index) {
      Layer *lay = (Layer *)(*it);
      if (lay->IsVisibleOnChart())
        vis += (lay->m_LayerName) + ";";
      else
        invis += (lay->m_LayerName) + ";";

      if (lay->HasVisibleNames() == wxCHK_CHECKED) {
        visnames += (lay->m_LayerName) + ";";
      } else if (lay->HasVisibleNames() == wxCHK_UNCHECKED) {
        invisnames += (lay->m_LayerName) + ";";
      }
    }
    CfgWrite(*this, "VisibleLayers", vis);
    CfgWrite(*this, "InvisibleLayers", invis);
    CfgWrite(*this, "VisNameInLayers", visnames);
    CfgWrite(*this, "InvisNameInLayers", invisnames);
  }
  CfgWrite(*this, "Locale", g_locale);
  CfgWrite(*this, "LocaleOverride", g_localeOverride);

  CfgWrite(*this, "KeepNavobjBackups", g_navobjbackups);
  CfgWrite(*this, "LegacyInputCOMPortFilterBehaviour", g_b_legacy_input_filter_behaviour);
  CfgWrite(*this, "AdvanceRouteWaypointOnArrivalOnly",
        g_bAdvanceRouteWaypointOnArrivalOnly);
  CfgWrite(*this, "EnableRootMenuDebug", g_enable_root_menu_debug);

  // LIVE ETA OPTION
  CfgWrite(*this, "LiveETA", g_bShowLiveETA);
  CfgWrite(*this, "DefaultBoatSpeed", g_defaultBoatSpeed);

  //    S57 Object Filter Settings

  endAllGroups();
  beginGroup("Settings/ObjectFilter");

  if (ps52plib) {
    for (unsigned int iPtr = 0; iPtr < ps52plib->pOBJLArray->GetCount();
         iPtr++) {
      OBJLElement *pOLE = (OBJLElement *)(ps52plib->pOBJLArray->Item(iPtr));

      wxString st1("viz");
      char name[7];
      strncpy(name, pOLE->OBJLName, 6);
      name[6] = 0;
      st1.Append(wxString(name, wxConvUTF8));
      CfgWrite(*this, st1, pOLE->nViz);
    }
  }

  //    Global State

  endAllGroups();
  beginGroup("Settings/GlobalState");

  wxString st1;

  //     if( cc1 ) {
  //         ViewPort vp = cc1->GetVP();
  //
  //         if( vp.IsValid() ) {
  //             st1.Printf( "%10.4f,%10.4f", vp.clat, vp.clon );
  //             CfgReadStr(*this, "VPLatLon", st1);
  //             st1.Printf( "%g", vp.view_scale_ppm );
  //             CfgReadStr(*this, "VPScale", st1);
  //             st1.Printf( "%i", ((int)(vp.rotation * 180 / PI)) % 360
  //             ); CfgReadStr(*this, "VPRotation", st1);
  //         }
  //     }

  st1.Printf("%10.4f, %10.4f", gLat, gLon);
  CfgWrite(*this, "OwnShipLatLon", st1);

  //    Various Options
  endAllGroups();
  beginGroup("Settings/GlobalState");
  if (!g_bInlandEcdis)
    CfgWrite(*this, "nColorScheme", (int)user_colors::GetColorScheme());

  CfgWrite(*this, "FrameWinX", g_nframewin_x);
  CfgWrite(*this, "FrameWinY", g_nframewin_y);
  CfgWrite(*this, "FrameWinPosX", g_nframewin_posx);
  CfgWrite(*this, "FrameWinPosY", g_nframewin_posy);
  CfgWrite(*this, "FrameMax", g_bframemax);

  CfgWrite(*this, "ClientPosX", g_lastClientRectx);
  CfgWrite(*this, "ClientPosY", g_lastClientRecty);
  CfgWrite(*this, "ClientSzX", g_lastClientRectw);
  CfgWrite(*this, "ClientSzY", g_lastClientRecth);

  CfgWrite(*this, "S52_DEPTH_UNIT_SHOW", g_nDepthUnitDisplay);

  CfgWrite(*this, "RoutePropSizeX", g_route_prop_sx);
  CfgWrite(*this, "RoutePropSizeY", g_route_prop_sy);
  CfgWrite(*this, "RoutePropPosX", g_route_prop_x);
  CfgWrite(*this, "RoutePropPosY", g_route_prop_y);

  // Sounds
  endAllGroups();
  beginGroup("Settings/Audio");
  CfgWrite(*this, "AISAlertSoundFile", g_AIS_sound_file);
  CfgWrite(*this, "DSCAlertSoundFile", g_DSC_sound_file);
  CfgWrite(*this, "SARTAlertSoundFile", g_SART_sound_file);
  CfgWrite(*this, "AnchorAlarmSoundFile", g_anchorwatch_sound_file);

  CfgWrite(*this, "bAIS_GCPA_AlertAudio", g_bAIS_GCPA_Alert_Audio);
  CfgWrite(*this, "bAIS_SART_AlertAudio", g_bAIS_SART_Alert_Audio);
  CfgWrite(*this, "bAIS_DSC_AlertAudio", g_bAIS_DSC_Alert_Audio);
  CfgWrite(*this, "bAnchorAlertAudio", g_bAnchor_Alert_Audio);

  //    AIS
  endAllGroups();
  beginGroup("Settings/AIS");

  CfgWrite(*this, "bNoCPAMax", g_bCPAMax);
  CfgWrite(*this, "NoCPAMaxNMi", g_CPAMax_NM);
  CfgWrite(*this, "bCPAWarn", g_bCPAWarn);
  CfgWrite(*this, "CPAWarnNMi", g_CPAWarn_NM);
  CfgWrite(*this, "bTCPAMax", g_bTCPA_Max);
  CfgWrite(*this, "TCPAMaxMinutes", g_TCPA_Max);
  CfgWrite(*this, "bMarkLostTargets", g_bMarkLost);
  CfgWrite(*this, "MarkLost_Minutes", g_MarkLost_Mins);
  CfgWrite(*this, "bRemoveLostTargets", g_bRemoveLost);
  CfgWrite(*this, "RemoveLost_Minutes", g_RemoveLost_Mins);
  CfgWrite(*this, "bShowCOGArrows", g_bShowCOG);
  CfgWrite(*this, "bSyncCogPredictors", g_bSyncCogPredictors);
  CfgWrite(*this, "CogArrowMinutes", g_ShowCOG_Mins);
  CfgWrite(*this, "bShowTargetTracks", g_bAISShowTracks);
  CfgWrite(*this, "TargetTracksMinutes", g_AISShowTracks_Mins);

  CfgWrite(*this, "bHideMooredTargets", g_bHideMoored);
  CfgWrite(*this, "MooredTargetMaxSpeedKnots", g_ShowMoored_Kts);

  CfgWrite(*this, "bAISAlertDialog", g_bAIS_CPA_Alert);
  CfgWrite(*this, "bAISAlertAudio", g_bAIS_CPA_Alert_Audio);

  CfgWrite(*this, "AISAlertAudioFile", g_sAIS_Alert_Sound_File);
  CfgWrite(*this, "bAISAlertSuppressMoored", g_bAIS_CPA_Alert_Suppress_Moored);
  CfgWrite(*this, "bShowAreaNotices", g_bShowAreaNotices);
  CfgWrite(*this, "bDrawAISSize", g_bDrawAISSize);
  CfgWrite(*this, "bDrawAISRealtime", g_bDrawAISRealtime);
  CfgWrite(*this, "AISRealtimeMinSpeedKnots", g_AIS_RealtPred_Kts);
  CfgWrite(*this, "bShowAISName", g_bShowAISName);
  CfgWrite(*this, "ShowAISTargetNameScale", g_Show_Target_Name_Scale);
  CfgWrite(*this, "bWplIsAprsPositionReport", g_bWplUsePosition);
  CfgWrite(*this, "WplSelAction", g_WplAction);
  CfgWrite(*this, "AISCOGPredictorWidth", g_ais_cog_predictor_width);
  CfgWrite(*this, "bShowScaledTargets", g_bAllowShowScaled);
  CfgWrite(*this, "AISScaledNumber", g_ShowScaled_Num);
  CfgWrite(*this, "AISScaledNumberWeightSOG", g_ScaledNumWeightSOG);
  CfgWrite(*this, "AISScaledNumberWeightCPA", g_ScaledNumWeightCPA);
  CfgWrite(*this, "AISScaledNumberWeightTCPA", g_ScaledNumWeightTCPA);
  CfgWrite(*this, "AISScaledNumberWeightRange", g_ScaledNumWeightRange);
  CfgWrite(*this, "AISScaledNumberWeightSizeOfTarget", g_ScaledNumWeightSizeOfT);
  CfgWrite(*this, "AISScaledSizeMinimal", g_ScaledSizeMinimal);
  CfgWrite(*this, "AISShowScaled", g_bShowScaled);

  CfgWrite(*this, "AlertDialogSizeX", g_ais_alert_dialog_sx);
  CfgWrite(*this, "AlertDialogSizeY", g_ais_alert_dialog_sy);
  CfgWrite(*this, "AlertDialogPosX", g_ais_alert_dialog_x);
  CfgWrite(*this, "AlertDialogPosY", g_ais_alert_dialog_y);
  CfgWrite(*this, "QueryDialogPosX", g_ais_query_dialog_x);
  CfgWrite(*this, "QueryDialogPosY", g_ais_query_dialog_y);
  CfgWrite(*this, "AISTargetListPerspective", g_AisTargetList_perspective);
  CfgWrite(*this, "AISTargetListRange", g_AisTargetList_range);
  CfgWrite(*this, "AISTargetListSortColumn", g_AisTargetList_sortColumn);
  CfgWrite(*this, "bAISTargetListSortReverse", g_bAisTargetList_sortReverse);
  CfgWrite(*this, "AISTargetListColumnSpec", g_AisTargetList_column_spec);
  CfgWrite(*this, "AISTargetListColumnOrder", g_AisTargetList_column_order);

  CfgWrite(*this, "S57QueryDialogSizeX", g_S57_dialog_sx);
  CfgWrite(*this, "S57QueryDialogSizeY", g_S57_dialog_sy);
  CfgWrite(*this, "S57QueryExtraDialogSizeX", g_S57_extradialog_sx);
  CfgWrite(*this, "S57QueryExtraDialogSizeY", g_S57_extradialog_sy);

  CfgWrite(*this, "bAISRolloverShowClass", g_bAISRolloverShowClass);
  CfgWrite(*this, "bAISRolloverShowCOG", g_bAISRolloverShowCOG);
  CfgWrite(*this, "bAISRolloverShowCPA", g_bAISRolloverShowCPA);

  CfgWrite(*this, "bAISAlertAckTimeout", g_bAIS_ACK_Timeout);
  CfgWrite(*this, "AlertAckTimeoutMinutes", g_AckTimeout_Mins);

  endAllGroups();
  beginGroup("Settings/GlobalState");
  if (ps52plib) {
    CfgWrite(*this, "bShowS57Text", ps52plib->GetShowS57Text());
    CfgWrite(*this, "bShowS57ImportantTextOnly", ps52plib->GetShowS57ImportantTextOnly());
    if (!g_bInlandEcdis)
      CfgWrite(*this, "nDisplayCategory", (long)ps52plib->GetDisplayCategory());
    CfgWrite(*this, "nSymbolStyle", (int)ps52plib->m_nSymbolStyle);
    CfgWrite(*this, "nBoundaryStyle", (int)ps52plib->m_nBoundaryStyle);

    CfgWrite(*this, "bShowSoundg", ps52plib->m_bShowSoundg);
    CfgWrite(*this, "bShowMeta", ps52plib->m_bShowMeta);
    CfgWrite(*this, "bUseSCAMIN", ps52plib->m_bUseSCAMIN);
    CfgWrite(*this, "bUseSUPER_SCAMIN", ps52plib->m_bUseSUPER_SCAMIN);
    CfgWrite(*this, "bShowAtonText", ps52plib->m_bShowAtonText);
    CfgWrite(*this, "bShowLightDescription", ps52plib->m_bShowLdisText);
    CfgWrite(*this, "bExtendLightSectors", ps52plib->m_bExtendLightSectors);
    CfgWrite(*this, "bDeClutterText", ps52plib->m_bDeClutterText);
    CfgWrite(*this, "bShowNationalText", ps52plib->m_bShowNationalTexts);

    CfgWrite(*this, "S52_MAR_SAFETY_CONTOUR",
          S52_getMarinerParam(S52_MAR_SAFETY_CONTOUR));
    CfgWrite(*this, "S52_MAR_SHALLOW_CONTOUR",
          S52_getMarinerParam(S52_MAR_SHALLOW_CONTOUR));
    CfgWrite(*this, "S52_MAR_DEEP_CONTOUR", S52_getMarinerParam(S52_MAR_DEEP_CONTOUR));
    CfgWrite(*this, "S52_MAR_TWO_SHADES", S52_getMarinerParam(S52_MAR_TWO_SHADES));
    CfgWrite(*this, "S52_DEPTH_UNIT_SHOW", ps52plib->m_nDepthUnitDisplay);
    CfgWrite(*this, "ENCSoundingScaleFactor", g_ENCSoundingScaleFactor);
    CfgWrite(*this, "ENCTextScaleFactor", g_ENCTextScaleFactor);
  }
  endAllGroups();
  beginGroup("Directories");
  CfgWrite(*this, "S57DataLocation", "");
  //    CfgReadStr(*this, "SENCFileLocation", "");

  endAllGroups();
  beginGroup("Directories");
  CfgWrite(*this, "InitChartDir", *pInit_Chart_Dir);
  CfgWrite(*this, "GPXIODir", g_gpx_path);
  CfgWrite(*this, "TCDataDir", g_TCData_Dir);
  CfgWrite(*this, "BasemapDir", g_Platform->NormalizePath(gWorldMapLocation));
  if (gWorldShapefileLocation.Length())
    CfgWrite(*this, "BaseShapefileDir",
          g_Platform->NormalizePath(gWorldShapefileLocation));
  CfgWrite(*this, "pluginInstallDir", g_Platform->NormalizePath(g_winPluginDir));

  endAllGroups();
  beginGroup("Settings/NMEADataSource");
  wxString connectionconfigs;
  for (size_t i = 0; i < TheConnectionParams().size(); i++) {
    if (i > 0) connectionconfigs.Append("|");
    connectionconfigs.Append(TheConnectionParams()[i]->Serialize());
  }
  CfgWrite(*this, "DataConnections", connectionconfigs);

  //    Fonts

  //  Store the persistent Auxiliary Font descriptor Keys
  endAllGroups();
  beginGroup("Settings/AuxFontKeys");

  QStringList keyArray = FontMgr::Get().GetAuxKeyArray();
  for (int i = 0; i < keyArray.size(); i++) {
    wxString key;
    key.Printf("Key%i", i);
    wxString keyval = QString_to_wxString(keyArray[i]);
    CfgWrite(*this, key, keyval);
  }

  wxString font_path;
#ifdef __WXX11__
  font_path = ("/Settings/X11Fonts");
#endif

#ifdef __WXGTK__
  font_path = ("/Settings/GTKFonts");
#endif

#ifdef __WXMSW__
  font_path = ("/Settings/MSWFonts");
#endif

#ifdef __WXMAC__
  font_path = ("/Settings/MacFonts");
#endif

#ifdef __WXQT__
  font_path = ("/Settings/QTFonts");
#endif

  // font_path is e.g. "/Settings/MSWFonts" -- a section path. Wipe any
  // previous state at that location then enter the group fresh.
  endAllGroups();
  {
    QString fp = wxString_to_QString(font_path);
    if (fp.startsWith('/')) fp.remove(0, 1);
    remove(fp);
    beginGroup(fp);
  }

  int nFonts = FontMgr::Get().GetNumFonts();

  for (int i = 0; i < nFonts; i++) {
    wxString cfstring(FontMgr::Get().GetConfigString(i));
    wxString valstring = FontMgr::Get().GetFullConfigDesc(i);
    CfgWrite(*this, cfstring, valstring);
  }

  //  Tide/Current Data Sources
  endAllGroups();
  if (childGroups().contains("TideCurrentDataSources"))
    remove("TideCurrentDataSources");
  beginGroup("TideCurrentDataSources");
  unsigned int id = 0;
  for (auto val : TideCurrentDataSet) {
    wxString key;
    key.Printf("tcds%d", id);
    CfgWrite(*this, key, wxString(val));
    ++id;
  }

  endAllGroups();
  beginGroup("Settings/Others");

  // Radar rings
  CfgWrite(*this, "ShowRadarRings",
        (bool)(g_iNavAidRadarRingsNumberVisible > 0));  // 3.0.0 config support
  CfgWrite(*this, "RadarRingsNumberVisible", g_iNavAidRadarRingsNumberVisible);
  CfgWrite(*this, "RadarRingsStep", g_fNavAidRadarRingsStep);
  CfgWrite(*this, "RadarRingsStepUnits", g_pNavAidRadarRingsStepUnits);
  CfgWrite(*this, "RadarRingsColour",
        g_colourOwnshipRangeRingsColour.GetAsString(wxC2S_HTML_SYNTAX));
  CfgWrite(*this, "WaypointUseScaMin", g_bUseWptScaMin);
  CfgWrite(*this, "WaypointScaMinValue", g_iWpt_ScaMin);
  CfgWrite(*this, "WaypointScaMaxValue", g_iWpt_ScaMax);
  CfgWrite(*this, "WaypointUseScaMinOverrule", g_bOverruleScaMin);
  CfgWrite(*this, "WaypointsShowName", g_bShowWptName);
  CfgWrite(*this, "UserIconsFirst", g_bUserIconsFirst);

  // Waypoint Radar rings
  CfgWrite(*this, "WaypointRangeRingsNumber", g_iWaypointRangeRingsNumber);
  CfgWrite(*this, "WaypointRangeRingsStep", g_fWaypointRangeRingsStep);
  CfgWrite(*this, "WaypointRangeRingsStepUnits", g_iWaypointRangeRingsStepUnits);
  CfgWrite(*this, "WaypointRangeRingsColour",
           QString_to_wxString(g_colourWaypointRangeRingsColour.name()));

  CfgWrite(*this, "ConfirmObjectDeletion", g_bConfirmObjectDelete);

  // Waypoint dragging with mouse; toh, 2009.02.24
  CfgWrite(*this, "WaypointPreventDragging", g_bWayPointPreventDragging);

  CfgWrite(*this, "EnableZoomToCursor", g_bEnableZoomToCursor);

  CfgWrite(*this, "TrackIntervalSeconds", g_TrackIntervalSeconds);
  CfgWrite(*this, "TrackDeltaDistance", g_TrackDeltaDistance);
  CfgWrite(*this, "TrackPrecision", g_nTrackPrecision);

  CfgWrite(*this, "RouteLineWidth", g_route_line_width);
  CfgWrite(*this, "TrackLineWidth", g_track_line_width);
  CfgWrite(*this, "TrackLineColour",
        g_colourTrackLineColour.GetAsString(wxC2S_HTML_SYNTAX));
  CfgWrite(*this, "DefaultWPIcon", g_default_wp_icon);
  CfgWrite(*this, "DataMonitorLogfile", g_dm_logfile);
  CfgWrite(*this, "DefaultRPIcon", g_default_routepoint_icon);

  endAllGroups();
  remove("MmsiProperties");
  beginGroup("MmsiProperties");
  for (unsigned int i = 0; i < g_MMSI_Props_Array.size(); i++) {
    wxString p;
    p.Printf("Props%d", i);
    CfgWrite(*this, p, g_MMSI_Props_Array[i]->Serialize());
  }
  endAllGroups();
  beginGroup("DataMonitor");
  CfgWrite(*this, "colors.ok", g_dm_ok);
  CfgWrite(*this, "colors.dropped", g_dm_dropped);
  CfgWrite(*this, "colors.filtered", g_dm_filtered);
  CfgWrite(*this, "colors.input", g_dm_input);
  CfgWrite(*this, "colors.output", g_dm_output);
  CfgWrite(*this, "colors.not-ok", g_dm_not_ok);

  SaveCanvasConfigs();

  sync();
  SendMessageToAllPlugins("GLOBAL_SETTINGS_UPDATED", "{\"updated\":\"1\"}");

#ifdef ocpnUSE_GL
  if (g_bopengl) {
    if (top_frame::Get()) top_frame::Get()->SendGlJsonConfigMsg();
  }
#endif
}

static QString exportFileName(wxWindow *parent,
                              const wxString suggestedName) {
  wxString path;
  wxString valid_name = SanitizeFileName(suggestedName);

#ifdef __ANDROID__
  if (!valid_name.EndsWith(".gpx")) {
    QFileInfo fi(wxString_to_QString(valid_name));
    valid_name =
        QString_to_wxString(fi.completeBaseName() + QStringLiteral(".gpx"));
  }
#endif
  int response = g_Platform->DoFileSelectorDialog(
      parent, &path, _("Export GPX file"), g_gpx_path, valid_name, "*.gpx");

  if (response == wxID_OK) {
    QFileInfo fi(wxString_to_QString(path));
    g_gpx_path = QString_to_wxString(fi.absolutePath());
    QString full = fi.absoluteFilePath();
    if (fi.suffix().left(3).compare("gpx", Qt::CaseInsensitive) != 0) {
      // Append .gpx if missing
      if (!fi.suffix().isEmpty()) {
        full = fi.absolutePath() + QDir::separator() + fi.completeBaseName() +
               ".gpx";
      } else {
        full += ".gpx";
      }
    }

#if defined(__WXMSW__) || defined(__WXGTK__)
    if (QFile::exists(full)) {
      int answer = OCPNMessageBox(NULL, _("Overwrite existing file?"),
                                  "Confirm", wxICON_QUESTION | wxYES_NO);
      if (answer != wxID_YES) return QString();
    }
#endif
    return full;
  }
  return QString();
}

int BackupDatabase(wxWindow *parent) {
  bool backupResult = false;
  QDateTime tm = QDateTime::currentDateTime();
  wxString proposedName =
      QString_to_wxString(tm.toString("'navobj-'yyyy-MM-dd_HH_mm"));
  wxString acceptedName;

  if (wxID_OK ==
      g_Platform->DoFileSelectorDialog(
          parent, &acceptedName, _("Backup"),
          QString_to_wxString(QStandardPaths::writableLocation(
              QStandardPaths::DocumentsLocation)),
          proposedName, "*.bkp")) {
    QFileInfo fileName(wxString_to_QString(acceptedName));
    if (!fileName.filePath().isEmpty()) {
#if defined(__WXMSW__) || defined(__WXGTK__)
      if (fileName.exists() && fileName.isFile()) {
        if (wxID_YES != OCPNMessageBox(NULL, _("Overwrite existing file?"),
                                       "Confirm", wxICON_QUESTION | wxYES_NO)) {
          return wxID_ABORT;  // We've decided not to overwrite a file, aborting
        }
      }
#endif

#ifdef __ANDROID__
      QString secureFileName = wxString_to_QString(androidGetCacheDir()) +
                               QDir::separator() + fileName.fileName();
      backupResult = NavObj_dB::GetInstance().Backup(secureFileName);
      AndroidSecureCopyFile(QString_to_wxString(secureFileName),
                            QString_to_wxString(fileName.absoluteFilePath()));
#else
      backupResult =
          NavObj_dB::GetInstance().Backup(fileName.absoluteFilePath());
#endif
    }
    return backupResult ? wxID_YES : wxID_NO;
  }
  return wxID_ABORT;  // Cancelled the file open dialog, aborting
}

bool ExportGPXRoutes(wxWindow *parent, RouteList *pRoutes,
                     const wxString suggestedName) {
#ifndef __ANDROID__
  QString fn = exportFileName(parent, suggestedName);
  if (!fn.isEmpty()) {
    NavObjectCollection1 *pgpx = new NavObjectCollection1;
    pgpx->AddGPXRoutesList(pRoutes);
    pgpx->SaveFile(fn);
    delete pgpx;
    return true;
  }
#else
  // Create the .GPX file, saving it in the OCPN Android cache directory
  QString fns = wxString_to_QString(androidGetCacheDir()) + QDir::separator() +
                wxString_to_QString(suggestedName) + ".gpx";
  NavObjectCollection1 *pgpx = new NavObjectCollection1;
  pgpx->AddGPXRoutesList(pRoutes);
  pgpx->SaveFile(fns);
  delete pgpx;

  // Kick off the Android file chooser activity
  wxString path;
  int response = g_Platform->DoFileSelectorDialog(
      parent, &path, _("Export GPX file"), g_gpx_path, suggestedName + ".gpx",
      "*.gpx");

  if (path.IsEmpty())  // relocation handled by SAF logic in Java
    return true;

  QString dest = wxString_to_QString(path);
  QFile::remove(dest);
  QFile::copy(fns, dest);  // known to be safe paths, since SAF is not involved.
  return true;

#endif

  return false;
}

bool ExportGPXTracks(wxWindow *parent, std::vector<Track *> *pTracks,
                     const wxString suggestedName) {
#ifndef __ANDROID__
  QString fn = exportFileName(parent, suggestedName);
  if (!fn.isEmpty()) {
    NavObjectCollection1 *pgpx = new NavObjectCollection1;
    pgpx->AddGPXTracksList(pTracks);
    pgpx->SaveFile(fn);
    delete pgpx;
    return true;
  }
#else
  // Create the .GPX file, saving it in the OCPN Android cache directory
  QString fns = wxString_to_QString(androidGetCacheDir()) + QDir::separator() +
                wxString_to_QString(suggestedName) + ".gpx";
  NavObjectCollection1 *pgpx = new NavObjectCollection1;
  pgpx->AddGPXTracksList(pTracks);
  pgpx->SaveFile(fns);
  delete pgpx;

  // Kick off the Android file chooser activity
  wxString path;
  int response = g_Platform->DoFileSelectorDialog(
      parent, &path, _("Export GPX file"), g_gpx_path, suggestedName + ".gpx",
      "*.gpx");

  if (path.IsEmpty())  // relocation handled by SAF logic in Java
    return true;

  QString dest = wxString_to_QString(path);
  QFile::remove(dest);
  QFile::copy(fns, dest);  // known to be safe paths, since SAF is not involved.
  return true;
#endif

  return false;
}

bool ExportGPXWaypoints(wxWindow *parent, RoutePointList *pRoutePoints,
                        const wxString suggestedName) {
#ifndef __ANDROID__
  QString fn = exportFileName(parent, suggestedName);
  if (!fn.isEmpty()) {
    NavObjectCollection1 *pgpx = new NavObjectCollection1;
    pgpx->AddGPXPointsList(pRoutePoints);
    pgpx->SaveFile(fn);
    delete pgpx;
    return true;
  }
#else
  // Create the .GPX file, saving it in the OCPN Android cache directory
  QString fns = wxString_to_QString(androidGetCacheDir()) + QDir::separator() +
                wxString_to_QString(suggestedName) + ".gpx";
  NavObjectCollection1 *pgpx = new NavObjectCollection1;
  pgpx->AddGPXPointsList(pRoutePoints);
  pgpx->SaveFile(fns);
  delete pgpx;

  // Kick off the Android file chooser activity
  wxString path;
  int response = g_Platform->DoFileSelectorDialog(
      parent, &path, _("Export GPX file"), g_gpx_path, suggestedName + ".gpx",
      "*.gpx");

  if (path.IsEmpty())  // relocation handled by SAF logic in Java
    return true;

  QString dest = wxString_to_QString(path);
  QFile::remove(dest);
  QFile::copy(fns, dest);  // known to be safe paths, since SAF is not involved.
  return true;

#endif

  return false;
}

void ExportGPX(wxWindow *parent, bool bviz_only, bool blayer) {
  NavObjectCollection1 *pgpx = new NavObjectCollection1;
  QString fns;

#ifndef __ANDROID__
  fns = exportFileName(parent, "userobjects.gpx");
  if (fns.isEmpty()) return;
#else
  // Create the .GPX file, saving it in the OCPN Android cache directory
  fns = wxString_to_QString(androidGetCacheDir()) + QDir::separator() +
        "userobjects.gpx";

#endif
  ::wxBeginBusyCursor();

  wxGenericProgressDialog *pprog = nullptr;
  int count = pWayPointMan->GetWaypointList()->size();
  int progStep = count / 32;
  if (count > 200) {
    pprog = new wxGenericProgressDialog(
        _("Export GPX file"), "0/0", count, NULL,
        wxPD_APP_MODAL | wxPD_SMOOTH | wxPD_ELAPSED_TIME | wxPD_ESTIMATED_TIME |
            wxPD_REMAINING_TIME);
    pprog->SetSize(400, wxDefaultCoord);
    pprog->Centre();
  }

  // WPTs
  int ic = 1;

  for (RoutePoint *pr : *pWayPointMan->GetWaypointList()) {
    if (pprog && !(ic % progStep)) {
      wxString msg;
      msg.Printf("%d/%d", ic, count);
      pprog->Update(ic, msg);
    }
    ic++;

    bool b_add = true;
    if (bviz_only && !pr->m_bIsVisible) b_add = false;

    if (pr->m_bIsInLayer && !blayer) b_add = false;
    if (b_add) {
      if (pr->IsShared() || !WptIsInRouteList(pr)) pgpx->AddGPXWaypoint(pr);
    }
  }
  // RTEs and TRKs
  for (Route *pRoute : *pRouteList) {
    bool b_add = true;
    if (bviz_only && !pRoute->IsVisible()) b_add = false;
    if (pRoute->m_bIsInLayer && !blayer) b_add = false;

    if (b_add) pgpx->AddGPXRoute(pRoute);
  }

  for (Track *pTrack : g_TrackList) {
    bool b_add = true;

    if (bviz_only && !pTrack->IsVisible()) b_add = false;

    if (pTrack->m_bIsInLayer && !blayer) b_add = false;

    if (b_add) pgpx->AddGPXTrack(pTrack);
  }

  pgpx->SaveFile(fns);

#ifdef __ANDROID__
  // Kick off the Android file chooser activity
  wxString path;
  int response =
      g_Platform->DoFileSelectorDialog(parent, &path, _("Export GPX file"),
                                       g_gpx_path, "userobjects.gpx", "*.gpx");
  if (path.IsEmpty())  // relocation handled by SAF logic in Java
    return;

  QString dest = wxString_to_QString(path);
  QFile::remove(dest);
  QFile::copy(fns, dest);  // known to be safe paths, since SAF is not involved.
  return;
#endif
  delete pgpx;
  ::wxEndBusyCursor();
  delete pprog;
}

void UI_ImportGPX(wxWindow *parent, bool islayer, wxString dirpath,
                  bool isdirectory, bool isPersistent) {
  int response = wxID_CANCEL;
  wxArrayString file_array;

  if (!islayer || dirpath.IsSameAs("")) {
    //  Platform DoFileSelectorDialog method does not properly handle multiple
    //  selections So use native method if not Android, which means Android gets
    //  single selection only.
#ifndef __ANDROID__
    wxFileDialog *popenDialog =
        new wxFileDialog(NULL, _("Import GPX file"), g_gpx_path, "",
                         "GPX files (*.gpx)|*.gpx|All files (*.*)|*.*",
                         wxFD_OPEN | wxFD_MULTIPLE);

    if (g_bresponsive && parent)
      popenDialog = g_Platform->AdjustFileDialogFont(parent, popenDialog);

    popenDialog->Centre();

#ifdef __WXOSX__
    if (parent) parent->HideWithEffect(wxSHOW_EFFECT_BLEND);
#endif

    response = popenDialog->ShowModal();

#ifdef __WXOSX__
    if (parent) parent->ShowWithEffect(wxSHOW_EFFECT_BLEND);
#endif

    if (response == wxID_OK) {
      popenDialog->GetPaths(file_array);

      //    Record the currently selected directory for later use
      if (file_array.GetCount()) {
        QFileInfo fn(wxString_to_QString(file_array[0]));
        g_gpx_path = QString_to_wxString(fn.absolutePath());
      }
    }
    delete popenDialog;
#else  // Android
    wxString path;
    response = g_Platform->DoFileSelectorDialog(
        NULL, &path, _("Import GPX file"), g_gpx_path, "", "*.gpx");

    QFileInfo fn(wxString_to_QString(path));
    g_gpx_path = QString_to_wxString(fn.absolutePath());
    if (path.IsEmpty()) {  // Return from SAF processing, expecting callback
      PrepareImportAndroid(islayer, isPersistent);
      return;
    } else
      file_array.Add(path);  // Return from safe app arena access

#endif
  } else {
    if (isdirectory) {
      QDirIterator it(wxString_to_QString(dirpath), {"*.gpx"}, QDir::Files,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) {
        file_array.Add(QString_to_wxString(it.next()));
      }
      if (file_array.GetCount()) response = wxID_OK;
    } else {
      file_array.Add(dirpath);
      response = wxID_OK;
    }
  }

  if (response == wxID_OK) {
    ImportFileArray(file_array, islayer, isPersistent, dirpath);
  }
}

void ImportFileArray(const wxArrayString &file_array, bool islayer,
                     bool isPersistent, wxString dirpath) {
  Layer *l = NULL;

  if (islayer) {
    l = new Layer();
    l->m_LayerID = ++g_LayerIdx;
    l->m_LayerFileName = file_array[0];
    if (file_array.GetCount() <= 1)
      l->m_LayerName = QString_to_wxString(
          QFileInfo(wxString_to_QString(file_array[0])).completeBaseName());
    else {
      if (dirpath.IsSameAs(""))
        l->m_LayerName = QString_to_wxString(
            QFileInfo(wxString_to_QString(g_gpx_path)).completeBaseName());
      else
        l->m_LayerName = QString_to_wxString(
            QFileInfo(wxString_to_QString(dirpath)).completeBaseName());
    }

    bool bLayerViz = g_bShowLayers;
    if (g_VisibleLayers.Contains(l->m_LayerName)) bLayerViz = true;
    if (g_InvisibleLayers.Contains(l->m_LayerName)) bLayerViz = false;
    l->m_bIsVisibleOnChart = bLayerViz;

    // Default for new layers is "Names visible"
    l->m_bHasVisibleNames = wxCHK_CHECKED;

    wxString laymsg;
    laymsg.Printf("New layer %d: %s", l->m_LayerID, l->m_LayerName.c_str());
    wxLogMessage(laymsg);

    pLayerList->insert(pLayerList->begin(), l);
  }

  for (unsigned int i = 0; i < file_array.GetCount(); i++) {
    wxString path = file_array[i];

    if (QFile::exists(wxString_to_QString(path))) {
      NavObjectCollection1 *pSet = new NavObjectCollection1;
      pugi::xml_parse_result result = pSet->load_file(path.fn_str());
      if (!result) {
        wxLogMessage("Error loading GPX file " + path);
        wxMessageBox(
            wxString::Format(_("Error loading GPX file %s, %s at character %d"),
                             path, result.description(), result.offset),
            _("Import GPX File"));
        pSet->reset();
        delete pSet;
        continue;
      }

      if (islayer) {
        l->m_NoOfItems = pSet->LoadAllGPXObjectsAsLayer(
            l->m_LayerID, l->m_bIsVisibleOnChart, l->m_bHasVisibleNames);
        l->m_LayerType = isPersistent ? _("Persistent") : _("Temporary");

        if (isPersistent) {
          // If this is a persistent layer also copy the file to config file
          // dir /layers
          wxString destf, f, name, ext;
          f = l->m_LayerFileName;
          QFileInfo fi(wxString_to_QString(f));
          name = QString_to_wxString(fi.completeBaseName());
          ext = QString_to_wxString(fi.suffix());
          destf = g_Platform->GetPrivateDataDir();
          appendOSDirSlash(&destf);
          destf.Append("layers");
          appendOSDirSlash(&destf);
          QString destDir = wxString_to_QString(destf);
          if (!QDir(destDir).exists()) {
            if (!QDir().mkpath(destDir))
              wxLogMessage("Error creating layer directory");
          }

          destf << name << "." << ext;
          wxString msg;
          QString destPath = wxString_to_QString(destf);
          QFile::remove(destPath);
          if (QFile::copy(wxString_to_QString(f), destPath))
            msg.Printf("File: %s.%s also added to persistent layers", name,
                       ext);
          else
            msg.Printf("Failed adding %s.%s to persistent layers", name, ext);
          wxLogMessage(msg);
        }
      } else {
        int wpt_dups;
        pSet->LoadAllGPXObjects(
            !pSet->IsOpenCPN(),
            wpt_dups);  // Import with full visibility of names and objects
#ifndef __ANDROID__
        if (wpt_dups > 0) {
          OCPNMessageBox(
              NULL,
              wxString::Format("%d " + _("duplicate waypoints detected "
                                         "during import and ignored."),
                               wpt_dups),
              _("OpenCPN Info"), wxICON_INFORMATION | wxOK, 10);
        }
#endif
      }
      delete pSet;
    }
  }
}

//-------------------------------------------------------------------------
//           Static Routine Switch to Inland Ecdis Mode
//-------------------------------------------------------------------------
void SwitchInlandEcdisMode(bool Switch) {
  if (Switch) {
    wxLogMessage("Switch InlandEcdis mode On");
    LoadS57();
    // Overrule some settings to comply with InlandEcdis
    // g_toolbarConfig = ".....XXXX.X...XX.XXXXXXXXXXXX";
    g_iDistanceFormat = 2;  // 0 = "Nautical miles"), 1 = "Statute miles", 2 =
                            // "Kilometers", 3 = "Meters"
    g_iSpeedFormat = 2;     // 0 = "kts"), 1 = "mph", 2 = "km/h", 3 = "m/s"
    if (ps52plib) ps52plib->SetDisplayCategory(STANDARD);
    g_bDrawAISSize = false;
    if (top_frame::Get()) top_frame::Get()->RequestNewToolbars(true);
  } else {
    wxLogMessage("Switch InlandEcdis mode Off");
    // reread the settings overruled by inlandEcdis
    if (pConfig) {
      pConfig->endAllGroups();
      pConfig->beginGroup("Settings");
      CfgRead(*pConfig, "GlobalToolbarConfig", &g_toolbarConfig);
      CfgRead(*pConfig, "DistanceFormat", &g_iDistanceFormat);
      CfgRead(*pConfig, "SpeedFormat", &g_iSpeedFormat);
      CfgRead(*pConfig, "ShowDepthUnits", &g_bShowDepthUnits, 1);
      CfgRead(*pConfig, "HeightFormat", &g_iHeightFormat);
      int read_int;
      CfgRead(*pConfig, "nDisplayCategory", &read_int, (enum _DisCat)STANDARD);
      if (ps52plib) ps52plib->SetDisplayCategory((enum _DisCat)read_int);
      pConfig->endAllGroups();
      pConfig->beginGroup("Settings/AIS");
      CfgRead(*pConfig, "bDrawAISSize", &g_bDrawAISSize);
      CfgRead(*pConfig, "bDrawAISRealtime", &g_bDrawAISRealtime);
    }
    if (top_frame::Get()) top_frame::Get()->RequestNewToolbars(true);
  }
}

//-------------------------------------------------------------------------
//
//          Static GPX Support Routines
//
//-------------------------------------------------------------------------
// This function formats the input date/time into a valid GPX ISO 8601
// time string specified in the UTC time zone.

wxString FormatGPXDateTime(QDateTime dt) {
  //      return dt.Format("%Y-%m-%dT%TZ", wxDateTime::GMT0);
  return QString_to_wxString(dt.toString("yyyy-MM-ddTHH:mm:ss'Z'"));
}

/**************************************************************************/
/*          LogMessageOnce                                                */
/**************************************************************************/

bool LogMessageOnce(const wxString &msg) {
  //    Search the array for a match
  QString qmsg = wxString_to_QString(msg);
  if (navutil::pMessageOnceArray->contains(qmsg)) return false;

  // Not found, so add to the array
  navutil::pMessageOnceArray->append(qmsg);

  //    And print it
  wxLogMessage(msg);
  return true;
}

/**************************************************************************/
/*          Some assorted utilities                                       */
/**************************************************************************/

QDateTime toUsrDateTime(const QDateTime ts, const int format,
                        const double lon) {
  if (!ts.isValid()) {
    return ts;
  }
  int effective_format = format;
  if (effective_format == GLOBAL_SETTINGS_INPUT) {
    if (::g_datetime_format == "UTC") {
      effective_format = UTCINPUT;
    } else if (::g_datetime_format == "LMT") {
      effective_format = LMTINPUT;
    } else if (::g_datetime_format == "Local Time") {
      effective_format = LTINPUT;
    } else {
      // Default to UTC
      effective_format = UTCINPUT;
    }
  }
  QDateTime dt;
  switch (effective_format) {
    case LMTINPUT:  // LMT@Location
      if (std::isnan(lon)) {
        dt = QDateTime();
      } else {
        dt = ts.addSecs(static_cast<qint64>(lon * 3600. / 15.));
      }
      break;
    case LTINPUT:  // Local@PC
      // Convert date/time from UTC to local time.
      dt = ts.toLocalTime();
      break;
    case UTCINPUT:  // UTC
      // The date/time is already in UTC.
      dt = ts;
      break;
  }
  return dt;
}

QDateTime fromUsrDateTime(const QDateTime ts, const int format,
                          const double lon) {
  if (!ts.isValid()) {
    return ts;
  }
  int effective_format = format;
  if (effective_format == GLOBAL_SETTINGS_INPUT) {
    if (::g_datetime_format == "UTC") {
      effective_format = UTCINPUT;
    } else if (::g_datetime_format == "LMT") {
      effective_format = LMTINPUT;
    } else if (::g_datetime_format == "Local Time") {
      effective_format = LTINPUT;
    } else {
      // Default to UTC
      effective_format = UTCINPUT;
    }
  }
  QDateTime dt;
  switch (effective_format) {
    case LMTINPUT:  // LMT@Location
      if (std::isnan(lon)) {
        dt = QDateTime();
      } else {
        dt = ts.addSecs(-static_cast<qint64>(lon * 3600. / 15.));
      }
      break;
    case LTINPUT:  // Local@PC
      // The input date/time is in local time, so convert it to UTC.
      dt = ts.toUTC();
      break;
    case UTCINPUT:  // UTC
      dt = ts;
      break;
  }
  return dt;
}

/**************************************************************************/
/*          Converts the speed from the units selected by user to knots   */
/**************************************************************************/
double fromUsrSpeed(double usr_speed, int unit) {
  double ret = NAN;
  if (unit == -1) unit = g_iSpeedFormat;
  switch (unit) {
    case SPEED_KTS:  // kts
      ret = usr_speed;
      break;
    case SPEED_MPH:  // mph
      ret = usr_speed / 1.15078;
      break;
    case SPEED_KMH:  // km/h
      ret = usr_speed / 1.852;
      break;
    case SPEED_MS:  // m/s
      ret = usr_speed / 0.514444444;
      break;
  }
  return ret;
}
/**************************************************************************/
/*     Converts the wind speed from the units selected by user to knots   */
/**************************************************************************/
double fromUsrWindSpeed(double usr_wspeed, int unit) {
  double ret = NAN;
  if (unit == -1) unit = g_iWindSpeedFormat;
  switch (unit) {
    case WSPEED_KTS:  // kts
      ret = usr_wspeed;
      break;
    case WSPEED_MS:  // m/s
      ret = usr_wspeed / 0.514444444;
      break;
    case WSPEED_MPH:  // mph
      ret = usr_wspeed / 1.15078;
      break;
    case WSPEED_KMH:  // km/h
      ret = usr_wspeed / 1.852;
      break;
  }
  return ret;
}

/**************************************************************************/
/*  Converts the temperature from the units selected by user to Celsius   */
/**************************************************************************/
double fromUsrTemp(double usr_temp, int unit) {
  double ret = NAN;
  if (unit == -1) unit = g_iTempFormat;
  switch (unit) {
    case TEMPERATURE_C:  // C
      ret = usr_temp;
      break;
    case TEMPERATURE_F:  // F
      ret = (usr_temp - 32) * 5.0 / 9.0;
      break;
    case TEMPERATURE_K:  // K
      ret = usr_temp - 273.15;
      break;
  }
  return ret;
}

wxString formatAngle(double angle) {
  wxString out;
  if (g_bShowMag && g_bShowTrue) {
    out.Printf("%03.0f %cT (%.0f %cM)", angle, 0x00B0, toMagnetic(angle),
               0x00B0);
  } else if (g_bShowTrue) {
    out.Printf("%03.0f %cT", angle, 0x00B0);
  } else {
    out.Printf("%03.0f %cM", toMagnetic(angle), 0x00B0);
  }
  return out;
}

/* render a rectangle at a given color and transparency */
void AlphaBlending(ocpnDC &dc, int x, int y, int size_x, int size_y,
                   float radius, wxColour color, unsigned char transparency) {
  wxDC *pdc = dc.GetDC();
  if (pdc) {
    //    Get wxImage of area of interest
    wxBitmap obm(size_x, size_y);
    wxMemoryDC mdc1;
    mdc1.SelectObject(obm);
    mdc1.Blit(0, 0, size_x, size_y, pdc, x, y);
    mdc1.SelectObject(wxNullBitmap);
    wxImage oim = obm.ConvertToImage();

    //    Create destination image
    wxBitmap olbm(size_x, size_y);
    wxMemoryDC oldc(olbm);
    if (!oldc.IsOk()) return;

    oldc.SetBackground(*wxBLACK_BRUSH);
    oldc.SetBrush(*wxWHITE_BRUSH);
    oldc.Clear();

    if (radius > 0.0) oldc.DrawRoundedRectangle(0, 0, size_x, size_y, radius);

    wxImage dest = olbm.ConvertToImage();
    unsigned char *dest_data =
        (unsigned char *)malloc(size_x * size_y * 3 * sizeof(unsigned char));
    unsigned char *bg = oim.GetData();
    unsigned char *box = dest.GetData();
    unsigned char *d = dest_data;

    //  Sometimes, on Windows, the destination image is corrupt...
    if (NULL == box) {
      free(d);
      return;
    }
    float alpha = 1.0 - (float)transparency / 255.0;
    int sb = size_x * size_y;
    for (int i = 0; i < sb; i++) {
      float a = alpha;
      if (*box == 0 && radius > 0.0) a = 1.0;
      int r = ((*bg++) * a) + (1.0 - a) * color.Red();
      *d++ = r;
      box++;
      int g = ((*bg++) * a) + (1.0 - a) * color.Green();
      *d++ = g;
      box++;
      int b = ((*bg++) * a) + (1.0 - a) * color.Blue();
      *d++ = b;
      box++;
    }

    dest.SetData(dest_data);

    //    Convert destination to bitmap and draw it
    wxBitmap dbm(dest);
    dc.DrawBitmap(dbm, x, y, false);

    // on MSW, the dc Bounding box is not updated on DrawBitmap() method.
    // Do it explicitely here for all platforms.
    dc.CalcBoundingBox(x, y);
    dc.CalcBoundingBox(x + size_x, y + size_y);
  } else {
#ifdef ocpnUSE_GL
    glEnable(GL_BLEND);

    float radMod = wxMax(radius, 2.0);
    wxColour c(color.Red(), color.Green(), color.Blue(), transparency);
    dc.SetBrush(wxBrush(c));
    dc.SetPen(wxPen(c, 1));
    dc.DrawRoundedRectangle(x, y, size_x, size_y, radMod);

    glDisable(GL_BLEND);

#endif
  }
}

void DimeControl(wxWindow *ctrl) {
#ifdef __WXOSX__
  // On macOS 10.14+, we use the native colours in both light mode and dark
  // mode, and do not need to do anything else. Dark mode is toggled at the
  // application level in `SetAndApplyColorScheme`, and is also respected if it
  // is enabled system-wide.
  if (wxPlatformInfo::Get().CheckOSVersion(10, 14)) {
    return;
  }
#endif
#ifdef __WXQT__
  return;  // this is seriously broken on wxqt
#endif

  if (wxSystemSettings::GetColour(wxSystemColour::wxSYS_COLOUR_WINDOW).Red() <
      128) {
    // Dark system color themes usually do better job than we do on diming UI
    // controls, do not fight with them
    return;
  }

  if (NULL == ctrl) return;

  wxColour col, window_back_color, gridline, uitext, udkrd, ctrl_back_color,
      text_color;
  col = GetGlobalColor("DILG0");                // Dialog Background white
  window_back_color = GetGlobalColor("DILG1");  // Dialog Background
  ctrl_back_color = GetGlobalColor("DILG1");    // Control Background
  text_color = GetGlobalColor("DILG3");         // Text
  uitext = GetGlobalColor("UITX1");             // Menu Text, derived from UINFF
  udkrd = GetGlobalColor("UDKRD");
  gridline = GetGlobalColor("GREY2");

  DimeControl(ctrl, col, window_back_color, ctrl_back_color, text_color, uitext,
              udkrd, gridline);
}

void DimeControl(wxWindow *ctrl, wxColour col, wxColour window_back_color,
                 wxColour ctrl_back_color, wxColour text_color, wxColour uitext,
                 wxColour udkrd, wxColour gridline) {
#ifdef __WXOSX__
  // On macOS 10.14+, we use the native colours in both light mode and dark
  // mode, and do not need to do anything else. Dark mode is toggled at the
  // application level in `SetAndApplyColorScheme`, and is also respected if it
  // is enabled system-wide.
  if (wxPlatformInfo::Get().CheckOSVersion(10, 14)) {
    return;
  }
#endif

  ColorScheme cs = global_color_scheme;

  // Are we in dusk or night mode? (Used below in several places.)
  bool darkMode =
      (cs == GLOBAL_COLOR_SCHEME_DUSK || cs == GLOBAL_COLOR_SCHEME_NIGHT);

  static int depth = 0;  // recursion count
  if (depth == 0) {      // only for the window root, not for every child
    // If the color scheme is DAY or RGB, use the default platform native colour
    // for backgrounds
    if (!darkMode) {
#ifdef _WIN32
      window_back_color = wxNullColour;
#else
      window_back_color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
#endif
      col = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
      uitext = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    }

    ctrl->SetBackgroundColour(window_back_color);
    if (darkMode) ctrl->SetForegroundColour(text_color);
  }

  wxWindowList kids = ctrl->GetChildren();
  for (unsigned int i = 0; i < kids.GetCount(); i++) {
    wxWindowListNode *node = kids.Item(i);
    wxWindow *win = node->GetData();

    if (dynamic_cast<wxListBox *>(win) || dynamic_cast<wxListCtrl *>(win) ||
        dynamic_cast<wxTextCtrl *>(win) ||
        dynamic_cast<wxTimePickerCtrl *>(win)) {
      win->SetBackgroundColour(col);
    } else if (dynamic_cast<wxStaticText *>(win) ||
               dynamic_cast<wxCheckBox *>(win) ||
               dynamic_cast<wxRadioButton *>(win)) {
      win->SetForegroundColour(uitext);
    }
#ifndef __WXOSX__
    // On macOS most controls can't be styled, and trying to do so only creates
    // weird coloured boxes around them. Fortunately, however, many of them
    // inherit a colour or tint from the background of their parent.

    else if (dynamic_cast<wxBitmapComboBox *>(win) ||
             dynamic_cast<wxChoice *>(win) || dynamic_cast<wxComboBox *>(win) ||
             dynamic_cast<wxTreeCtrl *>(win)) {
      win->SetBackgroundColour(col);
    }

    else if (dynamic_cast<wxScrolledWindow *>(win) ||
             dynamic_cast<wxGenericDirCtrl *>(win) ||
             dynamic_cast<wxListbook *>(win) || dynamic_cast<wxButton *>(win) ||
             dynamic_cast<wxToggleButton *>(win)) {
      win->SetBackgroundColour(window_back_color);
    }

    else if (dynamic_cast<wxNotebook *>(win)) {
      win->SetBackgroundColour(window_back_color);
      win->SetForegroundColour(text_color);
    }
#endif

    else if (dynamic_cast<wxHtmlWindow *>(win)) {
      if (cs != GLOBAL_COLOR_SCHEME_DAY && cs != GLOBAL_COLOR_SCHEME_RGB)
        win->SetBackgroundColour(ctrl_back_color);
      else
        win->SetBackgroundColour(wxNullColour);
    }

    else if (dynamic_cast<wxGrid *>(win)) {
      dynamic_cast<wxGrid *>(win)->SetDefaultCellBackgroundColour(
          window_back_color);
      dynamic_cast<wxGrid *>(win)->SetDefaultCellTextColour(uitext);
      dynamic_cast<wxGrid *>(win)->SetLabelBackgroundColour(col);
      dynamic_cast<wxGrid *>(win)->SetLabelTextColour(uitext);
      dynamic_cast<wxGrid *>(win)->SetGridLineColour(gridline);
    }

    if (win->GetChildren().GetCount() > 0) {
      depth++;
      wxWindow *w = win;
      DimeControl(w, col, window_back_color, ctrl_back_color, text_color,
                  uitext, udkrd, gridline);
      depth--;
    }
  }
}
