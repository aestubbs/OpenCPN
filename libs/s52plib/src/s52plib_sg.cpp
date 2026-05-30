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
 * s52plib scene-graph emit path (P2.8c).
 *
 * A third output target alongside RenderObjectToDC / RenderObjectToGL.
 * Instead of projecting geometry to screen pixels and rasterising, it
 * resolves S-52 symbology (colours, the conditional-symbology procedures)
 * and appends *world-coordinate* primitives (lon/lat) to an s52sg::Buffer.
 * The Qt scene graph applies the world->screen transform on the GPU, so
 * pan/zoom needs no re-decode and stays vector-sharp.
 *
 * This first step covers area fills (RUL_ARE_CO). Lines, point symbols
 * and text follow. The dispatch mirrors RenderAreaToGL so the two paths
 * stay recognisably parallel.
 */

#include <wx/wx.h>

#include <cmath>
#include <cstring>

#include <QHash>
#include <QString>
#include <QStringList>

#include "model/wx_qt_ui_types.h"  // WxImageToQImage

#include "bbox.h"  // LLBBox, required transitively by mygeom.h
#include "chartsymbols.h"  // ChartSymbols::GetImage (raster symbol atlas)
#include "mygeom.h"
#include "s52plib.h"
#include "s52s57.h"

// SM -> lon/lat inverse projection (declared in s52plib.h).
extern void fromSM_plib(double x, double y, double lat0, double lon0,
                        double *lat, double *lon);

// Memoise atlas bitmaps by S-52 symbol/pattern name. Every feature that
// uses the same symbol (e.g. dozens of lateral buoys) or AP pattern shares
// one implicitly-shared QImage: this skips the repeated wxImage->QImage deep
// copy AND gives all instances an identical QImage::cacheKey(), which the
// Qt-side TextureCacheNode keys on so the bitmap uploads to the GPU exactly
// once per cell subtree (P2.5). `name` is a fixed-width field (char[8],
// space/NUL padded); the raw bytes form a stable key.
//
// Thread note: chart load + emit is synchronous on the main thread, so the
// static cache needs no lock today. Guard it if emit ever moves to a worker.
static QImage cachedAtlasImage(ChartSymbols& symbols, const char* name) {
  static QHash<QString, QImage> cache;
  const QString key = QString::fromLatin1(name, 8);
  auto it = cache.constFind(key);
  if (it != cache.constEnd()) return it.value();
  wxImage img = symbols.GetImage(name);
  QImage q = img.IsOk() ? WxImageToQImage(img) : QImage();
  cache.insert(key, q);
  return q;
}

// Map an S-57 object class (FeatureName, e.g. "LIGHTS", "BOYLAT", "BCNCAR")
// to a viewing group for the mariner display toggles (P2.9).
static int viewGroupFor(const char* featureName) {
  if (!featureName) return s52sg::VgOther;
  if (std::strncmp(featureName, "LIGHTS", 6) == 0) return s52sg::VgLights;
  if (std::strncmp(featureName, "BOY", 3) == 0 ||
      std::strncmp(featureName, "BCN", 3) == 0)
    return s52sg::VgBuoysBeacons;
  return s52sg::VgOther;
}

// Map an S-52 display category to the scene-graph rank used for the
// Base/Standard/All filter.
static int dispRank(DisCat disc) {
  switch (disc) {
    case DISPLAYBASE:
      return s52sg::CatBase;
    case STANDARD:
    case MARINERS_STANDARD:
      return s52sg::CatStandard;
    case OTHER:
    default:
      return s52sg::CatOther;
  }
}

