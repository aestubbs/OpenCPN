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

#include <cstring>
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

// OGR S-57 driver (libs/s57-charts) -- reads a .000 cell into OGR
// features with assembled lon/lat geometry (P2.8d).
#include "ogr_s57.h"
#include "s57class_registrar.h"

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

  // Several s52plib conditional-symbology procedures (e.g. _LITDSN01 for
  // LIGHTS) reach for the global `ps52plib` instance rather than `this`.
  // The legacy app sets it during chart load; do the same so the CS code
  // doesn't dereference null.
  ps52plib = m_impl->lib;

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
// Build an S-57 area object from an OGRPolygon, look up its symbology, and
// emit its fill geometry into `buf`. Shared by the synthetic demo and the
// real-ENC loader. The PolyTessGeo computes its own feature reference
// internally and RenderAreaToSG inverts against that, so the ref_lat/ref_lon
// passed here only seeds the SM origin -- any value is fine.
// A minimal per-cell chart context shared by all objects. s52plib's
// conditional-symbology procedures dereference obj->m_chart_context
// (which S57Obj::Init leaves uninitialised) -- they null-check ->chart
// before the s57chart associated-objects callback, but read
// safety_contour directly, so a zeroed struct with chart=nullptr and a
// sane safety contour is enough to render without the chart-DB host.
chart_context* MakeMinimalChartContext(double ref_lat, double ref_lon) {
  auto* ctx = new chart_context();
  std::memset(ctx, 0, sizeof(chart_context));
  ctx->chart = nullptr;
  ctx->ref_lat = ref_lat;
  ctx->ref_lon = ref_lon;
  ctx->safety_contour = 30.0;  // S-52 default deep safety contour
  ctx->chart_type = 0;
  return ctx;
}

