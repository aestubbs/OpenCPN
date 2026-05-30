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

#include <algorithm>
#include <cstring>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMultiHash>
#include <QPointF>
#include <QPolygonF>

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
#include "s52utils.h"      // S52_setMarinerParam, S52_MAR_* (depth shading)

// OGR S-57 driver (libs/s57-charts) -- reads a .000 cell into OGR
// features with assembled lon/lat geometry (P2.8d).
#include "ogr_s57.h"
#include "s57class_registrar.h"
#include "s57registrar_mgr.h"  // typecode -> acronym for OSENC decode

#include "osenc_reader.h"  // OSENC record-type enums

namespace ocpn::qtui {

class S52Engine::Impl {
public:
  s52plib* lib = nullptr;
  QString status = QStringLiteral("S-52: not yet initialised");
  bool wx_initialised = false;
  // The S-57 class registrar is expensive to build (parses the object-class
  // / attribute CSVs) and immutable once loaded, so it's cached here and
  // shared across every cell load/scan rather than rebuilt per cell.
  S57ClassRegistrar* registrar = nullptr;
  // Maps OSENC numeric feature/attribute type codes back to S-57 acronyms
  // (the OGR path gets acronyms from field names instead). Built lazily on
  // first OSENC decode from the s57data CSVs.
  s57RegistrarMgr* regmgr = nullptr;
  QString s57data_dir;  // for the registrar manager CSVs