// Append one tessellated polygon (the object's PolyTessGeo) to `out` as
// triangle primitives in lon/lat, coloured `c`. The TriPrim vertices are
// SM metres relative to the PolyTessGeo feature reference; invert each
// back to geographic coords so the consumer can project them itself.
int s52plib::RenderToSGAC(s52sg::Buffer &out, ObjRazRules *rzRules,
                          Rules *rules) {
  S52color *c = getColor((char *)rules->INSTstr);
  if (!c) return 0;

  PolyTessGeo *ppg_geo = rzRules->obj->pPolyTessGeo;
  if (!ppg_geo) return 0;

  // Deferred tesselation, if the geometry has not been built yet.
  if (!ppg_geo->IsOk()) ppg_geo->BuildDeferredTess();

  double ref_lat = 0, ref_lon = 0;
  ppg_geo->GetChartRefPos(&ref_lat, &ref_lon);

  PolyTriGroup *ppg = ppg_geo->Get_PolyTriGroup_head();
  if (!ppg) return 0;

  const bool is_double = (ppg->data_type == DATA_TYPE_DOUBLE);
  const QColor color(c->R, c->G, c->B);
  const int dc = dispRank(rzRules->LUP->DISC);
  const int prio = rzRules->LUP->DPRI - '0';
  // SCAMIN (P2.14): only carry a real value when the Use-SCAMIN toggle is on;
  // otherwise the unset sentinel keeps the fill always visible. An un-SCAMIN'd
  // area fill intentionally keeps the sentinel so the consumer never culls it
  // (it must persist as the composite underlay).
  const int scamin =
      (m_bUseSCAMIN && rzRules->obj) ? rzRules->obj->Scamin : 100000002;

  for (TriPrim *p_tp = ppg->tri_prim_head; p_tp; p_tp = p_tp->p_next) {
    s52sg::Prim prim;
    prim.dispCat = dc;
    prim.priority = prio;
    prim.scamin = scamin;
    switch (p_tp->type) {
      case PTG_TRIANGLE_STRIP:
        prim.type = s52sg::PrimType::TriangleStrip;
        break;
      case PTG_TRIANGLE_FAN:
        prim.type = s52sg::PrimType::TriangleFan;
        break;
      case PTG_TRIANGLES:
      default:
        prim.type = s52sg::PrimType::Triangles;
        break;
    }
    prim.color = color;

    prim.verts.reserve(p_tp->nVert);
    // p_vertex stores x,y,x,y... in SM metres. After BuildTessGLU the
    // single-allocation buffer is float; the legacy SENC path may keep
    // doubles. Read accordingly.
    const float *pf = reinterpret_cast<const float *>(p_tp->p_vertex);
    const double *pd = reinterpret_cast<const double *>(p_tp->p_vertex);
    for (int i = 0; i < p_tp->nVert; ++i) {
      double east, north;
      if (is_double) {
        east = pd[2 * i];
        north = pd[2 * i + 1];
      } else {
        east = pf[2 * i];
        north = pf[2 * i + 1];
      }
      double lat, lon;
      fromSM_plib(east, north, ref_lat, ref_lon, &lat, &lon);
      prim.verts.push_back(QPointF(lon, lat));  // (lon, lat)
    }
    out.prims.push_back(std::move(prim));
  }
  return 1;
}

// Resolve an LS (simple line) instruction's pen and append `pts` as a
// coloured line strip. INSTstr format: "<style:4>,<width>,<colour>" e.g.
// "SOLD,2,CHGRD" -- style at [0..3], width at [5], colour token at [7].
int s52plib::RenderToSGLS(s52sg::Buffer &out, Rules *rules,
                          const QList<QPointF> &pts, int dispCat, int priority,
                          int scamin) {
  if (pts.size() < 2 || !rules->INSTstr) return 0;
  char *str = (char *)rules->INSTstr;
  S52color *c = getColor(str + 7);
  if (!c) return 0;

  s52sg::Prim prim;
  prim.type = s52sg::PrimType::LineStrip;
  prim.color = QColor(c->R, c->G, c->B);
  prim.width = static_cast<float>(atoi(str + 5));
  if (prim.width < 1.0f) prim.width = 1.0f;
  // Line style is the 4-char token at the start of INSTstr (SOLD/DASH/DOTT).
  // wx: DASH ~3mm period 66% on; DOTT ~1mm period 50% (RenderLS_Dash_GLSL).
  if (!strncmp(str, "DASH", 4)) {
    prim.dashOnMm = 2.0f;
    prim.dashOffMm = 1.0f;
  } else if (!strncmp(str, "DOTT", 4)) {
    prim.dashOnMm = 0.5f;
    prim.dashOffMm = 0.5f;
  }
  prim.verts = pts;
  prim.dispCat = dispCat;
  prim.priority = priority;
  prim.scamin = scamin;
  out.prims.push_back(std::move(prim));
  return 1;
}

