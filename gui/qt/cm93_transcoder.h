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
 * Cm93Transcoder (P2.19 step (a), part 2): CM93 decoded tables ->
 * S-52-renderable geometry. buildGeom() reconstructs an Extended_Geometry
 * (areas as rings, lines as joined segments, points, 3-D sounding
 * clusters) from a Cm93Object's vector records -- a verbatim extraction of
 * cm93chart::BuildGeom (gui/src/cm93.cpp:2526-2894) with the chart-member
 * scratch state moved onto this class. transformPoint() maps a cell-local
 * 16-bit point to lat/lon through the cell's Mercator transform (+ WGS84
 * offsets). Cm93AttrBlock walks an object's packed attribute block using
 * the dictionary's value types. The CreateS57Obj attribute/class
 * transcoding lands on top of this next.
 */

#ifndef OCPN_QT_CM93_TRANSCODER_H_
#define OCPN_QT_CM93_TRANSCODER_H_

#include <QDateTime>
#include <QList>
#include <QPolygonF>

#include "cm93_cell_reader.h"
#include "cm93_dictionary.h"

class Extended_Geometry;  // libs/s52plib/src/mygeom.h
class S57Obj;             // libs/s52plib/src/s52s57.h
struct _S57attVal;
typedef _S57attVal S57attVal;  // matches the s52s57.h typedef
class wxString;

namespace ocpn::qtui {

/** Walk a CM93 packed attribute block: each GetNextAttr() returns the
 *  current attribute record pointer (first byte = attribute number) and
 *  advances by the dictionary-driven value size. */
class Cm93AttrBlock {
public:
  Cm93AttrBlock(const void *block, const Cm93Dictionary *pdict);
  unsigned char *GetNextAttr();

  int m_cptr;
  unsigned char *m_block;
  const Cm93Dictionary *m_pDict;
};

/** One M_COVR coverage record captured during transcoding: the exterior
 *  ring (lon, lat), ids, publication year and WGS84 transform offsets.
 *  User offsets stay 0 until the CM93 offset dialog is ported. */
struct Cm93Covr {
  int cell_index = 0;
  int object_id = 0;
  int subcell = 0;
  int pub_year = 0;
  double wgsox = 0, wgsoy = 0;
  double user_xoff = 0, user_yoff = 0;
  double lat_min = 1000, lat_max = -1000, lon_min = 1000, lon_max = -1000;
  QPolygonF ring;  // (lon, lat)
};

class Cm93Transcoder {
public:
  Cm93Transcoder(Cm93CellBlock *cib, const Cm93Dictionary *dict)
      : m_cib(cib), m_dict(dict) {
    m_ncontour_alloc = 100;
    m_pcontour_array = static_cast<int *>(malloc(m_ncontour_alloc *
                                                 sizeof(int)));
  }
  ~Cm93Transcoder() { free(m_pcontour_array); }

  /** Reconstruct the object's geometry in cell-local 16-bit coordinates
   *  (callers map via transformPoint / the cell transform). Caller owns
   *  the returned Extended_Geometry. */
  Extended_Geometry *buildGeom(Cm93Object *pobject, int iobject);

  /** Cell-local point -> geographic lat/lon (Mercator inverse + offsets). */
  void transformPoint(cm93_point *s, double trans_x, double trans_y,
                      double *lat, double *lon);

  /** The semantic core (verbatim cm93chart::CreateS57Obj): one decoded
   *  Cm93Object + its built geometry -> a renderable S57Obj (class/attribute
   *  transcoding, ATON label fixups, WGS84 offsets, per-object transform
   *  coefficients, deferred tessellation for areas). Caller owns the
   *  returned object; xgeom ownership transfers (areas keep it, others are
   *  freed). NULL when the class is unknown to the dictionary. */
  S57Obj *createS57Obj(int cell_index, int iobject, int subcell,
                       Cm93Object *pobject, Extended_Geometry *xgeom,
                       double view_scale_ppm);

  /** Coverage records captured from this cell's M_COVR objects. */
  const QList<Cm93Covr> &coverage() const { return m_covrs; }
  /** Cell edition date captured from M_COVR RECDAT/_dgdat. */
  QDateTime editionDate() const { return m_ed_date; }

  const Cm93Dictionary *dict() const { return m_dict; }

private:
  void translateColmar(const wxString &sclass, S57attVal *pattValTmp);
  const Cm93Covr *findCovrAt(double lat, double lon) const;

  QList<Cm93Covr> m_covrs;
  QDateTime m_ed_date;
  Cm93CellBlock *m_cib;  // non-const: offset bookkeeping writes
  const Cm93Dictionary *m_dict;
  // Scratch reused across buildGeom calls (was cm93chart member state).
  int *m_pcontour_array = nullptr;
  int m_ncontour_alloc = 0;
  // Edge-index bias for update cells stacked over the base (wx
  // m_current_cell_vearray_offset); 0 until update-cell merging is ported.
  int m_current_cell_vearray_offset = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CM93_TRANSCODER_H_
