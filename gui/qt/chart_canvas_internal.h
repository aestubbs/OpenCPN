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
 * Shared include block for the ChartCanvas implementation, which is split
 * across several translation units (chart_canvas*.cpp) to keep each unit small
 * and parallel-compilable. Methods of the single ChartCanvas class are defined
 * across those .cpp files; this header gives each one the common set of Qt and
 * project includes the original monolithic chart_canvas.cpp pulled in.
 *
 * File-local helpers and statics (s_shared_worker, syncAisModelGlobals,
 * kTest*) stay in chart_canvas.cpp -- they are used only there.
 */

#ifndef OCPN_QT_CHART_CANVAS_INTERNAL_H_
#define OCPN_QT_CHART_CANVAS_INTERNAL_H_

#include <QPointer>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#if defined(__GLIBC__)
#include <malloc.h>  // malloc_trim -- return freed cell heap to the OS on evict
#endif

#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QVarLengthArray>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGTransformNode>
#include <QThread>
#include <QFile>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWheelEvent>

#include "ais_layer.h"
#include "chart_boundary_provider.h"
#include "chart_config.h"
#include "chart_layer.h"
#include "chart_worker.h"
#include "config_store.h"
#include "demo_nav_data_provider.h"
#include "pick_highlight_provider.h"
#include "shapefile_basemap_provider.h"
#include "own_ship_config.h"
#include "ais_config.h"
#include "route_defaults_config.h"
#include "own_ship_layer.h"
#include "anchor_watch_layer.h"
#include "ui_config.h"
#include "route_overlay_layers.h"
#include "tide_layer.h"
#include "tcmgr.h"            // ptcmgr (libs/tides) -- tide-station hit-testing
#include "idx_entry.h"
#include "display_config.h"   // showTides gate for the tide pick
#include "layer_compositor.h"
#include "model/ocpn_config.h"
#include "model/georef.h"  // DistanceBearingMercator -- cursor brg/rng
#include "model/track.h"  // g_pActiveTrack -- own-ship track recording
#include "model/routeman.h"  // pWayPointMan -- waypoint icon catalogue
#include "display_config.h"
#include "model_nav_data_provider.h"
#include "nav_state_view_model.h"
#include "ocharts_service.h"
#include "layer.h"
#include "object_query_view_model.h"
#include "raster_chart_provider.h"
#include "route_list_view_model.h"
#include "cm93_scanner.h"
#include "grid_layer.h"
#include "raster_chart_layer.h"
#include "time_controller.h"
#include "waypoint_icons.h"
#include "s57_dictionary.h"
#include "model/ais_decoder.h"   // g_MMSI_Props_Array (MMSI properties, P3.6)
#include "model/ais_defs.h"      // TRACKTYPE_*
#include "model/wx_qt_string.h"  // wxString <-> QString (MmsiProperties)
#include "switchable_nav_provider.h"
#include "s52_engine.h"
#include "s52_vector_chart_provider.h"
#include "test_chart.h"
#include "viewport.h"

#endif  // OCPN_QT_CHART_CANVAS_INTERNAL_H_