// Resolve a parsed S52_TextC into a Label and append it. The string,
// colour and nominal point size come from s52plib's text parse; the
// consumer renders it with a system font (not TexFont/DepthFont).
static void EmitTextC(s52sg::Buffer &out, S52_TextC *text, double anchor_lon,
                      double anchor_lat, int scamin, int dispCat, int viewGroup) {
  if (!text || text->frmtd.IsEmpty()) return;
  s52sg::Label label;
  label.pos = QPointF(anchor_lon, anchor_lat);
  label.text = QString::fromUtf8(text->frmtd.ToUTF8().data());
  if (text->pcol)
    label.color = QColor(text->pcol->R, text->pcol->G, text->pcol->B);
  else
    label.color = QColor(0, 0, 0);
  // bsize is the S-52 body size in points-ish; map directly for now.
  label.pointSize = text->bsize > 0 ? static_cast<float>(text->bsize) : 10.0f;
  label.hjust = text->hjust;
  label.vjust = text->vjust;
  label.scamin = scamin;
  label.dispCat = dispCat;
  label.viewGroup = viewGroup;
  out.labels.push_back(std::move(label));
}

int s52plib::RenderTextToSG(s52sg::Buffer &out, ObjRazRules *rzRules,
                            double anchor_lon, double anchor_lat) {
  if (!rzRules || !rzRules->LUP) return 0;

  const int scamin = rzRules->obj ? rzRules->obj->Scamin : 100000002;
  const int dc = dispRank(rzRules->LUP->DISC);
  const int vg =
      rzRules->obj ? viewGroupFor(rzRules->obj->FeatureName) : s52sg::VgOther;
  auto handle = [&](Rules *rules) {
    if (rules->ruleType == RUL_TXT_TX) {
      S52_TextC *t = S52_PL_parseTX(rzRules, rules, (char *)rules->INSTstr);
      EmitTextC(out, t, anchor_lon, anchor_lat, scamin, dc, vg);
      delete t;
    } else if (rules->ruleType == RUL_TXT_TE) {
      S52_TextC *t = S52_PL_parseTE(rzRules, rules, (char *)rules->INSTstr);
      EmitTextC(out, t, anchor_lon, anchor_lat, scamin, dc, vg);
      delete t;
    }
  };

  Rules *rules = rzRules->LUP->ruleList;
  while (rules != NULL) {
    if (rules->ruleType == RUL_CND_SY) {
      if (!rzRules->obj->bCS_Added) {
        rzRules->obj->CSrules = NULL;
        GetAndAddCSRules(rzRules, rules);
        rzRules->obj->bCS_Added = 1;
      }
      Rules *rules_last = rules;
      Rules *cs = rzRules->obj->CSrules;
      while (NULL != cs) {
        handle(cs);
        rules_last = cs;
        cs = cs->next;
      }
      rules = rules_last;
    } else {
      handle(rules);
    }
    rules = rules->next;
  }
  return 1;
}

