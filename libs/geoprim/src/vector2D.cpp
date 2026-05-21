/******************************************************************************
 *
 * Project:  OpenCPN
 *
 ***************************************************************************
 *   Copyright (C) 2013 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 */

// 2D vector primitives used by hit-test, line-clip, and LOD-reduction.
//
// Historically these lived in model/src/navutil_base.cpp, which created a
// layering inversion: libs/geoprim called the symbols (from LOD_reduce.cpp)
// while model depended on geoprim, so geoprim's only path to its own helpers
// was through whichever executable happened to link model. Moving the
// definitions here makes libs/geoprim self-contained, which is what
// libs/s52plib (and the new opencpn-qt) need to link cleanly.

#include <cmath>
#include <cstddef>

#include "vector2D.h"

extern "C" double vGetLengthOfNormal(pVector2D a, pVector2D b, pVector2D n) {
  vector2D c, vNormal;
  vNormal.x = 0;
  vNormal.y = 0;
  // c = ((a * b) / (|b|^2)) * b  -- projection of a onto b.
  c.x = b->x * (vDotProduct(a, b) / vDotProduct(b, b));
  c.y = b->y * (vDotProduct(a, b) / vDotProduct(b, b));
  // Perpendicular projection: e = a - c.
  vSubtractVectors(a, &c, &vNormal);
  *n = vNormal;
  return vVectorMagnitude(&vNormal);
}

extern "C" double vDotProduct(pVector2D v0, pVector2D v1) {
  return (v0 == NULL || v1 == NULL) ? 0.0 : (v0->x * v1->x) + (v0->y * v1->y);
}

extern "C" pVector2D vAddVectors(pVector2D v0, pVector2D v1, pVector2D v) {
  if (v0 == NULL || v1 == NULL) {
    v = (pVector2D)NULL;
  } else {
    v->x = v0->x + v1->x;
    v->y = v0->y + v1->y;
  }
  return v;
}

extern "C" pVector2D vSubtractVectors(pVector2D v0, pVector2D v1, pVector2D v) {
  if (v0 == NULL || v1 == NULL) {
    v = (pVector2D)NULL;
  } else {
    v->x = v0->x - v1->x;
    v->y = v0->y - v1->y;
  }
  return v;
}

extern "C" double vVectorSquared(pVector2D v0) {
  if (v0 == NULL) return 0.0;
  return (v0->x * v0->x) + (v0->y * v0->y);
}

extern "C" double vVectorMagnitude(pVector2D v0) {
  if (v0 == NULL) return 0.0;
  return std::sqrt(vVectorSquared(v0));
}
