/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "grib_color_maps.h"

#include <QtGlobal>

namespace ocpn::qtui::gribmaps {

namespace {

struct Stop {
  double val;
  QRgb rgb;
};

// The tables below are byte-for-byte the wx grib_pi maps
// (GribOverlayFactory.cpp:949+). Stop positions are normalized by the
// LAST stop's value, so the unit each column reads in is irrelevant.

constexpr Stop kCurrentMap[] = {
    {0, 0xd90000},  {1, 0xd92a00},  {2, 0xd96e00},  {3, 0xd9b200},
    {4, 0xd4d404},  {5, 0xa6d906},  {7, 0x06d9a0},  {9, 0x00d9b0},
    {12, 0x00d9c0}, {15, 0x00aed0}, {18, 0x0083e0}, {21, 0x0057e0},
    {24, 0x0000f0}, {27, 0x0400f0}, {30, 0x1c00f0}, {36, 0x4800f0},
    {42, 0x6900f0}, {48, 0xa000f0}, {56, 0xf000f0}};

constexpr Stop kGenericMap[] = {
    {0, 0x00d900},  {1, 0x2ad900},  {2, 0x6ed900},  {3, 0xb2d900},
    {4, 0xd4d400},  {5, 0xd9a600},  {7, 0xd90000},  {9, 0xd90040},
    {12, 0xd90060}, {15, 0xae0080}, {18, 0x8300a0}, {21, 0x5700c0},
    {24, 0x0000d0}, {27, 0x0400e0}, {30, 0x0800e0}, {36, 0xa000e0},
    {42, 0xc004c0}, {48, 0xc008a0}, {56, 0xc0a008}};

// zyGrib wind palette.
constexpr Stop kWindMap[] = {
    {0, 0x288CFF},  {3, 0x00AFFF},  {6, 0x00DCE1},  {9, 0x00F7B0},
    {12, 0x00EA9C}, {15, 0x82F059}, {18, 0xF0F503}, {21, 0xFFED00},
    {24, 0xFFDB00}, {27, 0xFFC700}, {30, 0xFFB400}, {33, 0xFF9800},
    {36, 0xFF7E00}, {39, 0xF77800}, {42, 0xEC7814}, {45, 0xE4711E},
    {48, 0xE06128}, {51, 0xDC5132}, {54, 0xD5453C}, {57, 0xCD3A46},
    {60, 0xBE2C50}, {63, 0xB41A5A}, {66, 0xAA1464}, {70, 0x962878},
    {75, 0x8C328C}};

constexpr Stop kAirTempMap[] = {
    {0, 0x283282},  {5, 0x273c8c},  {10, 0x264696}, {14, 0x2350a0},
    {18, 0x1f5aaa}, {22, 0x1a64b4}, {26, 0x136ec8}, {29, 0x0c78e1},
    {32, 0x0382e6}, {35, 0x0091e6}, {38, 0x009ee1}, {41, 0x00a6dc},
    {44, 0x00b2d7}, {47, 0x00bed2}, {50, 0x28c8c8}, {53, 0x78d2aa},
    {56, 0x8cdc78}, {59, 0xa0eb5f}, {62, 0xc8f550}, {65, 0xf3fb02},
    {68, 0xffed00}, {71, 0xffdd00}, {74, 0xffc900}, {78, 0xffab00},
    {82, 0xff8100}, {86, 0xf1780c}, {90, 0xe26a23}, {95, 0xd5453c},
    {100, 0xb53c59}};

// NOAA global SST contour palette.
constexpr Stop kSeaTempMap[] = {
    {-2, 0xcc04ae}, {2, 0x8f06e4},  {6, 0x486afa},  {10, 0x00ffff},
    {15, 0x00d54b}, {19, 0x59d800}, {23, 0xf2fc00}, {27, 0xff1500},
    {32, 0xff0000}, {36, 0xd80000}, {40, 0xa90000}, {44, 0x870000},
    {48, 0x690000}, {52, 0x550000}, {56, 0x330000}};

constexpr Stop kPrecipitationMap[] = {
    {0, 0xffffff},   {.01, 0xc8f0ff}, {.02, 0xb4e6ff}, {.05, 0x8cd3ff},
    {.07, 0x78caff}, {.1, 0x6ec1ff},  {.2, 0x64b8ff},  {.5, 0x50a6ff},
    {.7, 0x469eff},  {1.0, 0x3c96ff}, {2.0, 0x328eff}, {5.0, 0x1e7eff},
    {7.0, 0x1476f0}, {10, 0x0a6edc},  {20, 0x0064c8},  {50, 0x0052aa}};

constexpr Stop kCloudMap[] = {{0, 0xffffff},  {1, 0xf0f0e6},  {10, 0xe6e6dc},
                              {20, 0xdcdcd2}, {30, 0xc8c8b4}, {40, 0xaaaa8c},
                              {50, 0x969678}, {60, 0x787864}, {70, 0x646450},
                              {80, 0x5a5a46}, {90, 0x505036}};

constexpr Stop kRefcMap[] = {{0, 0xffffff},  {5, 0x06E8E4},  {10, 0x009BE9},
                             {15, 0x0400F3}, {20, 0x00F924}, {25, 0x06C200},
                             {30, 0x009100}, {35, 0xFAFB00}, {40, 0xEBB608},
                             {45, 0xFF9400}, {50, 0xFD0002}, {55, 0xD70000},
                             {60, 0xC20300}, {65, 0xF900FE}, {70, 0x945AC8}};

constexpr Stop kCapeMap[] = {
    {0, 0x0046c8},    {5, 0x0050f0},    {10, 0x005aff},   {15, 0x0069ff},
    {20, 0x0078ff},   {30, 0x000cff},   {45, 0x00a1ff},   {60, 0x00b6fa},
    {100, 0x00c9ee},  {150, 0x00e0da},  {200, 0x00e6b4},  {300, 0x82e678},
    {500, 0x9bff3b},  {700, 0xffdc00},  {1000, 0xffb700}, {1500, 0xf37800},
    {2000, 0xd4440c}, {2500, 0xc8201c}, {3000, 0xad0430}};

constexpr Stop kWindyMap[] = {
    {0, 0x6271B7},  {3, 0x3961A9},  {6, 0x4A94A9},  {9, 0x4D8D7B},
    {12, 0x53A553}, {15, 0x53A553}, {18, 0x359F35}, {21, 0xA79D51},
    {24, 0x9F7F3A}, {27, 0xA16C5C}, {30, 0xA16C5C}, {33, 0x813A4E},
    {36, 0xAF5088}, {39, 0xAF5088}, {42, 0x754A93}, {45, 0x754A93},
    {48, 0x6D61A3}, {51, 0x44698D}, {54, 0x44698D}, {57, 0x5C9098},
    {60, 0x7D44A5}, {63, 0x7D44A5}, {66, 0x7D44A5}, {69, 0xE7D7D7},
    {72, 0xE7D7D7}, {75, 0xE7D7D7}, {78, 0xDBD483}, {81, 0xDBD483},
    {84, 0xDBD483}, {87, 0xCDC470}, {90, 0xCDC470}, {93, 0xCDC470},
    {96, 0xCDC470}, {99, 0x808080}};

void tableFor(Map m, const Stop** map, int* len) {
  switch (m) {
    case Current:
      *map = kCurrentMap;
      *len = sizeof kCurrentMap / sizeof *kCurrentMap;
      break;
    case Wind:
      *map = kWindMap;
      *len = sizeof kWindMap / sizeof *kWindMap;
      break;
    case AirTemp:
      *map = kAirTempMap;
      *len = sizeof kAirTempMap / sizeof *kAirTempMap;
      break;
    case SeaTemp:
      *map = kSeaTempMap;
      *len = sizeof kSeaTempMap / sizeof *kSeaTempMap;
      break;
    case Precipitation:
      *map = kPrecipitationMap;
      *len = sizeof kPrecipitationMap / sizeof *kPrecipitationMap;
      break;
    case Cloud:
      *map = kCloudMap;
      *len = sizeof kCloudMap / sizeof *kCloudMap;
      break;
    case Cape:
      *map = kCapeMap;
      *len = sizeof kCapeMap / sizeof *kCapeMap;
      break;
    case Refc:
      *map = kRefcMap;
      *len = sizeof kRefcMap / sizeof *kRefcMap;
      break;
    case Windy:
      *map = kWindyMap;
      *len = sizeof kWindyMap / sizeof *kWindyMap;
      break;
    case Generic:
    default:
      *map = kGenericMap;
      *len = sizeof kGenericMap / sizeof *kGenericMap;
      break;
  }
}

}  // namespace

QRgb color(Map m, double fraction) {
  const Stop* map = nullptr;
  int len = 0;
  tableFor(m, &map, &len);
  // wx GetGraphicColor, gradual-colours branch. The clamp is the one
  // deviation: wx lets out-of-range fractions extrapolate past the table
  // ends and wrap the uchar channels; clamping pins them to the end
  // colours instead.
  fraction = qBound(0.0, fraction, 1.0);
  const double cmax = map[len - 1].val;
  for (int i = 1; i < len; i++) {
    const double a = map[i - 1].val / cmax;
    const double b = map[i].val / cmax;
    if (b > fraction || i == len - 1) {
      const double d = qBound(0.0, (fraction - a) / (b - a), 1.0);
      const QRgb ca = map[i - 1].rgb, cb = map[i].rgb;
      const int r = (1 - d) * qRed(0xff000000 | ca) + d * qRed(0xff000000 | cb);
      const int g =
          (1 - d) * qGreen(0xff000000 | ca) + d * qGreen(0xff000000 | cb);
      const int bl =
          (1 - d) * qBlue(0xff000000 | ca) + d * qBlue(0xff000000 | cb);
      return qRgb(r, g, bl);
    }
  }
  return qRgb(0, 0, 0);  // unreachable
}

bool rampForKey(const QString& key, Map* map, double* minSI, double* maxSI) {
  // wx defaults: defcolor[] in GribSettingsDialog.cpp:233 picks the map
  // per type; GetMin/GetMax give the range (here in the records' SI
  // units -- the normalized fraction is conversion-invariant). One
  // deliberate divergence: wx zero-fills reflectivity's default to the
  // generic map even though it ships the dedicated REFC radar palette;
  // we use the REFC palette.
  struct Spec {
    const char* key;
    Map map;
    double mn, mx;
  };
  static constexpr Spec kSpecs[] = {
      {"wind", Wind, 0, 40},                    // m/s
      {"gust", Wind, 0, 40},                    // m/s
      {"pressure", Generic, 84000, 112000},     // Pa
      {"waves", Generic, 0, 30},                // m
      {"current", Current, 0, 12},              // m/s
      {"rain", Precipitation, 0, 80},           // mm
      {"cloud", Cloud, 0, 100},                 // %
      {"airtemp", AirTemp, 233.15, 323.15},     // K
      {"seatemp", SeaTemp, 271.15, 329.15},     // K
      {"cape", Cape, 0, 3500},                  // J/kg
      {"refl", Refc, 0, 80},                    // dBZ
      {"humid", Generic, 0, 100},               // %
  };
  for (const Spec& s : kSpecs) {
    if (key == QLatin1String(s.key)) {
      *map = s.map;
      *minSI = s.mn;
      *maxSI = s.mx;
      return true;
    }
  }
  return false;
}

}  // namespace ocpn::qtui::gribmaps