// Walk a point object's rule list for SY (symbol) rules and append each
// raster symbol -- cropped from the S-52 atlas, with its pivot/hot-spot --
// as an s52sg::Symbol. Conditional symbology (e.g. the buoy/beacon CS
// procedures) is expanded first. Vector (HPGL) symbols are deferred.
int s52plib::RenderPointSymbolToSG(s52sg::Buffer &out, ObjRazRules *rzRules,
                                   double anchor_lon, double anchor_lat) {
  if (!rzRules || !rzRules->LUP) return 0;
  const int scamin = rzRules->obj ? rzRules->obj->Scamin : 100000002;
  const int dc = dispRank(rzRules->LUP->DISC);
  const int vg =
      rzRules->obj ? viewGroupFor(rzRules->obj->FeatureName) : s52sg::VgOther;

  auto emitSY = [&](Rules *rules) {
    Rule *prule = rules->razRule;
    if (!prule) return;

    // Symbol rotation (mirrors RenderSY): a supplementary angle in the SY()
    // instruction (e.g. SY(LIGHTS11,135) -> the standard ATON flare lean) is
    // overridden by an ORIENT attribute (the object's own bearing; +180 for
    // LIGHTS). Without this, every point symbol drew upright at 0 deg.
    float angle = 0.0f;
    if (rules->INSTstr && rules->INSTstr[8] == ',') {
      char sangle[16];
      int cp = 0;
      while (rules->INSTstr[cp + 9] && rules->INSTstr[cp + 9] != ')' &&
             cp < 15) {
        sangle[cp] = rules->INSTstr[cp + 9];
        ++cp;
      }
      sangle[cp] = 0;
      angle = static_cast<float>(atoi(sangle));
    }
    double orient;
    if (rzRules->obj && GetDoubleAttr(rzRules->obj, "ORIENT", orient)) {
      angle = static_cast<float>(orient);
      if (strncmp(rzRules->obj->FeatureName, "LIGHTS", 6) == 0) {
        angle += 180.0f;
        if (angle > 360.0f) angle -= 360.0f;
      }
    }

    if (prule->definition.SYDF == 'R') {  // raster symbol from the atlas
      QImage qimg = cachedAtlasImage(m_chartSymbols, prule->name.SYNM);
      if (qimg.isNull()) return;
      s52sg::Symbol sym;
      sym.pos = QPointF(anchor_lon, anchor_lat);
      sym.image = qimg;
      sym.pivot = QPointF(prule->pos.symb.pivot_x.SYCL,
                          prule->pos.symb.pivot_y.SYRW);
      sym.rotationDeg = angle;
      sym.scamin = scamin;
      sym.dispCat = dc;
      sym.viewGroup = vg;
      out.symbols.push_back(std::move(sym));
    } else if (prule->definition.SYDF == 'V' && prule->vector.SVCT) {
      // Vector (HPGL) symbol -> billboard geometry. Render with r=(0,0),
      // rot=0 so the captured Line/Circle/Polygon coords are symbol-local
      // pixels relative to the pivot. g_scaminScale (a global the GL path
      // sets in ObjectRenderCheckCat, which we bypass) must be 1.
      extern float g_scaminScale;
      g_scaminScale = 1.0f;
      s52sg::VectorSymbol vsym;
      vsym.pos = QPointF(anchor_lon, anchor_lat);
      vsym.scamin = scamin;
      vsym.dispCat = dc;
      vsym.viewGroup = vg;
      HPGL->SetVP(&vp_plib);
      HPGL->SetTargetSG(&vsym);
      wxPoint r0(0, 0);
      wxPoint pivot(prule->pos.symb.pivot_x.SYCL,
                    prule->pos.symb.pivot_y.SYRW);
      wxPoint origin(prule->pos.symb.bnbox_x.SBXC,
                     prule->pos.symb.bnbox_y.SBXR);
      // Bake the symbol rotation into the captured ops (SG symbols are screen
      // billboards, so the angle is fixed, not viewport-relative).
      HPGL->Render(prule->vector.SVCT, prule->colRef.SCRF, r0, pivot, origin,
                   1.0f, angle, true);
      if (!vsym.ops.isEmpty()) out.vectorSymbols.push_back(std::move(vsym));
    }
  };

  // Sector-light arcs (CARC / RUL_ARC_2C): a coloured arc at a screen-fixed
  // radius around the light + radial sector-leg lines. wx draws these with a
  // dedicated ring shader (RenderCARC); here we emit them as VectorSymbol
  // billboard geometry (screen-fixed, world-anchored), like the HPGL symbols.
  // INSTstr = outline_col,outline_w,arc_col,arc_w,sectr1,sectr2,radius_mm,
  // sector_radius_mm (',' or ';' separated).
  auto emitCARC = [&](Rules *r) {
    if (!r->INSTstr) return;
    constexpr double kPi = 3.14159265358979323846;
    QString inst = QString::fromLatin1((const char *)r->INSTstr);
    inst.replace(';', ',');
    const QStringList tk = inst.split(',');
    if (tk.size() < 8) return;
    const float ppmm = GetPPMM();
    const double sectr1 = tk[4].toDouble();
    const double sectr2 = tk[5].toDouble();
    const double rad = tk[6].toDouble() * ppmm;
    const double leg = tk[7].toDouble() * ppmm;
    if (rad <= 0.0) return;
    // Coloured band ~1 mm (wx clamps to 1 mm on large displays); a thin black
    // edge each side -- wx's outline only extends outline_width/2 (px) beyond
    // the colour, NOT outline_width * px/mm (that swamped the band in black).
    double arcw = tk[3].toDouble() * ppmm;
    if (arcw > ppmm) arcw = ppmm;
    if (arcw < 1.0) arcw = 1.0;
    const double edge = std::max(1.0, tk[1].toDouble() * 0.5);

    QByteArray acol = tk[2].trimmed().toLatin1();
    S52color *c = getColor(acol.data());
    const QColor arcColor = c ? QColor(c->R, c->G, c->B) : QColor(0, 0, 0);

    s52sg::VectorSymbol vsym;
    vsym.pos = QPointF(anchor_lon, anchor_lat);
    vsym.scamin = scamin;
    vsym.dispCat = dc;
    vsym.viewGroup = s52sg::VgLights;

    // Bearing is S-52 "from seaward", clockwise from north. Screen angle =
    // bearing - 90 with the billboard's y-down: north -> up, east -> right.
    // Wrap when the lit sector crosses north (se <= sb).
    double sb = sectr1, se = sectr2;
    if (se <= sb) se += 360.0;

    // Filled annulus band [inner,outer] over [sb,se] as a triangle list.
    auto band = [&](double inner, double outer, const QColor &col) {
      s52sg::VectorOp op;
      op.filled = true;
      op.color = col;
      bool have = false;
      QPointF pin0, pout0;
      for (double ang = sb; ang < se + 0.01; ang += 6.0) {
        const double a = (std::min(ang, se) - 90.0) * kPi / 180.0;
        const double cs = std::cos(a), sn = std::sin(a);
        const QPointF pin(inner * cs, inner * sn), pout(outer * cs, outer * sn);
        if (have) {
          op.verts << pin0 << pout0 << pout;  // quad -> 2 triangles
          op.verts << pin0 << pout << pin;
        }
        pin0 = pin;
        pout0 = pout;
        have = true;
      }
      if (op.verts.size() >= 3) vsym.ops.push_back(op);
    };
    const double half = arcw / 2.0;
    // Coloured band, plus a black edge band on each radial side (drawn as
    // separate non-overlapping bands so draw order never matters -- the colour
    // is never covered, and white sectors stay visible against the sea).
    band(rad - half, rad + half, arcColor);
    band(rad - half - edge, rad - half, QColor(0, 0, 0));
    band(rad + half, rad + half + edge, QColor(0, 0, 0));

    // Dashed black sector legs (wx: ~1.2 mm dash / 0.6 mm gap).
    if (leg > 0.0) {
      const double on = 1.2 * ppmm, period = on + 0.6 * ppmm;
      for (const double b : {sectr1, sectr2}) {
        const double a = (b - 90.0) * kPi / 180.0;
        const double cs = std::cos(a), sn = std::sin(a);
        s52sg::VectorOp legOp;
        legOp.filled = false;
        legOp.color = QColor(0, 0, 0);
        for (double d = 0.0; d < leg; d += period) {
          const double d2 = std::min(d + on, leg);
          legOp.verts << QPointF(d * cs, d * sn) << QPointF(d2 * cs, d2 * sn);
        }
        if (legOp.verts.size() >= 2) vsym.ops.push_back(legOp);
      }
    }
    if (!vsym.ops.isEmpty()) out.vectorSymbols.push_back(std::move(vsym));
  };

  Rules *rules = rzRules->LUP->ruleList;
  while (rules != NULL) {
    if (rules->ruleType == RUL_SYM_PT) {
      emitSY(rules);
    } else if (rules->ruleType == RUL_ARC_2C) {
      emitCARC(rules);
    } else if (rules->ruleType == RUL_CND_SY) {
      if (!rzRules->obj->bCS_Added) {
        rzRules->obj->CSrules = NULL;
        GetAndAddCSRules(rzRules, rules);
        rzRules->obj->bCS_Added = 1;
      }
      Rules *rules_last = rules;
      Rules *cs = rzRules->obj->CSrules;
      while (NULL != cs) {
        if (cs->ruleType == RUL_SYM_PT) emitSY(cs);
        else if (cs->ruleType == RUL_ARC_2C) emitCARC(cs);
        rules_last = cs;
        cs = cs->next;
      }
      rules = rules_last;
    }
    rules = rules->next;
  }
  return 1;
}

