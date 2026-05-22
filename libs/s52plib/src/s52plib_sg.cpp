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

#include "bbox.h"  // LLBBox, required transitively by mygeom.h
#include "mygeom.h"
#include "s52plib.h"
#include "s52s57.h"

// SM -> lon/lat inverse projection (declared in s52plib.h).
extern void fromSM_plib(double x, double y, double lat0, double lon0,
                        double *lat, double *lon);

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

  for (TriPrim *p_tp = ppg->tri_prim_head; p_tp; p_tp = p_tp->p_next) {
    s52sg::Prim prim;
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
                          const QList<QPointF> &pts) {
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
  out.prims.push_back(std::move(prim));
  return 1;
}

// Walk a line object's rule list, mirroring DoRenderObject's LS/CS
// dispatch: LS rules emit line strips; conditional symbology (e.g.
// DEPCNT depth-contour colour) is expanded first. Complex-line (LC)
// symbol patterns are deferred -- they fall back to nothing for now.
int s52plib::RenderLineToSG(s52sg::Buffer &out, ObjRazRules *rzRules,
                            const QList<QPointF> &pts) {
  if (!rzRules || !rzRules->LUP) return 0;

  Rules *rules = rzRules->LUP->ruleList;
  while (rules != NULL) {
    switch (rules->ruleType) {
      case RUL_SIM_LN:
        RenderToSGLS(out, rules, pts);
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
          if (rules->ruleType == RUL_SIM_LN) RenderToSGLS(out, rules, pts);
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
