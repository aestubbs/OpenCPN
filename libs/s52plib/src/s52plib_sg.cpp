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

#include "model/wx_qt_ui_types.h"  // WxImageToQImage

#include "bbox.h"  // LLBBox, required transitively by mygeom.h
#include "chartsymbols.h"  // ChartSymbols::GetImage (raster symbol atlas)
#include "mygeom.h"
#include "s52plib.h"
#include "s52s57.h"

// SM -> lon/lat inverse projection (declared in s52plib.h).
extern void fromSM_plib(double x, double y, double lat0, double lon0,
                        double *lat, double *lon);

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

  for (TriPrim *p_tp = ppg->tri_prim_head; p_tp; p_tp = p_tp->p_next) {
    s52sg::Prim prim;
    prim.dispCat = dc;
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
                          const QList<QPointF> &pts, int dispCat) {
  if (pts.size() < 2 || !rules->INSTstr) return 0;
  char *str = (char *)rules->INSTstr;
  S52color *c = getColor(str + 7);
  if (!c) return 0;

  s52sg::Prim prim;
  prim.type = s52sg::PrimType::LineStrip;
  prim.color = QColor(c->R, c->G, c->B);
  prim.width = static_cast<float>(atoi(str + 5));
  if (prim.width < 1.0f) prim.width = 1.0f;
  prim.verts = pts;
  prim.dispCat = dispCat;
  out.prims.push_back(std::move(prim));
  return 1;
}

// Resolve a parsed S52_TextC into a Label and append it. The string,
// colour and nominal point size come from s52plib's text parse; the
// consumer renders it with a system font (not TexFont/DepthFont).
static void EmitTextC(s52sg::Buffer &out, S52_TextC *text, double anchor_lon,
                      double anchor_lat, int scamin, int dispCat) {
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
  out.labels.push_back(std::move(label));
}

int s52plib::RenderTextToSG(s52sg::Buffer &out, ObjRazRules *rzRules,
                            double anchor_lon, double anchor_lat) {
  if (!rzRules || !rzRules->LUP) return 0;

  const int scamin = rzRules->obj ? rzRules->obj->Scamin : 100000002;
  const int dc = dispRank(rzRules->LUP->DISC);
  auto handle = [&](Rules *rules) {
    if (rules->ruleType == RUL_TXT_TX) {
      S52_TextC *t = S52_PL_parseTX(rzRules, rules, (char *)rules->INSTstr);
      EmitTextC(out, t, anchor_lon, anchor_lat, scamin, dc);
      delete t;
    } else if (rules->ruleType == RUL_TXT_TE) {
      S52_TextC *t = S52_PL_parseTE(rzRules, rules, (char *)rules->INSTstr);
      EmitTextC(out, t, anchor_lon, anchor_lat, scamin, dc);
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

  auto emitSY = [&](Rules *rules) {
    Rule *prule = rules->razRule;
    if (!prule) return;
    if (prule->definition.SYDF != 'R') return;  // raster symbols only for now
    wxImage img = m_chartSymbols.GetImage(prule->name.SYNM);
    if (!img.IsOk()) return;
    s52sg::Symbol sym;
    sym.pos = QPointF(anchor_lon, anchor_lat);
    sym.image = WxImageToQImage(img);
    sym.pivot = QPointF(prule->pos.symb.pivot_x.SYCL,
                        prule->pos.symb.pivot_y.SYRW);
    sym.scamin = scamin;
    sym.dispCat = dc;
    out.symbols.push_back(std::move(sym));
  };

  Rules *rules = rzRules->LUP->ruleList;
  while (rules != NULL) {
    if (rules->ruleType == RUL_SYM_PT) {
      emitSY(rules);
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
// DEPCNT depth-contour colour) is expanded first. Complex-line (LC)
// symbol patterns are deferred -- they fall back to nothing for now.
int s52plib::RenderLineToSG(s52sg::Buffer &out, ObjRazRules *rzRules,
                            const QList<QPointF> &pts) {
  if (!rzRules || !rzRules->LUP) return 0;
  const int dc = dispRank(rzRules->LUP->DISC);

  Rules *rules = rzRules->LUP->ruleList;
  while (rules != NULL) {
    switch (rules->ruleType) {
      case RUL_SIM_LN:
        RenderToSGLS(out, rules, pts, dc);
        break;

      case RUL_CND_SY: {
        if (!rzRules->obj->bCS_Added) {
          rzRules->obj->CSrules = NULL;
          GetAndAddCSRules(rzRules, rules);
          rzRules->obj->bCS_Added = 1;
        }
        Rules *rules_last = rules;
        rules = rzRules->obj->CSrules;
        while (NULL != rules) {
          if (rules->ruleType == RUL_SIM_LN) RenderToSGLS(out, rules, pts, dc);
          rules_last = rules;
          rules = rules->next;
        }
        rules = rules_last;
        break;
      }

      case RUL_NONE:
      default:
        break;
    }
    rules = rules->next;
  }
  return 1;
}

// Walk an area object's rule list, mirroring RenderAreaToGL: AC rules emit
// fills; conditional-symbology rules are expanded first. Pattern fills
// (AP) and the visibility/scale culling that the GL path performs are
// deferred -- synthetic demo objects are always in view.
int s52plib::RenderAreaToSG(s52sg::Buffer &out, ObjRazRules *rzRules) {
  if (!rzRules || !rzRules->LUP) return 0;

  Rules *rules = rzRules->LUP->ruleList;
  while (rules != NULL) {
    switch (rules->ruleType) {
      case RUL_ARE_CO:
        RenderToSGAC(out, rzRules, rules);
        break;

      case RUL_CND_SY: {
        if (!rzRules->obj->bCS_Added) {
          rzRules->obj->CSrules = NULL;
          GetAndAddCSRules(rzRules, rules);
          rzRules->obj->bCS_Added = 1;
        }
        Rules *rules_last = rules;
        rules = rzRules->obj->CSrules;
        while (NULL != rules) {
          if (rules->ruleType == RUL_ARE_CO) RenderToSGAC(out, rzRules, rules);
          rules_last = rules;
          rules = rules->next;
        }
        rules = rules_last;
        break;
      }

      case RUL_NONE:
      default:
        break;
    }
    rules = rules->next;
  }
  return 1;
}