// Walk a line object's rule list, mirroring DoRenderObject's LS/CS
// dispatch: LS rules emit line strips; conditional symbology (e.g.
// DEPCNT depth-contour colour) is expanded first. Complex-line (LC) rules
// are HPGL symbol-along-line patterns we can't emit without the vector
// path -- they fall back to a plain line in the LC's resolved colour.
int s52plib::RenderLineToSG(s52sg::Buffer &out, ObjRazRules *rzRules,
                            const QList<QPointF> &pts) {
  if (!rzRules || !rzRules->LUP) return 0;
  const int dc = dispRank(rzRules->LUP->DISC);
  const int prio = rzRules->LUP->DPRI - '0';

  const int scamin = rzRules->obj ? rzRules->obj->Scamin : 100000002;
  const int vg =
      rzRules->obj ? viewGroupFor(rzRules->obj->FeatureName) : s52sg::VgOther;

  // LC: an HPGL line-symbol repeated along the line (cables, pipelines,
  // restricted-area borders, recommended tracks). Capture the symbol's glyph
  // geometry once (local px, pivot at origin) + its repeat length, and hand it
  // to the consumer as a ComplexLine to walk along the path (wx draw_lc_poly).
  // If the symbol has no HPGL vector, fall back to a dashed line.
  auto emitLC = [&](Rules *r) {
    if (pts.size() < 2 || !r->razRule) return;
    Rule *pr = r->razRule;
    S52color *c = pr->colRef.LCRF ? getColor(pr->colRef.LCRF + 1) : nullptr;
    const QColor col = c ? QColor(c->R, c->G, c->B) : QColor(0, 0, 0);
    const int isym = pr->pos.line.bnbox_w.SYHL;       // repeat length, 0.01 mm
    const float lengthPx = isym > 0 ? isym * GetPPMM() / 100.0f : 0.0f;
    if (pr->vector.LVCT && lengthPx >= 1.0f) {
      extern float g_scaminScale;
      g_scaminScale = 1.0f;
      s52sg::VectorSymbol vsym;  // reuse the HPGL->SG capture
      HPGL->SetVP(&vp_plib);
      HPGL->SetTargetSG(&vsym);
      wxPoint r0(0, 0);
      wxPoint pivot(pr->pos.line.pivot_x.LICL, pr->pos.line.pivot_y.LIRW);
      HPGL->Render(pr->vector.LVCT, pr->colRef.LCRF, r0, pivot, pivot, 1.0f,
                   0.0f, true);
      if (!vsym.ops.isEmpty()) {
        s52sg::ComplexLine cl;
        cl.path = pts;
        cl.symbol = std::move(vsym.ops);
        cl.lengthPx = lengthPx;
        cl.color = col;
        cl.dispCat = dc;
        cl.priority = prio;
        cl.scamin = scamin;
        cl.viewGroup = vg;
        out.complexLines.push_back(std::move(cl));
        return;
      }
    }
    // Fallback: dashed line in the LC colour.
    s52sg::Prim prim;
    prim.type = s52sg::PrimType::LineStrip;
    prim.color = col;
    prim.width = 1.0f;
    prim.dashOnMm = 1.8f;
    prim.dashOffMm = 1.2f;
    prim.verts = pts;
    prim.dispCat = dc;
    prim.priority = prio;
    prim.scamin = m_bUseSCAMIN ? scamin : 100000002;  // P2.14
    out.prims.push_back(std::move(prim));
  };

  // SCAMIN for LS lines (P2.14): pass a real value only while Use-SCAMIN is on;
  // the unset sentinel keeps an un-SCAMIN'd line always visible (the consumer
  // SCAMIN-culls a Prim only when it carries a real value).
  const int ls_scamin = m_bUseSCAMIN ? scamin : 100000002;
  auto handle = [&](Rules *r) {
    if (r->ruleType == RUL_SIM_LN)
      RenderToSGLS(out, r, pts, dc, prio, ls_scamin);
    else if (r->ruleType == RUL_COM_LN)
      emitLC(r);
  };

  Rules *rules = rzRules->LUP->ruleList;
  while (rules != NULL) {
    if (rules->ruleType == RUL_CND_SY) {
      if (!rzRules->obj->bCS_Added) {
        rzRules->obj->CSrules = NULL;
        GetAndAddCSRules(rzRules, rules);
        rzRules->obj->bCS_Added = 1;
      }
      Rules *rules_last = rules;
      Rules *cs = rzRules->obj->CSrules;
      while (NULL != cs) {
        handle(cs);
        rules_last = cs;
        cs = cs->next;
      }
      rules = rules_last;
    } else {
      handle(rules);
    }
    rules = rules->next;
  }
  return 1;
}

