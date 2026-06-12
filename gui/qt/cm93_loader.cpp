/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "cm93_loader.h"

#include <cstring>

#include <QColor>
#include <QPointF>

#include "bbox.h"
#include "mygeom.h"
#include "s52plib.h"
#include "s52s57.h"
#include "x11_macro_scrub.h"  // the wx/GL headers above define Status & co.

#include "cm93_cell_reader.h"
#include "cm93_dictionary.h"
#include "cm93_transcoder.h"

namespace ocpn::qtui {

namespace {

// The minimal per-cell chart context the CS procedures need (mirrors
// s52_engine.cpp's file-local MakeMinimalChartContext: zeroed struct,
// chart=nullptr, a sane safety contour).
chart_context* makeMinimalChartContext(double ref_lat, double ref_lon) {
  auto* ctx = new chart_context();
  std::memset(ctx, 0, sizeof(chart_context));
  ctx->chart = nullptr;
  ctx->ref_lat = ref_lat;
  ctx->ref_lon = ref_lon;
  ctx->safety_contour = 30.0;  // S-52 default deep safety contour
  ctx->chart_type = 0;
  return ctx;
}

// Emit one assembled S57Obj through the LUP -> rules -> RenderToSG chain,
// mirroring the OGR path's per-geometry drivers (s52_engine.cpp:643+).
void emitObj(s52plib* plib, s52sg::Buffer& buf, S57Obj* obj,
             Cm93Transcoder& tx) {
  LUPname tname;
  switch (obj->Primitive_type) {
    case GEO_AREA:
      tname = plib->m_nBoundaryStyle;
      break;
    case GEO_LINE:
      tname = LINES;
      break;
    default:
      tname = plib->m_nSymbolStyle;
      break;
  }
  LUPrec* lup = plib->S52_LUPLookup(tname, obj->FeatureName, obj);
  if (!lup) return;
  plib->_LUP2rules(lup, obj);
  ObjRazRules rz;
  rz.obj = obj;
  rz.LUP = lup;
  rz.sm_transform_parms = nullptr;
  rz.child = nullptr;
  rz.next = nullptr;
  rz.mps = nullptr;

  switch (obj->Primitive_type) {
    case GEO_AREA:
      if (obj->pPolyTessGeo) plib->RenderAreaToSG(buf, &rz);
      plib->RenderPointSymbolToSG(buf, &rz, obj->m_lon, obj->m_lat);
      plib->RenderTextToSG(buf, &rz, obj->m_lon, obj->m_lat);
      break;
    case GEO_LINE: {
      // geoPt holds CELL-LOCAL 16-bit vertices (createS57Obj case 2);
      // map each through the cell transform to (lon, lat).
      if (!obj->geoPt || obj->npt < 2) break;
      QList<QPointF> pts;
      pts.reserve(obj->npt);
      const pt* v = obj->geoPt;
      for (int i = 0; i < obj->npt; ++i) {
        cm93_point p;
        p.x = static_cast<unsigned short>(v[i].x);
        p.y = static_cast<unsigned short>(v[i].y);
        double lat = 0, lon = 0;
        tx.transformPoint(&p, 0., 0., &lat, &lon);
        pts.append(QPointF(lon, lat));
      }
      plib->RenderLineToSG(buf, &rz, pts);
      const QPointF anchor = pts.at(pts.size() / 2);
      plib->RenderPointSymbolToSG(buf, &rz, anchor.x(), anchor.y());
      plib->RenderTextToSG(buf, &rz, anchor.x(), anchor.y());
      break;
    }
    default: {  // GEO_POINT
      if (obj->npt > 1 && obj->geoPtz && obj->geoPtMulti) {
        // Sounding cluster: depths in geoPtz[3i+2], positions (lon, lat)
        // in geoPtMulti -- emit depth labels like the NOAA SOUNDG path.
        for (int i = 0; i < obj->npt; ++i) {
          const double depth = obj->geoPtz[i * 3 + 2];
          s52sg::Label lab;
          lab.pos = QPointF(obj->geoPtMulti[i * 2],
                            obj->geoPtMulti[i * 2 + 1]);
          lab.color = QColor(60, 60, 60);
          lab.pointSize = 11.0f;
          lab.text = depth < 10.0 ? QString::number(depth, 'f', 1)
                                  : QString::number(qRound(depth));
          lab.isSounding = true;
          lab.depth = static_cast<float>(depth);
          buf.labels.push_back(lab);
        }
        break;
      }
      plib->RenderPointSymbolToSG(buf, &rz, obj->m_lon, obj->m_lat);
      plib->RenderTextToSG(buf, &rz, obj->m_lon, obj->m_lat);
      break;
    }
  }
}

}  // namespace

bool Cm93Loader::loadCell(s52plib* plib, const QString& path,
                          const Cm93Dictionary* dict, s52sg::Buffer* out,
                          double* north, double* south, double* east,
                          double* west) {
  if (!plib || !dict || !out) return false;

  Cm93CellBlock cib;
  if (!Cm93CellReader::ingest(path, &cib)) return false;

  Cm93Transcoder tx(&cib, dict);
  chart_context* ctx = makeMinimalChartContext(cib.min_lat, cib.min_lon);

  double n = -90, s = 90, e = -180, w = 180;
  for (int i = 0; i < cib.m_nfeature_records; ++i) {
    Cm93Object* po = &cib.pobject_block[i];
    if ((po->geotype & 0x0f) == 0) continue;  // no geometry (parents)
    Extended_Geometry* xg = tx.buildGeom(po, i);
    if (!xg) continue;
    // view_scale_ppm only sizes the worst-case point-label bbox; a mid
    // value keeps the boxes sane at any zoom.
    S57Obj* obj = tx.createS57Obj(0, i, 0, po, xg, 0.0005);
    if (!obj) continue;
    obj->m_chart_context = ctx;
    emitObj(plib, *out, obj, tx);
    n = std::max(n, obj->BBObj.GetMaxLat());
    s = std::min(s, obj->BBObj.GetMinLat());
    e = std::max(e, obj->BBObj.GetMaxLon());
    w = std::min(w, obj->BBObj.GetMinLon());
    // Areas keep their xgeom inside PolyTessGeo; the object itself is
    // emit-complete now. (Leaked like the OGR proof-of-pipeline path; the
    // queryable-feature capture owns lifetimes when that lands for CM93.)
  }
  if (north) *north = n;
  if (south) *south = s;
  if (east) *east = e;
  if (west) *west = w;
  return true;
}

}  // namespace ocpn::qtui