void EmitAreaPoly(s52plib* plib, s52sg::Buffer& buf, const char* feature,
                  OGRPolygon* poly, double ref_lat, double ref_lon,
                  S57Obj* obj, chart_context* ctx) {
  obj->m_chart_context = ctx;
  auto* ptg = new PolyTessGeo(poly, true, ref_lat, ref_lon, 0.0);
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

// Synthetic helper: build an area from a (lon,lat) ring + double attrs.
void EmitArea(s52plib* plib, s52sg::Buffer& buf, const char* feature,
              const std::vector<std::pair<double, double>>& ring,
              double ref_lat, double ref_lon,
              const std::vector<std::pair<const char*, double>>& attrs = {}) {
  auto* obj = new S57Obj(feature);
  for (const auto& a : attrs) obj->AddDoubleAttribute(a.first, a.second);

  OGRPolygon poly;
  OGRLinearRing lr;
  for (const auto& p : ring) lr.addPoint(p.first, p.second);  // (lon, lat)
  if (!ring.empty()) lr.addPoint(ring.front().first, ring.front().second);
  poly.addRing(&lr);

  EmitAreaPoly(plib, buf, feature, &poly, ref_lat, ref_lon, obj,
               MakeMinimalChartContext(ref_lat, ref_lon));
}

// Copy an OGR feature's set fields onto an S57Obj as S-52 attributes, so
// conditional-symbology procedures (DEPARE01 reading DRVAL1/2, etc.) see
// them. The OGR field names are the S-57 attribute acronyms (the driver's
// class registrar maps them). Acronyms are truncated to 6 chars to match
// S57Obj's fixed-width attribute store.
void CopyFeatureAttributes(OGRFeature* feat, S57Obj* obj) {
  OGRFeatureDefn* defn = feat->GetDefnRef();
  const int n = defn->GetFieldCount();
  for (int i = 0; i < n; ++i) {
    if (!feat->IsFieldSet(i)) continue;
    OGRFieldDefn* fd = defn->GetFieldDefn(i);
    char acronym[7];
    std::strncpy(acronym, fd->GetNameRef(), 6);
    acronym[6] = 0;
    switch (fd->GetType()) {
      case OFTInteger:
        obj->AddIntegerAttribute(acronym, feat->GetFieldAsInteger(i));
        break;
      case OFTReal:
        obj->AddDoubleAttribute(acronym, feat->GetFieldAsDouble(i));
        break;
      case OFTString: {
        // AddStringAttribute takes a mutable char*.
        const char* s = feat->GetFieldAsString(i);
        std::vector<char> buf(s, s + std::strlen(s) + 1);
        obj->AddStringAttribute(acronym, buf.data());
        break;
      }
      default:
        break;
    }
  }
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

s52sg::Buffer S52Engine::loadEncCell(const QString& path_000,
                                     const QString& s57data_dir,
                                     double* out_north, double* out_south,
                                     double* out_east, double* out_west) {
  s52sg::Buffer buf;
  if (!m_impl->lib || !m_impl->lib->m_bOK) return buf;
  s52plib* plib = m_impl->lib;

  // The OGR S-57 driver needs an S57ClassRegistrar loaded from the
  // object-class / attribute CSVs. Open()'s lazy init calls
  // LoadInfo(NULL) which bails, so build it explicitly and inject it.
  auto* registrar = new S57ClassRegistrar();
  if (!registrar->LoadInfo(s57data_dir.toUtf8().constData(), FALSE)) {
    delete registrar;
    m_impl->status = QStringLiteral("S-52: could not load S-57 class CSVs from %1")
                         .arg(s57data_dir);
    Q_EMIT changed();
    return buf;
  }

  OGRS57DataSource ds;
  ds.SetS57Registrar(registrar);
  // Assemble feature geometry (not raw primitives); split soundings into
  // points and carry their depth as the 3rd ordinate.
  const char* opts[] = {"RETURN_PRIMITIVES=OFF", "RETURN_LINKAGES=OFF",
                        "LNAM_REFS=OFF", "SPLIT_MULTIPOINT=ON",
                        "ADD_SOUNDG_DEPTH=ON", nullptr};
  ds.SetOptionList(const_cast<char**>(opts));

  int open_rv = ds.Open(path_000.toUtf8().constData(), TRUE);
  if (open_rv) {
    m_impl->status =
        QStringLiteral("S-52: failed to open ENC cell %1 (rv=%2)")
            .arg(path_000)
            .arg(open_rv);
    Q_EMIT changed();
    return buf;
  }

  double n = -90, s = 90, e = -180, w = 180;  // accumulate extent
  int n_areas = 0;
  int n_lines = 0;
  int n_points = 0;

  // NB: OGRS57Layer::GetNextFeature is disabled in this vendored driver
  // (its filter logic is commented out, so it always returns NULL). Read
  // straight from the S57Reader module instead, like the legacy ingest.
  chart_context* ctx = MakeMinimalChartContext(0.0, 0.0);
  S57Reader* reader = ds.GetModule(0);
  if (reader) {
    reader->Rewind();
    OGRFeature* feat;
    while ((feat = reader->ReadNextFeature()) != nullptr) {
      OGRGeometry* geom = feat->GetGeometryRef();
      OGRFeatureDefn* fdefn = feat->GetDefnRef();
      const char* className = fdefn ? fdefn->GetName() : "";
      if (geom && className && className[0]) {
        OGREnvelope env;
        geom->getEnvelope(&env);
        if (env.MaxY > n) n = env.MaxY;
        if (env.MinY < s) s = env.MinY;
        if (env.MaxX > e) e = env.MaxX;
        if (env.MinX < w) w = env.MinX;

        const OGRwkbGeometryType gt = wkbFlatten(geom->getGeometryType());
        if (gt == wkbPolygon || gt == wkbMultiPolygon) {
          auto emitOne = [&](OGRPolygon* poly) {
            OGRLinearRing* ext = poly->getExteriorRing();
            if (!ext || ext->getNumPoints() < 3) return;  // skip degenerate
            auto* obj = new S57Obj(className);
            CopyFeatureAttributes(feat, obj);
            EmitAreaPoly(plib, buf, className, poly, 0.0, 0.0, obj, ctx);
            ++n_areas;
          };
          if (gt == wkbPolygon) {
            emitOne(static_cast<OGRPolygon*>(geom));
          } else {
            auto* mp = static_cast<OGRMultiPolygon*>(geom);
            for (int k = 0; k < mp->getNumGeometries(); ++k)
              emitOne(static_cast<OGRPolygon*>(mp->getGeometryRef(k)));
          }
        } else if (gt == wkbLineString || gt == wkbMultiLineString) {
          // The OGR driver assembles line geometry in lon/lat directly, so
          // no SM round-trip: build a minimal GEO_LINE S57Obj for the LUP
          // lookup, then emit each line string's points with the resolved
          // pen.
          auto emitLine = [&](OGRLineString* ls) {
            const int np = ls->getNumPoints();
            if (np < 2) return;
            auto* obj = new S57Obj(className);
            obj->m_chart_context = ctx;
            obj->Primitive_type = GEO_LINE;
            CopyFeatureAttributes(feat, obj);
            LUPrec* lup = plib->S52_LUPLookup(LINES, obj->FeatureName, obj);
            if (!lup) {
              delete obj;
              return;
            }
            plib->_LUP2rules(lup, obj);
            ObjRazRules rz;
            rz.obj = obj;
            rz.LUP = lup;
            rz.sm_transform_parms = nullptr;
            rz.child = nullptr;
            rz.next = nullptr;
            rz.mps = nullptr;
            QList<QPointF> pts;
            pts.reserve(np);
            for (int pi = 0; pi < np; ++pi)
              pts.append(QPointF(ls->getX(pi), ls->getY(pi)));  // (lon, lat)
            plib->RenderLineToSG(buf, &rz, pts);
            ++n_lines;
          };
          if (gt == wkbLineString) {
            emitLine(static_cast<OGRLineString*>(geom));
          } else {
            auto* ml = static_cast<OGRMultiLineString*>(geom);
            for (int k = 0; k < ml->getNumGeometries(); ++k)
              emitLine(static_cast<OGRLineString*>(ml->getGeometryRef(k)));
          }
        } else if (gt == wkbPoint || gt == wkbMultiPoint) {
          auto emitPoint = [&](OGRPoint* pt) {
            const double lon = pt->getX(), lat = pt->getY();
            if (strncmp(className, "SOUNDG", 6) == 0) {
              // Sounding: the depth rides in the Z ordinate (we opened the
              // cell with ADD_SOUNDG_DEPTH + SPLIT_MULTIPOINT). Format it as
              // a label; metres, one decimal under 10 fathoms-equivalent.
              const double depth = pt->getZ();
              s52sg::Label lab;
              lab.pos = QPointF(lon, lat);
              lab.color = QColor(60, 60, 60);
              lab.pointSize = 9.0f;
              lab.text = depth < 10.0 ? QString::number(depth, 'f', 1)
                                      : QString::number(qRound(depth));
              const int si = feat->GetFieldIndex("SCAMIN");
              if (si >= 0 && feat->IsFieldSet(si))
                lab.scamin = feat->GetFieldAsInteger(si);
              buf.labels.push_back(lab);
              ++n_points;
              return;
            }
            // Other point features: build a GEO_POINT S57Obj, look up its
            // symbology, and emit any TX/TE text labels. (Raster point
            // symbols are added next.)
            auto* obj = new S57Obj(className);
            obj->m_chart_context = ctx;
            obj->Primitive_type = GEO_POINT;
            obj->m_lat = lat;
            obj->m_lon = lon;
            CopyFeatureAttributes(feat, obj);
            LUPrec* lup =
                plib->S52_LUPLookup(PAPER_CHART, obj->FeatureName, obj);
            if (!lup) {
              delete obj;
              return;
            }
            plib->_LUP2rules(lup, obj);
            ObjRazRules rz;
            rz.obj = obj;
            rz.LUP = lup;
            rz.sm_transform_parms = nullptr;
            rz.child = nullptr;
            rz.next = nullptr;
            rz.mps = nullptr;
            // Raster symbol (buoy/beacon/...) + any TX/TE label. Symbol
            // first so CSrules are built once and shared with the text walk.
            plib->RenderPointSymbolToSG(buf, &rz, lon, lat);
            plib->RenderTextToSG(buf, &rz, lon, lat);
            ++n_points;
          };
          if (gt == wkbPoint) {
            emitPoint(static_cast<OGRPoint*>(geom));
          } else {
            auto* mp = static_cast<OGRMultiPoint*>(geom);
            for (int k = 0; k < mp->getNumGeometries(); ++k)
              emitPoint(static_cast<OGRPoint*>(mp->getGeometryRef(k)));
          }
        }
      }
      OGRFeature::DestroyFeature(feat);
    }
  }

  if (out_north) *out_north = n;
  if (out_south) *out_south = s;
  if (out_east) *out_east = e;
  if (out_west) *out_west = w;

  m_impl->status = QStringLiteral(
                       "S-52: loaded ENC %1 -- %2 areas, %3 lines, %4 points, "
                       "extent %5..%6 lat, %7..%8 lon")
                       .arg(QString::fromUtf8(
                           path_000.toUtf8().mid(path_000.lastIndexOf('/') + 1)))
                       .arg(n_areas)
                       .arg(n_lines)
                       .arg(n_points)
                       .arg(s, 0, 'f', 3)
                       .arg(n, 0, 'f', 3)
                       .arg(w, 0, 'f', 3)
                       .arg(e, 0, 'f', 3);
  Q_EMIT changed();
  return buf;
}

bool S52Engine::isOk() const {
  return m_impl->lib && m_impl->lib->m_bOK;
}

QString S52Engine::status() const {
  return m_impl->status;
}

}  // namespace ocpn::qtui
