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
 * Implement s52_engine.h.
 *
 * This is the only file in gui/qt/ that includes wxWidgets headers --
 * encapsulates the s52plib library's wx surface behind a Qt-clean façade.
 */

#include "s52_engine.h"

#include <vector>

#include <wx/init.h>
#include <wx/image.h>
#include <wx/string.h>

#include <ogr_geometry.h>

#include "model/wx_qt_string.h"

#include "bbox.h"
#include "chartsymbols.h"  // ChartCtx for SetPLIBColorScheme
#include "mygeom.h"        // PolyTessGeo
#include "s52plib.h"
#include "s52s57.h"        // S57Obj, ObjRazRules, LUPrec

namespace ocpn::qtui {

class S52Engine::Impl {
public:
  s52plib* lib = nullptr;
  QString status = QStringLiteral("S-52: not yet initialised");
  bool wx_initialised = false;

  ~Impl() {
    delete lib;
    if (wx_initialised) wxUninitialize();
  }
};

S52Engine::S52Engine(QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {}

S52Engine::~S52Engine() = default;

bool S52Engine::init(const QString& data_dir) {
  if (m_impl->lib) return m_impl->lib->m_bOK;

  // s52plib expects bare wx services (wxString conversion, wxLog, wxImage
  // PNG handler for the rasterised symbol sheets). wxInitialize is the
  // minimal init; an explicit wxImage PNG handler covers the symbol PNGs
  // without needing a wxApp.
  if (!wxIsMainThread()) {
    // wx insists on main thread for its global init. opencpn-qt's main
    // calls us from main; this branch is a safety check.
    m_impl->status = QStringLiteral("S-52: init must be called from main");
    Q_EMIT changed();
    return false;
  }
  m_impl->wx_initialised = wxInitialize();
  if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) {
    wxImage::AddHandler(new wxPNGHandler());
  }

  const QString rle_path = data_dir + QStringLiteral("/S52RAZDS.RLE");
  m_impl->lib = new s52plib(QString_to_wxString(rle_path));
  if (!m_impl->lib->m_bOK) {
    m_impl->status =
        QStringLiteral("S-52: init failed (could not load %1)").arg(rle_path);
    Q_EMIT changed();
    return false;
  }

  // Configure the presentation library for rendering: a colour scheme
  // (so getColor() resolves S-52 tokens) and the standard display
  // category / boundary+symbol styles. ChartCtx(false, 0) -- not the GL
  // path; the scene-graph emit doesn't touch textures.
  ChartCtx ctx(false, 0);
  m_impl->lib->SetPLIBColorScheme(GLOBAL_COLOR_SCHEME_DAY, ctx);
  m_impl->lib->SetDisplayCategory(STANDARD);
  m_impl->lib->m_nBoundaryStyle = PLAIN_BOUNDARIES;
  m_impl->lib->m_nSymbolStyle = SIMPLIFIED;
  m_impl->lib->UpdateMarinerParams();

  m_impl->status =
      QStringLiteral("S-52: initialised; presentation library v%1.%2 loaded "
                     "from %3")
          .arg(m_impl->lib->GetMajorVersion())
          .arg(m_impl->lib->GetMinorVersion())
          .arg(data_dir);
  Q_EMIT changed();
  return true;
}

namespace {
// Build one synthetic S-57 area feature, look up its symbology, and emit
// its fill geometry into `buf`. `ring` is the exterior boundary as
// (lon, lat) pairs (counter-clockwise, will be closed automatically).
// `attrs` are optional (acronym, double-value) attributes -- e.g. DRVAL1
// / DRVAL2 for a depth area so the DEPARE conditional symbology picks the
// right depth shade.
void EmitArea(s52plib* plib, s52sg::Buffer& buf, const char* feature,
              const std::vector<std::pair<double, double>>& ring,
              double ref_lat, double ref_lon,
              const std::vector<std::pair<const char*, double>>& attrs = {}) {
  auto* obj = new S57Obj(feature);
  for (const auto& a : attrs) obj->AddDoubleAttribute(a.first, a.second);

  OGRPolygon poly;
  OGRLinearRing lr;
  for (const auto& p : ring) lr.addPoint(p.first, p.second);  // (lon, lat)
  // Close the ring explicitly (first point repeated) -- version-agnostic
  // vs OGRLinearRing::closeRings().
  if (!ring.empty()) lr.addPoint(ring.front().first, ring.front().second);
  poly.addRing(&lr);

  auto* ptg = new PolyTessGeo(&poly, true, ref_lat, ref_lon, 0.0);
  if (!ptg->IsOk()) {
    delete ptg;
    delete obj;
    return;
  }
  obj->SetAreaGeometry(ptg, ref_lat, ref_lon);

  LUPrec* lup = plib->S52_LUPLookup(PLAIN_BOUNDARIES, obj->FeatureName, obj);
  if (!lup) {
    delete obj;
    return;
  }
  plib->_LUP2rules(lup, obj);

  ObjRazRules rzRules;
  rzRules.obj = obj;
  rzRules.LUP = lup;
  rzRules.sm_transform_parms = nullptr;  // SG emit applies no projection
  rzRules.child = nullptr;
  rzRules.next = nullptr;
  rzRules.mps = nullptr;

  plib->RenderAreaToSG(buf, &rzRules);
  // obj/ptg intentionally leaked for this proof-of-pipeline; real chart
  // loading owns these in the chart-object set.
}
}  // namespace

s52sg::Buffer S52Engine::buildDemoChart(double north, double south,
                                        double east, double west) {
  s52sg::Buffer buf;
  if (!m_impl->lib || !m_impl->lib->m_bOK) return buf;
  s52plib* plib = m_impl->lib;

  const double ref_lat = (north + south) / 2.0;
  const double ref_lon = (east + west) / 2.0;

  // A deep-water depth area filling the whole extent (DRVAL1/2 push the
  // DEPARE conditional symbology to the deep-water shade).
  EmitArea(plib, buf, "DEPARE",
           {{west, south}, {east, south}, {east, north}, {west, north}},
           ref_lat, ref_lon, {{"DRVAL1", 20.0}, {"DRVAL2", 30.0}});

  // A shallow depth area (a bay) in the south-west quadrant.
  const double midlat = (north + south) / 2.0;
  const double midlon = (east + west) / 2.0;
  EmitArea(plib, buf, "DEPARE",
           {{west, south}, {midlon, south}, {midlon, midlat}, {west, midlat}},
           ref_lat, ref_lon, {{"DRVAL1", 0.0}, {"DRVAL2", 5.0}});

  // A landmass in the north-east quadrant.
  EmitArea(plib, buf, "LNDARE",
           {{midlon, midlat}, {east, midlat}, {east, north}, {midlon, north}},
           ref_lat, ref_lon);

  return buf;
}

bool S52Engine::isOk() const {
  return m_impl->lib && m_impl->lib->m_bOK;
}

QString S52Engine::status() const {
  return m_impl->status;
}

}  // namespace ocpn::qtui
