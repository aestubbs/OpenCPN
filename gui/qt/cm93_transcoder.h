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

#include "cm93_cell_reader.h"
#include "cm93_dictionary.h"

class Extended_Geometry;  // libs/s52plib/src/mygeom.h

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

class Cm93Transcoder {
public:
  Cm93Transcoder(const Cm93CellBlock *cib, const Cm93Dictionary *dict)
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

  const Cm93Dictionary *dict() const { return m_dict; }

private:
  const Cm93CellBlock *m_cib;
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