// Tessellate the object's polygon to an independent triangle list in
// lon/lat (fans/strips expanded). Used by the AP pattern-fill emit.
static QList<QPointF> tessLonLatTriangles(PolyTessGeo *ppg_geo) {
  QList<QPointF> out;
  if (!ppg_geo) return out;
  if (!ppg_geo->IsOk()) ppg_geo->BuildDeferredTess();
  double ref_lat = 0, ref_lon = 0;
  ppg_geo->GetChartRefPos(&ref_lat, &ref_lon);
  PolyTriGroup *ppg = ppg_geo->Get_PolyTriGroup_head();
  if (!ppg) return out;
  const bool is_double = (ppg->data_type == DATA_TYPE_DOUBLE);

  for (TriPrim *p_tp = ppg->tri_prim_head; p_tp; p_tp = p_tp->p_next) {
    QList<QPointF> v;
    v.reserve(p_tp->nVert);
    const float *pf = reinterpret_cast<const float *>(p_tp->p_vertex);
    const double *pd = reinterpret_cast<const double *>(p_tp->p_vertex);
    for (int i = 0; i < p_tp->nVert; ++i) {
      double east = is_double ? pd[2 * i] : pf[2 * i];
      double north = is_double ? pd[2 * i + 1] : pf[2 * i + 1];
      double lat, lon;
      fromSM_plib(east, north, ref_lat, ref_lon, &lat, &lon);
      v.append(QPointF(lon, lat));
    }
    if (p_tp->type == PTG_TRIANGLE_FAN) {
      for (qsizetype i = 1; i + 1 < v.size(); ++i) out << v[0] << v[i] << v[i + 1];
    } else if (p_tp->type == PTG_TRIANGLE_STRIP) {
      for (qsizetype i = 0; i + 2 < v.size(); ++i) {
        if (i & 1)
          out << v[i + 1] << v[i] << v[i + 2];
        else
          out << v[i] << v[i + 1] << v[i + 2];
      }
    } else {  // PTG_TRIANGLES
      out += v;
    }
  }
  return out;
}