  ~Impl() {
    delete lib;
    delete registrar;
    delete regmgr;
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

  m_impl->s57data_dir = data_dir;  // registrar-manager CSVs live here too
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

// Reconstruct a PolyTessGeo from an OSENC FEATURE_GEOMETRY_RECORD_AREA payload
// (ported from Osenc::BuildPolyTessGeo). `p` points at the payload start
// (after the 6-byte record base); `plen` is its length. The payload is:
// 4 doubles extent {s_lat,n_lat,w_lon,e_lon}, 3 uint32 {contour, triprim,
// edge counts}, then the per-contour point-count array (contour×int) and the
// triangle primitives, then the edge index table (ignored here). Returns
// nullptr on a malformed/too-short payload.
PolyTessGeo* buildOsencPolyTessGeo(const char* p, uint32_t plen,
                                   double ref_lat, double ref_lon,
                                   const char** boundary_edges = nullptr) {
  if (boundary_edges) *boundary_edges = nullptr;
  constexpr uint32_t kFixed = 4 * sizeof(double) + 3 * sizeof(uint32_t);  // 44
  if (plen < kFixed) return nullptr;
  double ext[4];
  std::memcpy(ext, p, 4 * sizeof(double));  // s_lat, n_lat, w_lon, e_lon
  uint32_t nContours, nTriPrim, nEdge;
  std::memcpy(&nContours, p + 32, 4);
  std::memcpy(&nTriPrim, p + 36, 4);
  std::memcpy(&nEdge, p + 40, 4);
  (void)nEdge;
  const char* const end = p + plen;
  const char* run = p + kFixed;

  auto* pPTG = new PolyTessGeo();
  pPTG->SetExtents(ext[2], ext[0], ext[3], ext[1]);  // w, s, e, n
  // The TriPrim vertices are SM metres relative to the CELL reference (the
  // OSENC writer used the cell-extent centroid as m_ref_lat/lon). The SG emit
  // (RenderToSGAC) inverts SM->lon/lat via GetChartRefPos(), so the ptg MUST
  // carry that ref -- the default ctor leaves it 0,0 and the fill lands off
  // the coast of Africa. (NOAA's PolyTessGeo(poly,ref...) ctor sets it; this
  // hand-built ptg must set it explicitly.)
  pPTG->m_ref_lat = ref_lat;
  pPTG->m_ref_lon = ref_lon;
  auto* ppg = new PolyTriGroup;
  ppg->m_bSMSENC = true;
  ppg->data_type = DATA_TYPE_DOUBLE;
  ppg->nContours = static_cast<int>(nContours);
  ppg->pn_vertex = static_cast<int*>(malloc(nContours * sizeof(int)));
  // Per-contour point-count array.
  if (run + nContours * sizeof(int) > end) { delete ppg; delete pPTG; return nullptr; }
  std::memcpy(ppg->pn_vertex, run, nContours * sizeof(int));
  run += nContours * sizeof(int);
  ppg->pgroup_geom = nullptr;

  TriPrim** p_prev = &(ppg->tri_prim_head);
  int nvert_max = 0;
  size_t total_bytes = 2 * sizeof(float);
  bool bad = false;
  for (uint32_t i = 0; i < nTriPrim && !bad; ++i) {
    if (run + 1 + sizeof(uint32_t) + 4 * sizeof(double) > end) { bad = true; break; }
    const uint8_t tri_type = static_cast<uint8_t>(*run++);
    uint32_t nvert;
    std::memcpy(&nvert, run, sizeof(uint32_t));
    run += sizeof(uint32_t);
    auto* tp = new TriPrim;
    *p_prev = tp;
    p_prev = &(tp->p_next);
    tp->p_next = nullptr;
    tp->type = tri_type;
    tp->nVert = nvert;
    nvert_max = std::max<int>(nvert_max, static_cast<int>(nvert));
    double bb[4];
    std::memcpy(bb, run, 4 * sizeof(double));  // minx, maxx, miny, maxy
    run += 4 * sizeof(double);
    tp->tri_box.Set(bb[2], bb[0], bb[3], bb[1]);
    const size_t vbytes = static_cast<size_t>(nvert) * 2 * sizeof(float);
    if (run + vbytes > end) { bad = true; tp->nVert = 0; break; }
    tp->p_vertex = reinterpret_cast<double*>(const_cast<char*>(run));
    run += vbytes;
    total_bytes += vbytes;
  }
  // Coalesce vertices into one owned float buffer (the source payload is
  // transient), repointing each TriPrim.
  auto* vbuf = static_cast<unsigned char*>(malloc(total_bytes));
  unsigned char* vrun = vbuf;
  for (TriPrim* tp = ppg->tri_prim_head; tp; tp = tp->p_next) {
    const size_t vb = static_cast<size_t>(tp->nVert) * 2 * sizeof(float);
    std::memcpy(vrun, tp->p_vertex, vb);
    tp->p_vertex = reinterpret_cast<double*>(vrun);
    vrun += vb;
  }
  ppg->bsingle_alloc = true;
  ppg->single_buffer = vbuf;
  ppg->single_buffer_size = total_bytes;
  ppg->data_type = DATA_TYPE_FLOAT;

  pPTG->SetPPGHead(ppg);
  pPTG->SetnVertexMax(nvert_max);
  pPTG->Set_OK(true);
  // `run` now points just past the triangle vertices -- i.e. at the boundary
  // edge-index table (nEdge triples of [startVC, ±edgeVE, endVC]), which the
  // caller resolves into the area outline / coastline. Only valid if every
  // triprim parsed cleanly (otherwise the offset is meaningless).
  if (boundary_edges && !bad) *boundary_edges = run;
  return pPTG;
}

// Edge-index tables (line features + area boundaries) are arrays of edge
// entries; we only need the first 3 ints of each: [startVC, ±edgeVE, endVC].
// CRITICAL: o-charts OESU stores **4 ints per entry** (a trailing reserved/0
// field), whereas the open-source OSENC in gui/src/o_senc.cpp stores 3. Reading
// 4-int data at stride 3 drifts every entry -> edges paired with the wrong
// nodes (the chart-wide "spider web") and unresolved entries (missing lines).
// Detect the stride from the record length and repack into a canonical stride-3
// table (caller owns the malloc'd result). `avail_bytes` is the table size.
int* repackEdgeIndex(const char* tbl, uint32_t ec, qint64 avail_bytes,
                     int* count_out) {
  *count_out = 0;
  if (ec == 0 || !tbl) return nullptr;
  const qint64 per = avail_bytes / (static_cast<qint64>(ec) * sizeof(int));
  if (per < 3) return nullptr;  // malformed / too short
  const int* src = reinterpret_cast<const int*>(tbl);
  int* dst = static_cast<int*>(malloc(static_cast<size_t>(ec) * 3 * sizeof(int)));
  for (uint32_t i = 0; i < ec; ++i) {
    dst[i * 3 + 0] = src[i * per + 0];  // start connected-node (VC) index
    dst[i * 3 + 2] = src[i * per + 2];  // end connected-node (VC) index
    // Edge index + traversal direction. Open OSENC (stride 3) carries the
    // direction in the SIGN of the edge field. OESU (stride 4) carries an
    // UNSIGNED edge index plus a separate 4th "direction" int (0 = forward,
    // non-zero = reverse) -- the topology field we previously discarded, which
    // left every reverse edge drawn forward (the "X" connector artifacts).
    // Normalise both to the canonical signed form [in, ±edgeVE, en].
    const int edge = src[i * per + 1];
    if (per >= 4) {
      const int dir = src[i * per + 3];
      dst[i * 3 + 1] = dir ? -std::abs(edge) : std::abs(edge);
    } else {
      dst[i * 3 + 1] = edge;
    }
  }
  *count_out = static_cast<int>(ec);
  return dst;
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
  // P2.15 (OGR/.000 half): area BOUNDARY lines. RenderAreaToSG emits only the
  // AC/AP fill; the area's S-52 boundary line rules (RUL_SIM_LN / RUL_COM_LN --
  // depth-area edges, DRGARE/RESARE borders, ...) are not. The OGR path has no
  // m_lsindex edge list (unlike OSENC), so take the boundary from the
  // OGRPolygon rings (exterior + holes; geographic lon/lat, as the LNDARE
  // coast-shade capture does) and feed each to RenderLineToSG with the area's
  // rzRules -- it dispatches the boundary rules as for a line feature
  // (priority-sorted, so the border draws over the fill). Mirrors wx's second
  // RenderObjectToGL pass over area objects.
  auto emitRing = [&](const OGRLinearRing* r) {
    if (!r) return;
    const int np = r->getNumPoints();
    if (np < 2) return;
    QList<QPointF> pts;
    pts.reserve(np);
    for (int i = 0; i < np; ++i)
      pts.append(QPointF(r->getX(i), r->getY(i)));  // (lon, lat)
    plib->RenderLineToSG(buf, &rzRules, pts);
  };
  emitRing(poly->getExteriorRing());
  for (int k = 0; k < poly->getNumInteriorRings(); ++k)
    emitRing(poly->getInteriorRing(k));
  // Area centroid SY symbols + TX/TE text (area names, restricted-area markers,
  // etc.) via the LUP/CS, anchored at the area base point -- parity with the
  // OSENC path and with wx (which walks every rule type per object).
  plib->RenderPointSymbolToSG(buf, &rzRules, obj->m_lon, obj->m_lat);
  plib->RenderTextToSG(buf, &rzRules, obj->m_lon, obj->m_lat);
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

namespace {
// Accumulators shared across cells when merging a multi-cell surface.
struct LoadExtent {
  double n = -90, s = 90, e = -180, w = 180;
  void grow(const OGREnvelope& env) {
    if (env.MaxY > n) n = env.MaxY;
    if (env.MinY < s) s = env.MinY;
    if (env.MaxX > e) e = env.MaxX;
    if (env.MinX < w) w = env.MinX;
  }
};
struct LoadCounts {
  int areas = 0, lines = 0, points = 0;
};

// Open one ENC cell via the OGR S-57 driver and append its features to
// `buf` (decoded through `plib`). Shares the registrar across cells.
// Returns false (and sets `err`) if the cell can't be opened.
bool loadOneCell(s52plib* plib, s52sg::Buffer& buf, const QString& path_000,
                 S57ClassRegistrar* registrar, LoadExtent& ext,
                 LoadCounts& cc, QString& err) {
  OGRS57DataSource ds;
  ds.SetS57Registrar(registrar);
  // Assemble feature geometry (not raw primitives); split soundings into
  // points and carry their depth as the 3rd ordinate.
  const char* opts[] = {"RETURN_PRIMITIVES=OFF", "RETURN_LINKAGES=OFF",
                        "LNAM_REFS=OFF", "SPLIT_MULTIPOINT=ON",
                        "ADD_SOUNDG_DEPTH=ON", nullptr};
  ds.SetOptionList(const_cast<char**>(opts));

  if (ds.Open(path_000.toUtf8().constData(), TRUE)) {
    err = QStringLiteral("failed to open %1").arg(path_000);
    return false;
  }

  double& n = ext.n;
  double& s = ext.s;
  double& e = ext.e;
  double& w = ext.w;
  int& n_areas = cc.areas;
  int& n_lines = cc.lines;
  int& n_points = cc.points;

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

        // Object-query snapshot: class + set attributes + bbox + a
        // representative shape (first part) for the click hit-test.
        {
          s52sg::QueryObject qo;
          qo.className = QString::fromUtf8(className);
          qo.minLon = env.MinX;
          qo.minLat = env.MinY;
          qo.maxLon = env.MaxX;
          qo.maxLat = env.MaxY;
          if (OGRFeatureDefn* fd = feat->GetDefnRef()) {
            const int nf = fd->GetFieldCount();
            for (int fi = 0; fi < nf; ++fi) {
              if (!feat->IsFieldSet(fi)) continue;
              const char* an = fd->GetFieldDefn(fi)->GetNameRef();
              const char* av = feat->GetFieldAsString(fi);
              if (an && av && av[0])
                qo.attrs.append(
                    {QString::fromUtf8(an), QString::fromUtf8(av).trimmed()});
            }
          }
          if (gt == wkbPolygon || gt == wkbMultiPolygon) {
            qo.geom = s52sg::QueryGeom::Area;
            OGRPolygon* poly =
                gt == wkbPolygon
                    ? static_cast<OGRPolygon*>(geom)
                    : static_cast<OGRPolygon*>(
                          static_cast<OGRMultiPolygon*>(geom)->getGeometryRef(
                              0));
            OGRLinearRing* r = poly ? poly->getExteriorRing() : nullptr;
            if (r)
              for (int i = 0; i < r->getNumPoints(); ++i)
                qo.shape.append(QPointF(r->getX(i), r->getY(i)));
          } else if (gt == wkbLineString || gt == wkbMultiLineString) {
            qo.geom = s52sg::QueryGeom::Line;
            OGRLineString* ls =
                gt == wkbLineString
                    ? static_cast<OGRLineString*>(geom)
                    : static_cast<OGRLineString*>(
                          static_cast<OGRMultiLineString*>(geom)
                              ->getGeometryRef(0));
            if (ls)
              for (int i = 0; i < ls->getNumPoints(); ++i)
                qo.shape.append(QPointF(ls->getX(i), ls->getY(i)));
          } else if (gt == wkbPoint || gt == wkbMultiPoint) {
            qo.geom = s52sg::QueryGeom::Point;
            OGRPoint* p =
                gt == wkbPoint ? static_cast<OGRPoint*>(geom)
                               : static_cast<OGRPoint*>(
                                     static_cast<OGRMultiPoint*>(geom)
                                         ->getGeometryRef(0));
            if (p) qo.shape.append(QPointF(p->getX(), p->getY()));
          }
          buf.queryObjects.append(std::move(qo));
        }

        if (gt == wkbPolygon || gt == wkbMultiPolygon) {
          auto emitOne = [&](OGRPolygon* poly) {
            OGRLinearRing* ext = poly->getExteriorRing();
            if (!ext || ext->getNumPoints() < 3) return;  // skip degenerate
            auto* obj = new S57Obj(className);
            CopyFeatureAttributes(feat, obj);
            EmitAreaPoly(plib, buf, className, poly, 0.0, 0.0, obj, ctx);
            ++n_areas;
            // Capture land-area exterior rings (lon, lat) for the coastline
            // land-shade pass.
            if (strncmp(className, "LNDARE", 6) == 0) {
              const int np = ext->getNumPoints();
              QList<QPointF> ring;
              ring.reserve(np);
              for (int i = 0; i < np; ++i)
                ring.append(QPointF(ext->getX(i), ext->getY(i)));
              buf.landContours.append(std::move(ring));
            }
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
            // Line SY symbols + TX/TE text (names along cables/pipes, etc.) via
            // the LUP/CS, anchored at the line midpoint -- parity with the
            // OSENC path. The OGR obj has no base point set, so use the mid
            // vertex.
            const QPointF anchor = pts.at(np / 2);
            plib->RenderPointSymbolToSG(buf, &rz, anchor.x(), anchor.y());
            plib->RenderTextToSG(buf, &rz, anchor.x(), anchor.y());
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
              lab.isSounding = true;
              lab.depth = static_cast<float>(depth);
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
  return true;
}

// Build the registrar the OGR S-57 driver needs (loaded from the
// object-class / attribute CSVs). Open()'s lazy init calls LoadInfo(NULL)
// which bails, so it must be created explicitly and injected. Returns
// nullptr on failure.
S57ClassRegistrar* makeRegistrar(const QString& s57data_dir) {
  auto* registrar = new S57ClassRegistrar();
  if (!registrar->LoadInfo(s57data_dir.toUtf8().constData(), FALSE)) {
    delete registrar;
    return nullptr;
  }
  return registrar;
}
}  // namespace

s52sg::Buffer S52Engine::loadEncCell(const QString& path_000,
                                     const QString& s57data_dir,
                                     double* out_north, double* out_south,
                                     double* out_east, double* out_west) {
  return loadEncCells({path_000}, s57data_dir, out_north, out_south, out_east,
                      out_west);
}

s52sg::Buffer S52Engine::loadEncCells(const QStringList& paths_000,
                                      const QString& s57data_dir,
                                      double* out_north, double* out_south,
                                      double* out_east, double* out_west) {
  s52sg::Buffer buf;
  if (!m_impl->lib || !m_impl->lib->m_bOK) return buf;
  s52plib* plib = m_impl->lib;

  if (!m_impl->registrar) m_impl->registrar = makeRegistrar(s57data_dir);
  S57ClassRegistrar* registrar = m_impl->registrar;
  if (!registrar) {
    m_impl->status =
        QStringLiteral("S-52: could not load S-57 class CSVs from %1")
            .arg(s57data_dir);
    Q_EMIT changed();
    return buf;
  }

  LoadExtent ext;
  LoadCounts cc;
  int n_cells = 0;
  QString err;
  for (const QString& path : paths_000) {
    QString cellErr;
    if (loadOneCell(plib, buf, path, registrar, ext, cc, cellErr))
      ++n_cells;
    else if (err.isEmpty())
      err = cellErr;
  }
  // S-52 display-priority order (see decodeOsenc): area fills under line/area
  // symbols, stable within a priority.
  std::stable_sort(buf.prims.begin(), buf.prims.end(),
                   [](const s52sg::Prim& a, const s52sg::Prim& b) {
                     return a.priority < b.priority;
                   });
  qWarning(
      "loadEncCells: %d/%lld cells -- %d areas, %d lines, %d points; "
      "%lld pattern fills, %lld raster symbols, %lld vector symbols (pre-sort)",
      n_cells, (long long)paths_000.size(), cc.areas, cc.lines, cc.points,
      (long long)buf.patternFills.size(), (long long)buf.symbols.size(),
      (long long)buf.vectorSymbols.size());

  if (out_north) *out_north = ext.n;
  if (out_south) *out_south = ext.s;
  if (out_east) *out_east = ext.e;
  if (out_west) *out_west = ext.w;

  m_impl->status =
      QStringLiteral(
          "S-52: %1 cell(s) -- %2 areas, %3 lines, %4 points, extent "
          "%5..%6 lat, %7..%8 lon%9")
          .arg(n_cells)
          .arg(cc.areas)
          .arg(cc.lines)
          .arg(cc.points)
          .arg(ext.s, 0, 'f', 3)
          .arg(ext.n, 0, 'f', 3)
          .arg(ext.w, 0, 'f', 3)
          .arg(ext.e, 0, 'f', 3)
          .arg(err.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(err));
  Q_EMIT changed();
  return buf;
}

s52sg::Buffer S52Engine::loadOsencCell(const QString& path, double* on,
                                       double* os, double* oe, double* ow) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return s52sg::Buffer{};
  return decodeOsenc(f.readAll(), on, os, oe, ow);
}

s52sg::Buffer S52Engine::decodeOsenc(const QByteArray& bytes, double* on,
                                     double* os, double* oe, double* ow) {
  s52sg::Buffer buf;
  if (!m_impl->lib || !m_impl->lib->m_bOK) return buf;
  s52plib* plib = m_impl->lib;
  ps52plib = plib;  // CS procedures reach for the global

  // Registrar manager (typecode -> acronym): built once from the s57data CSVs.
  if (!m_impl->regmgr && !m_impl->s57data_dir.isEmpty())
    m_impl->regmgr =
        new s57RegistrarMgr(QString_to_wxString(m_impl->s57data_dir), nullptr);
  s57RegistrarMgr* reg = m_impl->regmgr;
  if (!reg) return buf;

  const char* p = bytes.constData();
  const qint64 N = bytes.size();
  qint64 off = 0;

  double ref_lat = 0, ref_lon = 0;
  double nlat = 0, slat = 0, elon = 0, wlon = 0;
  bool have_extent = false;
  chart_context* ctx = nullptr;
  S57Obj* cur = nullptr;
  std::vector<S57Obj*> objects;
  // Edge tables, as stored in the OSENC: VE index -> interleaved SM-metre
  // (easting,northing) floats; VC index -> SM-metre point. Converted to
  // (lon,lat) after the read loop (see below) before line geometry is emitted.
  QHash<int, QVector<float>> ve;
  QHash<int, QPointF> vc;

  while (off + 6 <= N) {
    uint16_t type;
    uint32_t length;
    std::memcpy(&type, p + off, 2);
    std::memcpy(&length, p + off + 2, 4);
    if (length < 6 || off + static_cast<qint64>(length) > N) break;
    const char* pl = p + off + 6;
    const uint32_t plen = length - 6;
    off += length;

    switch (type) {
      case CELL_EXTENT_RECORD: {
        if (plen >= 8 * sizeof(double)) {
          double d[8];
          std::memcpy(d, pl, 8 * sizeof(double));
          // 8 doubles = the 4 corners (sw,nw,ne,se) as lat/lon pairs. Match the
          // wx Osenc reader exactly: NLAT=nw_lat[2] SLAT=se_lat[6]
          // WLON=nw_lon[3] ELON=se_lon[7]. (Was d[5]=ne_lon; equal to se_lon
          // only for axis-aligned cells, but d[7] is the canonical field.)
          nlat = d[2]; slat = d[6]; wlon = d[3]; elon = d[7];
          have_extent = true;
          ref_lat = (nlat + slat) / 2.0;
          ref_lon = (elon + wlon) / 2.0;
          if (!ctx) ctx = MakeMinimalChartContext(ref_lat, ref_lon);
        }
        break;
      }
      case FEATURE_ID_RECORD: {
        cur = nullptr;
        if (plen >= 5) {
          uint16_t ftc;
          std::memcpy(&ftc, pl, 2);
          const std::string acr = reg->getFeatureAcronym(ftc);
          if (!acr.empty()) {
            if (!ctx) ctx = MakeMinimalChartContext(ref_lat, ref_lon);
            cur = new S57Obj(acr.c_str());
            cur->m_chart_context = ctx;
            cur->Primitive_type = GEO_META;  // sentinel until geometry seen
            objects.push_back(cur);
          }
        }
        break;
      }
      case FEATURE_ATTRIBUTE_RECORD: {
        if (cur && plen >= 3) {
          uint16_t atc;
          std::memcpy(&atc, pl, 2);
          const uint8_t vt = static_cast<uint8_t>(pl[2]);
          const std::string acr = reg->getAttributeAcronym(atc);
          if (!acr.empty()) {
            if (vt == 0 && plen >= 3 + sizeof(uint32_t)) {
              uint32_t v;
              std::memcpy(&v, pl + 3, 4);
              cur->AddIntegerAttribute(acr.c_str(), static_cast<int>(v));
            } else if (vt == 2 && plen >= 3 + sizeof(double)) {
              double v;
              std::memcpy(&v, pl + 3, 8);
              cur->AddDoubleAttribute(acr.c_str(), v);
            } else if (vt == 4 && plen > 3) {
              QByteArray s(pl + 3, static_cast<int>(plen - 3));
              const int z = s.indexOf('\0');
              if (z >= 0) s.truncate(z);
              std::vector<char> vb(s.constData(), s.constData() + s.size() + 1);
              cur->AddStringAttribute(acr.c_str(), vb.data());
            }
          }
        }
        break;
      }
      case FEATURE_GEOMETRY_RECORD_POINT: {
        if (cur && plen >= 2 * sizeof(double)) {
          double lat, lon;
          std::memcpy(&lat, pl, 8);
          std::memcpy(&lon, pl + 8, 8);
          cur->SetPointGeometry(lat, lon, ref_lat, ref_lon);
          cur->Primitive_type = GEO_POINT;
        }
        break;
      }
      case FEATURE_GEOMETRY_RECORD_AREA: {
        if (cur) {
          const char* bedges = nullptr;
          PolyTessGeo* ptg =
              buildOsencPolyTessGeo(pl, plen, ref_lat, ref_lon, &bedges);
          if (ptg && ptg->IsOk()) {
            cur->SetAreaGeometry(ptg, ref_lat, ref_lon);
            // The area also carries its boundary as an edge-index table after
            // the triangles (same [startVC, ±edgeVE, endVC] triples as a line).
            // Attach it as GEO_AREA line geometry so the emit pass can resolve
            // the outline (coastline land-shade, area borders) -- mirrors the
            // wx Osenc reader doing SetAreaGeometry + SetLineGeometry(GEO_AREA).
            uint32_t nEdge = 0;
            std::memcpy(&nEdge, pl + 40, 4);
            int n = 0;
            int* tbl = repackEdgeIndex(
                bedges, nEdge,
                bedges ? static_cast<qint64>(pl + plen - bedges) : 0, &n);
            if (tbl && n > 0) {
              double aext[4];
              std::memcpy(aext, pl, 4 * sizeof(double));  // s_lat,n_lat,w,e
              LineGeometryDescriptor lD;
              lD.extent_s_lat = aext[0]; lD.extent_n_lat = aext[1];
              lD.extent_w_lon = aext[2]; lD.extent_e_lon = aext[3];
              lD.indexCount = n;
              lD.indexTable = tbl;
              cur->SetLineGeometry(&lD, GEO_AREA, ref_lat, ref_lon);
            }
            cur->Primitive_type = GEO_AREA;  // keep AREA (SetLineGeometry reset it)
          } else {
            delete ptg;
          }
        }
        break;
      }
      case FEATURE_GEOMETRY_RECORD_LINE: {
        // payload: 4 doubles (extent) + uint32 edgeVector_count + index table.
        if (cur && plen >= 4 * sizeof(double) + sizeof(uint32_t)) {
          double ext[4];
          std::memcpy(ext, pl, 4 * sizeof(double));
          uint32_t ec;
          std::memcpy(&ec, pl + 32, 4);
          // The table is the rest of the payload; repackEdgeIndex auto-detects
          // 3- vs 4-int stride and yields a canonical signed stride-3
          // [in, ±edge, en] (4th OESU int folded into the edge sign).
          int n = 0;
          int* tbl = repackEdgeIndex(pl + 36, ec,
                                     static_cast<qint64>(plen) - 36, &n);
          if (tbl && n > 0) {
            LineGeometryDescriptor lD;
            lD.extent_s_lat = ext[0]; lD.extent_n_lat = ext[1];
            lD.extent_w_lon = ext[2]; lD.extent_e_lon = ext[3];
            lD.indexCount = n;
            // SetLineGeometry ALIASES this table (no copy); keep it alive for
            // the object's lifetime (intentionally leaked, like the OGR path).
            lD.indexTable = tbl;
            cur->SetLineGeometry(&lD, GEO_LINE, ref_lat, ref_lon);
          }
        }
        break;
      }
      case VECTOR_EDGE_NODE_TABLE_RECORD: {
        const char* r = pl;
        qint64 rem = plen;
        if (rem >= 4) {
          int nCount;
          std::memcpy(&nCount, r, 4);
          r += 4; rem -= 4;
          for (int i = 0; i < nCount && rem >= 8; ++i) {
            int fi, pc;
            std::memcpy(&fi, r, 4); std::memcpy(&pc, r + 4, 4);
            r += 8; rem -= 8;
            const qint64 nb = static_cast<qint64>(pc) * 2 * sizeof(float);
            QVector<float> pts;
            if (pc > 0 && rem >= nb) {
              pts.resize(pc * 2);
              std::memcpy(pts.data(), r, nb);
              r += nb; rem -= nb;
            }
            ve.insert(fi, pts);
          }
        }
        break;
      }
      case VECTOR_CONNECTED_NODE_TABLE_RECORD: {
        const char* r = pl;
        qint64 rem = plen;
        if (rem >= 4) {
          int nCount;
          std::memcpy(&nCount, r, 4);
          r += 4; rem -= 4;
          for (int i = 0; i < nCount && rem >= 12; ++i) {
            int fi;
            float xy[2];
            std::memcpy(&fi, r, 4);
            std::memcpy(xy, r + 4, 8);
            r += 12; rem -= 12;
            vc.insert(fi, QPointF(xy[0], xy[1]));
          }
        }
        break;
      }
      case FEATURE_GEOMETRY_RECORD_MULTIPOINT: {
        // Soundings: extent(4 doubles) + point_count(uint32) + per point
        // (easting, northing, depth) as SM-metre floats relative to the cell
        // ref. Emit each as a depth label, mirroring the NOAA SOUNDG path; the
        // provider declutters them (shallowest-per-cell) and applies SCAMIN.
        if (cur && plen >= 4 * sizeof(double) + sizeof(uint32_t)) {
          uint32_t pc = 0;
          std::memcpy(&pc, pl + 32, 4);
          const char* tbl = pl + 36;
          const qint64 need = static_cast<qint64>(pc) * 3 * sizeof(float);
          if (static_cast<qint64>(plen) - 36 >= need) {
            const int scamin = cur->Scamin;
            for (uint32_t i = 0; i < pc; ++i) {
              float v[3];
              std::memcpy(v, tbl + i * 3 * sizeof(float), 3 * sizeof(float));
              double lat, lon;
              fromSM_plib(v[0], v[1], ref_lat, ref_lon, &lat, &lon);
              const double depth = v[2];
              s52sg::Label lab;
              lab.pos = QPointF(lon, lat);
              lab.color = QColor(60, 60, 60);
              lab.pointSize = 9.0f;
              lab.text = depth < 10.0 ? QString::number(depth, 'f', 1)
                                      : QString::number(qRound(depth));
              lab.scamin = scamin;
              lab.isSounding = true;
              lab.depth = static_cast<float>(depth);
              buf.labels.push_back(lab);
              // Make each sounding queryable (right-click depth -> SOUNDG with
              // its value), mirroring the OGR SPLIT_MULTIPOINT path. The SOUNDG
              // S57Obj stays GEO_META, so it is skipped by the feature pass
              // above -- capture the point here where lon/lat/depth are in hand.
              s52sg::QueryObject sq;
              sq.className = QStringLiteral("SOUNDG");
              sq.geom = s52sg::QueryGeom::Point;
              sq.shape.append(QPointF(lon, lat));
              sq.minLon = sq.maxLon = lon;
              sq.minLat = sq.maxLat = lat;
              sq.attrs.append(
                  {QStringLiteral("VALSOU"), QString::number(depth, 'f', 1)});
              buf.queryObjects.append(std::move(sq));
            }
          }
        }
        // Leave Primitive_type as the META sentinel so the emit pass skips this
        // object -- the labels above already carry the soundings.
        break;
      }
      default:
        break;  // headers, coverage -- not yet emitted
    }
  }

  // The VE/VC tables store SM (easting/northing) metres relative to the cell
  // reference, NOT lon/lat (the OSENC writer runs toSM on every node). Invert
  // them to (lon, lat) now so the resolved line strips feed RenderLineToSG --
  // which emits its points verbatim as geographic coords -- in the same space
  // as the OGR/NOAA path. Without this, lines land at SM-metre "coordinates"
  // far off the chart and never draw. Points are unaffected (their geometry
  // record carries lat/lon doubles directly).
  for (auto it = vc.begin(); it != vc.end(); ++it) {
    double lat, lon;
    fromSM_plib(it.value().x(), it.value().y(), ref_lat, ref_lon, &lat, &lon);
    it.value() = QPointF(lon, lat);
  }
  for (auto it = ve.begin(); it != ve.end(); ++it) {
    QVector<float>& e = it.value();
    for (int k = 0; k + 1 < e.size(); k += 2) {
      double lat, lon;
      fromSM_plib(e[k], e[k + 1], ref_lat, ref_lon, &lat, &lon);
      e[k] = static_cast<float>(lon);
      e[k + 1] = static_cast<float>(lat);
    }
  }

  // --- Emit pass: point symbols/text + resolved line geometry. ---
  auto appendDedup = [](QList<QPointF>& list, const QPointF& pt) {
    if (list.isEmpty() || list.last() != pt) list.append(pt);
  };
  // Resolve one edge-index triple [startVC, ±edgeVE, endVC] (vc/ve already in
  // lon/lat) into a polyline: startNode -> edge points (orientation per sign)
  // -> endNode. Shared by line features and area-boundary outlines.
  // Resolve one edge-index triple [startVC, ±edgeVE, endVC] (vc/ve already in
  // lon/lat) into a polyline: startNode -> edge points (forward if the edge
  // index is positive, reversed if negative -- the OESU direction flag is
  // folded into the sign by repackEdgeIndex) -> endNode. This mirrors wx's
  // CE/EE/EE_REV/EC/CC connector logic. Shared by lines and area boundaries.
  auto resolveSeg = [&](const int* idx) {
    QList<QPointF> pts;
    const int inode = idx[0];
    int ven = idx[1];
    bool fwd = true;
    if (ven < 0) { ven = -ven; fwd = false; }
    const int enode = idx[2];
    const auto itin = vc.constFind(inode);
    if (itin != vc.constEnd()) appendDedup(pts, itin.value());
    if (ven) {
      auto ite = ve.constFind(ven);
      if (ite != ve.constEnd() && !ite.value().isEmpty()) {
        const QVector<float>& e = ite.value();
        const int n = e.size() / 2;
        if (fwd)
          for (int k = 0; k < n; ++k)
            appendDedup(pts, QPointF(e[k * 2], e[k * 2 + 1]));
        else
          for (int k = n - 1; k >= 0; --k)
            appendDedup(pts, QPointF(e[k * 2], e[k * 2 + 1]));
      }
    }
    const auto iten = vc.constFind(enode);
    if (iten != vc.constEnd()) appendDedup(pts, iten.value());
    return pts;
  };
  // Stitch an area's boundary edge-triples into closed rings. The triples
  // [startVC, ±edgeVE, endVC] are NOT stored in geometric order and an area
  // may have several disjoint loops (mainland + islets, holes), so naively
  // concatenating them in stream order joins non-adjacent points -- producing
  // the crossing "X" slivers the coast-shade band then renders. Chain instead
  // by the connected-node (VC) indices: an exact topological join (no float
  // matching), following each directed edge so the ring keeps its winding.
  // Returns one closed ring per loop (lon/lat).
  auto stitchRings = [&](const int* lsindex, int nseg) {
    QList<QList<QPointF>> rings;
    if (!lsindex || nseg <= 0) return rings;
    // Adjacency on BOTH endpoints: the SENC boundary edges are not guaranteed
    // to be stored already oriented in ring order, so chain orientation-
    // agnostically -- match either VC endpoint and reverse the segment when we
    // enter it at its end node.
    QMultiHash<int, int> incident;  // VC node -> segment index (either end)
    for (int i = 0; i < nseg; ++i) {
      incident.insert(lsindex[i * 3], i);
      incident.insert(lsindex[i * 3 + 2], i);
    }
    QVector<bool> used(nseg, false);
    for (int s = 0; s < nseg; ++s) {
      if (used[s]) continue;
      QList<QPointF> ring;
      const int startNode = lsindex[s * 3];
      int node = startNode, cur = s;
      for (int guard = 0; cur >= 0 && !used[cur] && guard <= nseg; ++guard) {
        used[cur] = true;
        const int a = lsindex[cur * 3], b = lsindex[cur * 3 + 2];
        QList<QPointF> seg = resolveSeg(&lsindex[cur * 3]);
        int other;
        if (node == a) {
          other = b;  // traverse forward
        } else {
          std::reverse(seg.begin(), seg.end());  // entered at the end node
          other = a;
        }
        for (const QPointF& p : seg) appendDedup(ring, p);
        if (other == startNode) break;  // ring closed
        int next = -1;  // first unused edge incident to `other`
        for (auto it = incident.constFind(other);
             it != incident.constEnd() && it.key() == other; ++it)
          if (!used[it.value()]) { next = it.value(); break; }
        node = other;
        cur = next;
      }
      // Drop a duplicate closing vertex; makeCoastShadeNode wraps with modulo.
      if (ring.size() >= 2 && ring.first() == ring.last()) ring.removeLast();
      if (ring.size() < 3) continue;
      // Force CW (negative signed area in lon/lat) so the coast-shade builder's
      // left normal points into the land (not the water) after the provider's
      // y-flip to world coords. The stitch start orientation is arbitrary, so a
      // consistent forced winding is required; CW matches the land side.
      double area = 0.0;
      for (int i = 0, n = ring.size(); i < n; ++i) {
        const QPointF& p = ring[i];
        const QPointF& q = ring[(i + 1) % n];
        area += p.x() * q.y() - q.x() * p.y();
      }
      if (area > 0.0) std::reverse(ring.begin(), ring.end());
      rings.append(std::move(ring));
    }
    return rings;
  };
  int n_points = 0, n_lines = 0, n_areas = 0;
  for (S57Obj* obj : objects) {
    // P2.16 -- "Chart information objects" gate. Legacy ObjectRenderCheckCat
    // (s52plib.cpp:10680) suppresses M_* meta objects (M_QUAL/M_COVR/M_NSYS/...)
    // when Show-Meta is off; skip the whole object so none of its
    // area/line/symbol/text emits. (Object-query snapshots, built in the
    // separate pass below, are unaffected.)
    if (plib && !plib->m_bShowMeta && obj->FeatureName[0] == 'M' &&
        obj->FeatureName[1] == '_')
      continue;
    if (obj->Primitive_type == GEO_POINT) {
      LUPrec* lup = plib->S52_LUPLookup(PAPER_CHART, obj->FeatureName, obj);
      if (!lup) continue;
      plib->_LUP2rules(lup, obj);
      ObjRazRules rz;
      rz.obj = obj; rz.LUP = lup; rz.sm_transform_parms = nullptr;
      rz.child = nullptr; rz.next = nullptr; rz.mps = nullptr;
      plib->RenderPointSymbolToSG(buf, &rz, obj->m_lon, obj->m_lat);
      plib->RenderTextToSG(buf, &rz, obj->m_lon, obj->m_lat);
      ++n_points;
    } else if (obj->Primitive_type == GEO_LINE) {
      // Each edge-triple [startVC, ±edgeVE, endVC] is an INDEPENDENT segment:
      // startNode -> edge points -> endNode (the start/end "connectors" of the
      // wx s57chart line_segment_element list, types CE/EE/EC/CC). Mirror that
      // by emitting one polyline per triple -- the triples within a feature are
      // NOT guaranteed to be listed in geometrically contiguous order, so
      // concatenating them into a single strip draws spurious connector lines
      // between unrelated nodes (the chart-wide "spider web").
      LUPrec* lup = plib->S52_LUPLookup(LINES, obj->FeatureName, obj);
      if (!lup) continue;
      plib->_LUP2rules(lup, obj);  // resolve symbology + CS rules once per obj
      ObjRazRules rz;
      rz.obj = obj; rz.LUP = lup; rz.sm_transform_parms = nullptr;
      rz.child = nullptr; rz.next = nullptr; rz.mps = nullptr;
      bool any = false;
      for (int iseg = 0; iseg < obj->m_n_lsindex; ++iseg) {
        const QList<QPointF> pts = resolveSeg(&obj->m_lsindex_array[iseg * 3]);
        if (pts.size() < 2) continue;
        plib->RenderLineToSG(buf, &rz, pts);
        any = true;
      }
      if (any) ++n_lines;
      // Line features also carry SY symbols and TX/TE text via their LUP/CS
      // (e.g. a name along a fairway, a cable/pipe label), anchored at the
      // feature's base point (extent centre). wx walks every rule type per
      // object; mirror that.
      plib->RenderPointSymbolToSG(buf, &rz, obj->m_lon, obj->m_lat);
      plib->RenderTextToSG(buf, &rz, obj->m_lon, obj->m_lat);
    } else if (obj->Primitive_type == GEO_AREA) {
      LUPrec* lup = plib->S52_LUPLookup(PLAIN_BOUNDARIES, obj->FeatureName, obj);
      if (!lup) continue;
      plib->_LUP2rules(lup, obj);
      ObjRazRules rz;
      rz.obj = obj; rz.LUP = lup; rz.sm_transform_parms = nullptr;
      rz.child = nullptr; rz.next = nullptr; rz.mps = nullptr;
      plib->RenderAreaToSG(buf, &rz);
      // P2.15: area BOUNDARY lines. RenderAreaToSG emits only the AC/AP fill;
      // the area's S-52 boundary line rules (RUL_SIM_LN / RUL_COM_LN in its
      // PLAIN_/SYMBOLIZED_BOUNDARIES LUP -- depth-area edges, DRGARE/RESARE
      // borders, ...) are not. Walk the boundary edge-triples (the same ones
      // used for the LNDARE coast-shade below) and feed each to RenderLineToSG
      // with the area's rz; it dispatches those boundary rules exactly as for a
      // line feature (priority-sorted, so the border draws over the fill).
      // Mirrors wx's second RenderObjectToGL pass over area objects.
      for (int iseg = 0; iseg < obj->m_n_lsindex; ++iseg) {
        const QList<QPointF> bpts = resolveSeg(&obj->m_lsindex_array[iseg * 3]);
        if (bpts.size() >= 2) plib->RenderLineToSG(buf, &rz, bpts);
      }
      // Area features also carry centroid SY symbols and TX/TE text via their
      // LUP/CS (restricted-area markers, anchorage symbols, area names, the
      // depth label of a DEPARE, etc.), anchored at the area base point (extent
      // centre). wx walks every rule type per object; mirror that.
      plib->RenderPointSymbolToSG(buf, &rz, obj->m_lon, obj->m_lat);
      plib->RenderTextToSG(buf, &rz, obj->m_lon, obj->m_lat);
      ++n_areas;
      // LNDARE boundary -> coastline land-shade. Stitch the boundary edges
      // into proper closed, contiguous rings (by VC node index). Concatenating
      // the triples in stream order joined non-adjacent points, so the shade
      // band drew crossing slivers across the polygon (the "X" artifacts).
      if (strncmp(obj->FeatureName, "LNDARE", 6) == 0) {
        for (QList<QPointF>& ring :
             stitchRings(obj->m_lsindex_array, obj->m_n_lsindex))
          buf.landContours.append(std::move(ring));
      }
    }
  }

  // --- Object-query snapshots: one QueryObject per feature (class + attrs +
  // bbox + shape, in lon/lat) so the right-click "Object query" popup works for
  // o-charts/OSENC cells too -- the OGR/.000 path builds these in loadOneCell,
  // this path never did (object query was always blank for o-charts). Built as
  // a SEPARATE pass so it is not gated by the LUP lookups in the emit loop
  // (which `continue` past unsymbolised objects). The vc/ve tables are already
  // inverted to lon/lat above, so resolveSeg/stitchRings yield geographic
  // shapes that S52VectorChartProvider::objectsAt hit-tests directly. Soundings
  // are captured in the MULTIPOINT case (their S57Obj stays GEO_META here).
  for (S57Obj* obj : objects) {
    s52sg::QueryObject qo;
    qo.className = QString::fromLatin1(obj->FeatureName).trimmed();
    if (obj->att_array)
      for (int i = 0; i < obj->n_attr; ++i) {
        char acr[7] = {0};
        std::memcpy(acr, obj->att_array + 6 * i, 6);
        const QString name = QString::fromLatin1(acr).trimmed();
        if (name.isEmpty()) continue;
        const QString val =
            wxString_to_QString(obj->GetAttrValueAsString(acr)).trimmed();
        if (!val.isEmpty()) qo.attrs.append({name, val});
      }
    if (obj->Primitive_type == GEO_POINT) {
      qo.geom = s52sg::QueryGeom::Point;
      qo.shape.append(QPointF(obj->m_lon, obj->m_lat));
    } else if (obj->Primitive_type == GEO_LINE) {
      qo.geom = s52sg::QueryGeom::Line;
      for (int iseg = 0; iseg < obj->m_n_lsindex; ++iseg)
        for (const QPointF& p : resolveSeg(&obj->m_lsindex_array[iseg * 3]))
          qo.shape.append(p);
    } else if (obj->Primitive_type == GEO_AREA) {
      qo.geom = s52sg::QueryGeom::Area;
      const QList<QList<QPointF>> rings =
          stitchRings(obj->m_lsindex_array, obj->m_n_lsindex);
      if (!rings.isEmpty()) qo.shape = rings.first();
    } else {
      continue;  // GEO_META / unhandled -- nothing to hit-test
    }
    if (qo.shape.isEmpty()) continue;
    double mnx = qo.shape[0].x(), mxx = mnx, mny = qo.shape[0].y(), mxy = mny;
    for (const QPointF& p : qo.shape) {
      mnx = std::min(mnx, p.x());
      mxx = std::max(mxx, p.x());
      mny = std::min(mny, p.y());
      mxy = std::max(mxy, p.y());
    }
    qo.minLon = mnx;
    qo.maxLon = mxx;
    qo.minLat = mny;
    qo.maxLat = mxy;
    buf.queryObjects.append(std::move(qo));
  }

  if (on) *on = nlat;
  if (os) *os = slat;
  if (oe) *oe = elon;
  if (ow) *ow = wlon;
  // Draw in S-52 display-priority order (group-1 area fills under line/area
  // symbols), stable so same-priority objects keep emit order. Without this,
  // an object's area fill emitted after a line painted over it (pontoon cut).
  std::stable_sort(buf.prims.begin(), buf.prims.end(),
                   [](const s52sg::Prim& a, const s52sg::Prim& b) {
                     return a.priority < b.priority;
                   });
  qWarning(
      "decodeOsenc: %d objects -- %d areas, %d points, %d lines (%d edges, "
      "%d nodes)%s",
      static_cast<int>(objects.size()), n_areas, n_points, n_lines, ve.size(),
      vc.size(), have_extent ? "" : " [no extent]");
  return buf;
}

CellExtent S52Engine::scanOneCellExtent(const QString& path_000,
                                        const QString& s57data_dir) {
  CellExtent ce;
  if (!m_impl->lib || !m_impl->lib->m_bOK) return ce;
  if (!m_impl->registrar) m_impl->registrar = makeRegistrar(s57data_dir);
  S57ClassRegistrar* registrar = m_impl->registrar;
  if (!registrar) return ce;

  OGRS57DataSource ds;
  ds.SetS57Registrar(registrar);
  // Assemble feature geometry (so envelopes are populated) but skip the
  // expensive extras a full decode wants -- no depth ordinate, no
  // multipoint splitting needed just for a bounding box.
  const char* opts[] = {"RETURN_PRIMITIVES=OFF", "RETURN_LINKAGES=OFF",
                        "LNAM_REFS=OFF", nullptr};
  ds.SetOptionList(const_cast<char**>(opts));
  if (ds.Open(path_000.toUtf8().constData(), TRUE)) return ce;  // open failed

  ce.path = path_000;
  ce.name = QFileInfo(path_000).completeBaseName();
  ce.band = CellExtent::bandFromName(ce.name);

  // OGRS57Layer::GetNextFeature is disabled in the vendored driver; read
  // straight from the reader module (as the loader does).
  S57Reader* reader = ds.GetModule(0);
  if (reader) {
    // Native compilation scale (DSPM:CSCL), populated during ingest -- the
    // 1:N the cell was drawn at. Drives the quilt's per-zoom tier choice.
    ce.nativeScale = reader->GetCSCL();
    reader->Rewind();
    OGRFeature* feat;
    while ((feat = reader->ReadNextFeature()) != nullptr) {
      if (OGRGeometry* geom = feat->GetGeometryRef()) {
        OGREnvelope env;
        geom->getEnvelope(&env);
        if (env.MaxY > ce.north) ce.north = env.MaxY;
        if (env.MinY < ce.south) ce.south = env.MinY;
        if (env.MaxX > ce.east) ce.east = env.MaxX;
        if (env.MinX < ce.west) ce.west = env.MinX;
      }
      if (OGRFeatureDefn* fd = feat->GetDefnRef()) {
        const char* cn = fd->GetName();
        // Substantive chart-surface objects: depth area/contour, soundings,
        // land/coastline. A cell with none is administrative (EEZ / coverage
        // only) and is excluded from the quilt.
        if (cn && (strncmp(cn, "DEPARE", 6) == 0 ||
                   strncmp(cn, "DEPCNT", 6) == 0 ||
                   strncmp(cn, "SOUNDG", 6) == 0 ||
                   strncmp(cn, "LNDARE", 6) == 0 ||
                   strncmp(cn, "COALNE", 6) == 0))
          ++ce.navFeatures;
        // M_COVR with CATCOV=1 is the cell's actual data-coverage polygon(s).
        else if (cn && strcmp(cn, "M_COVR") == 0) {
          const int ci = feat->GetFieldIndex("CATCOV");
          const int catcov =
              (ci >= 0 && feat->IsFieldSet(ci)) ? feat->GetFieldAsInteger(ci)
                                                : 0;
          if (catcov == 1) {
            auto addPoly = [&](OGRPolygon* poly) {
              OGRLinearRing* r = poly ? poly->getExteriorRing() : nullptr;
              if (!r) return;
              const int np = r->getNumPoints();
              QPolygonF qp;
              qp.reserve(np);
              for (int i = 0; i < np; ++i)
                qp << QPointF(r->getX(i), r->getY(i));  // (lon, lat)
              if (qp.size() >= 3) ce.coverage.append(qp);
            };
            if (OGRGeometry* cg = feat->GetGeometryRef()) {
              const OGRwkbGeometryType gt = wkbFlatten(cg->getGeometryType());
              if (gt == wkbPolygon) {
                addPoly(static_cast<OGRPolygon*>(cg));
              } else if (gt == wkbMultiPolygon) {
                auto* mp = static_cast<OGRMultiPolygon*>(cg);
                for (int k = 0; k < mp->getNumGeometries(); ++k)
                  addPoly(static_cast<OGRPolygon*>(mp->getGeometryRef(k)));
              }
            }
          }
        }
      }
      OGRFeature::DestroyFeature(feat);
    }
  }
  return ce;  // caller checks ce.valid()
}

QList<CellExtent> S52Engine::scanCellExtents(const QStringList& paths_000,
                                             const QString& s57data_dir) {
  QList<CellExtent> out;
  out.reserve(paths_000.size());
  for (const QString& path : paths_000) {
    CellExtent ce = scanOneCellExtent(path, s57data_dir);
    if (ce.valid()) out.push_back(ce);
  }
  qWarning("scanCellExtents: %lld/%lld cells catalogued",
           (long long)out.size(), (long long)paths_000.size());
  return out;
}

bool S52Engine::isOk() const {
  return m_impl->lib && m_impl->lib->m_bOK;
}

QString S52Engine::status() const {
  return m_impl->status;
}

void S52Engine::setColorScheme(int scheme) {
  if (!m_impl || !m_impl->lib) return;
  ColorScheme cs = GLOBAL_COLOR_SCHEME_DAY;
  if (scheme == 1)
    cs = GLOBAL_COLOR_SCHEME_DUSK;
  else if (scheme == 2)
    cs = GLOBAL_COLOR_SCHEME_NIGHT;
  // Mutates the shared s52plib colour table; must run on the decode thread
  // (s52plib is not re-entrant). Subsequent cell decodes emit the new palette.
  ChartCtx ctx(false, 0);
  m_impl->lib->SetPLIBColorScheme(cs, ctx);
  m_impl->lib->UpdateMarinerParams();
}

void S52Engine::applyDisplaySettings(const ChartDisplaySettings& s) {
  if (!m_impl || !m_impl->lib) return;
  s52plib* lib = m_impl->lib;
  // Symbol + boundary LUP selection (consulted at decode time).
  lib->m_nSymbolStyle = s.symbolStyle == 1 ? SIMPLIFIED : PAPER_CHART;
  lib->m_nBoundaryStyle =
      s.boundaryStyle == 1 ? SYMBOLIZED_BOUNDARIES : PLAIN_BOUNDARIES;
  lib->SetShowS57ImportantTextOnly(s.importantTextOnly);
  lib->m_bUseSCAMIN = s.useScamin;
  // P2.16 -- cartography / text-detail toggles. These select which objects and
  // text strings are turned into geometry at decode time, so a re-decode
  // (reloadResidentCells) is what makes a change take effect.
  lib->m_bShowMeta = s.chartInfoObjects;
  lib->SetShowAtonText(s.buoyLightLabels);
  lib->SetShowLdisText(s.lightDescriptions);
  lib->SetExtendLightSectors(s.extendedLightSectors);
  lib->SetShowNationalText(s.nationalText);
  lib->SetTextOverlapAvoid(s.declutterText);
  lib->m_bUseSUPER_SCAMIN = s.superScamin;
  // Depth shading + contours (the DEPARE/DEPCNT colour-fill conditional
  // symbology reads these mariner params). Safety contour drives the
  // safety-depth shade too.
  S52_setMarinerParam(S52_MAR_TWO_SHADES, s.twoShades ? 1.0 : 0.0);
  S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR, s.safetyContour);
  S52_setMarinerParam(S52_MAR_SAFETY_DEPTH, s.safetyContour);
  S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, s.shallowContour);
  S52_setMarinerParam(S52_MAR_DEEP_CONTOUR, s.deepContour);
  lib->UpdateMarinerParams();
}

}  // namespace ocpn::qtui