int s52plib::RenderToSGAP(s52sg::Buffer &out, ObjRazRules *rzRules,
                          Rules *rules) {
  Rule *prule = rules->razRule;
  if (!prule || prule->definition.PADF != 'R') return 0;  // raster patterns
  QImage qpat = cachedAtlasImage(m_chartSymbols, prule->name.PANM);
  if (qpat.isNull()) return 0;
  if (!rzRules->obj->pPolyTessGeo) return 0;

  s52sg::PatternFill pf;
  pf.tris = tessLonLatTriangles(rzRules->obj->pPolyTessGeo);
  if (pf.tris.isEmpty()) return 0;
  pf.pattern = qpat;
  pf.dispCat = dispRank(rzRules->LUP->DISC);
  // SCAMIN (P2.14): real value only while Use-SCAMIN is on; the consumer
  // SCAMIN-culls a pattern fill only when it carries a real value.
  pf.scamin = (m_bUseSCAMIN && rzRules->obj) ? rzRules->obj->Scamin : 100000002;
  out.patternFills.push_back(std::move(pf));
  return 1;
}

// Walk an area object's rule list, mirroring RenderAreaToGL: AC rules emit
// solid fills, AP rules emit tiled pattern fills; conditional-symbology
// rules are expanded first. Scale/category culling is applied by the
// consumer.
int s52plib::RenderAreaToSG(s52sg::Buffer &out, ObjRazRules *rzRules) {
  if (!rzRules || !rzRules->LUP) return 0;

  auto handle = [&](Rules *r) {
    if (r->ruleType == RUL_ARE_CO)
      RenderToSGAC(out, rzRules, r);
    else if (r->ruleType == RUL_ARE_PA)
      RenderToSGAP(out, rzRules, r);
  };

  Rules *rules = rzRules->LUP->ruleList;
  while (rules != NULL) {
    if (rules->ruleType == RUL_CND_SY) {
      if (!rzRules->obj->bCS_Added) {
        rzRules->obj->CSrules = NULL;
        GetAndAddCSRules(rzRules, rules);
        rzRules->obj->bCS_Added = 1;
      }
      Rules *rules_last = rules;
      Rules *cs = rzRules->obj->CSrules;
      while (NULL != cs) {
        handle(cs);
        rules_last = cs;
        cs = cs->next;
      }
      rules = rules_last;
    } else {
      handle(rules);
    }
    rules = rules->next;
  }
  return 1;
}
