/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

// Verbatim extraction of cm93chart::BuildGeom / ::Transform and
// cm93_attr_block from gui/src/cm93.cpp (2526-2989); see the header for
// the adaptation rules. Keep edits mechanical so the copies stay diffable
// until the wx file retires with P3.11.

#include "cm93_transcoder.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <wx/wx.h>  // wxMax/wxMin/wxPrintf (verbatim body; wx links anyway)

#include "bbox.h"            // LLBBox (mygeom.h dependency)
#include "mygeom.h"          // Extended_Geometry (libs/s52plib)
#include "model/georef.h"    // DEGREE
#include "ogr_geometry.h"    // OGRMultiPoint (3-D sounding clusters)

namespace ocpn::qtui {

static const double CM93_semimajor_axis_meters = 6378388.0;  // CM93 datum
#ifndef PI
#define PI 3.1415926535897931160E0
#endif

Extended_Geometry *Cm93Transcoder::buildGeom(Cm93Object *pobject, int iobject)

{
  wxString s;
  int geomtype;

  int geom_type_maybe = pobject->geotype;

  switch (geom_type_maybe) {
    case 1:
      geomtype = 1;
      break;
    case 2:
      geomtype = 2;
      break;
    case 4:
      geomtype = 3;
      break;
    case 129:
      geomtype = 1;
      break;
    case 130:
      geomtype = 2;
      break;
    case 132:
      geomtype = 3;
      break;
    case 8:
      geomtype = 8;
      break;
    case 16:
      geomtype = 16;
      break;
    case 161:
      geomtype = 1;
      break;  // lighthouse first child
    case 33:
      geomtype = 1;
      break;
    default:
      geomtype = -1;
      break;
  }

  int iseg;

  Extended_Geometry *ret_ptr = new Extended_Geometry;

  int lon_max, lat_max, lon_min, lat_min;
  lon_max = 0;
  lon_min = 65536;
  lat_max = 0;
  lat_min = 65536;

  switch (geomtype) {
    case 3:  // Areas
    {
      vector_record_descriptor *psegs =
          (vector_record_descriptor *)pobject->pGeometry;

      int nsegs = pobject->n_geom_elements;

      ret_ptr->n_vector_indices = nsegs;
      ret_ptr->pvector_index = (int *)malloc(nsegs * 3 * sizeof(int));

      // Traverse the object once to get a maximum polygon vertex count
      int n_maxvertex = 0;
      for (int i = 0; i < nsegs; i++) {
        geometry_descriptor *pgd =
            (geometry_descriptor *)(psegs[i].pGeom_Description);
        n_maxvertex += pgd->n_points;
      }

      // TODO  May not need this fluff adder....
      n_maxvertex += 1;  // fluff

      wxPoint2DDouble *pPoints =
          (wxPoint2DDouble *)calloc((n_maxvertex) * sizeof(wxPoint2DDouble), 1);

      int ip = 1;
      int n_prev_vertex_index = 1;
      bool bnew_ring = true;
      int ncontours = 0;
      iseg = 0;

      cm93_point start_point;
      start_point.x = 0;
      start_point.y = 0;

      cm93_point cur_end_point;
      cur_end_point.x = 1;
      cur_end_point.y = 1;

      int n_max_points = -1;
      while (iseg < nsegs) {
        int type_seg = psegs[iseg].segment_usage;

        geometry_descriptor *pgd =
            (geometry_descriptor *)(psegs[iseg].pGeom_Description);

        int npoints = pgd->n_points;
        cm93_point *rseg = pgd->p_points;

        n_max_points = wxMax(n_max_points, npoints);

        //    Establish ring starting conditions
        if (bnew_ring) {
          bnew_ring = false;

          if ((type_seg & 4) == 0)
            start_point = rseg[0];
          else
            start_point = rseg[npoints - 1];
        }

        if (((type_seg & 4) == 0)) {
          cur_end_point = rseg[npoints - 1];
          for (int j = 0; j < npoints; j++) {
            //                                    if(ncontours == 0) // outer
            //                                    ring describes envelope
            {
              lon_max = wxMax(lon_max, rseg[j].x);
              lon_min = wxMin(lon_min, rseg[j].x);
              lat_max = wxMax(lat_max, rseg[j].y);
              lat_min = wxMin(lat_min, rseg[j].y);
            }

            pPoints[ip].m_x = rseg[j].x;
            pPoints[ip].m_y = rseg[j].y;
            ip++;
          }
        } else if ((type_seg & 4) == 4)  // backwards
        {
          cur_end_point = rseg[0];
          for (int j = npoints - 1; j >= 0; j--) {
            //                                    if(ncontours == 0) // outer
            //                                    ring describes envelope
            {
              lon_max = wxMax(lon_max, rseg[j].x);
              lon_min = wxMin(lon_min, rseg[j].x);
              lat_max = wxMax(lat_max, rseg[j].y);
              lat_min = wxMin(lat_min, rseg[j].y);
            }

            pPoints[ip].m_x = rseg[j].x;
            pPoints[ip].m_y = rseg[j].y;
            ip++;
          }
        }

        ip--;  // skip the last point in each segment

        ret_ptr->pvector_index[iseg * 3 + 0] =
            0;  //-1;                 // first connected node
        ret_ptr->pvector_index[iseg * 3 + 1] =
            pgd->index + m_current_cell_vearray_offset;  // edge index
        ret_ptr->pvector_index[iseg * 3 + 2] =
            0;  //-2;                 // last connected node

        if ((cur_end_point.x == start_point.x) &&
            (cur_end_point.y == start_point.y)) {
          // done with a ring

          ip++;  // leave in ring closure point

          int nRingVertex = ip - n_prev_vertex_index;

          //    possibly increase contour array size
          if (ncontours > m_ncontour_alloc - 1) {
            m_ncontour_alloc *= 2;
            int *tmp = m_pcontour_array;
            m_pcontour_array = (int *)realloc(m_pcontour_array,
                                              m_ncontour_alloc * sizeof(int));
            if (NULL == tmp) {
              free(tmp);
              tmp = NULL;
            }
          }
          m_pcontour_array[ncontours] = nRingVertex;  // store the vertex count

          bnew_ring = true;  // set for next ring
          n_prev_vertex_index = ip;
          ncontours++;
        }
        iseg++;
      }  // while iseg

      ret_ptr->n_max_edge_points = n_max_points;

      ret_ptr->n_contours =
          ncontours;  // parameters passed to trapezoid tesselator

      if (0 == ncontours) ncontours = 1;  // avoid 0 alloc
      ret_ptr->contour_array = (int *)malloc(ncontours * sizeof(int));
      memcpy(ret_ptr->contour_array, m_pcontour_array, ncontours * sizeof(int));

      ret_ptr->vertex_array = pPoints;
      ret_ptr->n_max_vertex = n_maxvertex;

      ret_ptr->pogrGeom = NULL;

      ret_ptr->xmin = lon_min;
      ret_ptr->xmax = lon_max;
      ret_ptr->ymin = lat_min;
      ret_ptr->ymax = lat_max;

      break;
    }  // case 3

    case 1:  // single points
    {
      cm93_point *pt = (cm93_point *)pobject->pGeometry;
      ret_ptr->pogrGeom = NULL;  // t;

      ret_ptr->pointx = pt->x;
      ret_ptr->pointy = pt->y;
      break;
    }

    case 2:  // LINE geometry
    {
      vector_record_descriptor *psegs =
          (vector_record_descriptor *)pobject->pGeometry;

      int nsegs = pobject->n_geom_elements;

      ret_ptr->n_vector_indices = nsegs;
      ret_ptr->pvector_index = (int *)malloc(nsegs * 3 * sizeof(int));

      //    Calculate the number of points
      int n_maxvertex = 0;
      for (int imseg = 0; imseg < nsegs; imseg++) {
        geometry_descriptor *pgd =
            (geometry_descriptor *)psegs->pGeom_Description;

        n_maxvertex += pgd->n_points;
        psegs++;
      }

      wxPoint2DDouble *pPoints =
          (wxPoint2DDouble *)malloc(n_maxvertex * sizeof(wxPoint2DDouble));

      psegs = (vector_record_descriptor *)pobject->pGeometry;

      int ip = 0;
      int lon_max, lat_max, lon_min, lat_min;
      lon_max = 0;
      lon_min = 65536;
      lat_max = 0;
      lat_min = 65536;
      int n_max_points = -1;

      for (int iseg = 0; iseg < nsegs; iseg++) {
        int type_seg = psegs->segment_usage;

        geometry_descriptor *pgd =
            (geometry_descriptor *)psegs->pGeom_Description;

        psegs++;  // next segment

        int npoints = pgd->n_points;
        cm93_point *rseg = pgd->p_points;

        n_max_points = wxMax(n_max_points, npoints);

        if (((type_seg & 4) != 4)) {
          for (int j = 0; j < npoints; j++) {
            lon_max = wxMax(lon_max, rseg[j].x);
            lon_min = wxMin(lon_min, rseg[j].x);
            lat_max = wxMax(lat_max, rseg[j].y);
            lat_min = wxMin(lat_min, rseg[j].y);

            pPoints[ip].m_x = rseg[j].x;
            pPoints[ip].m_y = rseg[j].y;
            ip++;
          }
        }

        else if ((type_seg & 4) == 4)  // backwards
        {
          for (int j = npoints - 1; j >= 0; j--) {
            lon_max = wxMax(lon_max, rseg[j].x);
            lon_min = wxMin(lon_min, rseg[j].x);
            lat_max = wxMax(lat_max, rseg[j].y);
            lat_min = wxMin(lat_min, rseg[j].y);

            pPoints[ip].m_x = rseg[j].x;
            pPoints[ip].m_y = rseg[j].y;
            ip++;
          }
        }

        ret_ptr->pvector_index[iseg * 3 + 0] =
            0;  //-1;                 // first connected node
        ret_ptr->pvector_index[iseg * 3 + 1] =
            pgd->index + m_current_cell_vearray_offset;  // edge index
        ret_ptr->pvector_index[iseg * 3 + 2] =
            0;  //-2;                 // last connected node

      }  // for

      ret_ptr->n_max_edge_points = n_max_points;

      ret_ptr->vertex_array = pPoints;
      ret_ptr->n_max_vertex = n_maxvertex;

      ret_ptr->pogrGeom = NULL;

      ret_ptr->xmin = lon_min;
      ret_ptr->xmax = lon_max;
      ret_ptr->ymin = lat_min;
      ret_ptr->ymax = lat_max;

      break;
    }  // case 2  (lines)

    case 8: {
      geometry_descriptor *pgd = (geometry_descriptor *)pobject->pGeometry;

      int npoints = pgd->n_points;
      cm93_point_3d *rseg = (cm93_point_3d *)pgd->p_points;

      OGRMultiPoint *pSMP = new OGRMultiPoint;

      int z;
      double zp;
      for (int ip = 0; ip < npoints; ip++) {
        z = rseg[ip].z;

        //    This is a magic number if there ever was one.....
        if (z >= 12000)
          zp = double(z - 12000);
        else
          zp = z / 10.;

        OGRPoint *ppoint = new OGRPoint(rseg[ip].x, rseg[ip].y, zp);
        pSMP->addGeometryDirectly(ppoint);

        lon_max = wxMax(lon_max, rseg[ip].x);
        lon_min = wxMin(lon_min, rseg[ip].x);
        lat_max = wxMax(lat_max, rseg[ip].y);
        lat_min = wxMin(lat_min, rseg[ip].y);
      }

      ret_ptr->pogrGeom = pSMP;

      ret_ptr->xmin = lon_min;
      ret_ptr->xmax = lon_max;
      ret_ptr->ymin = lat_min;
      ret_ptr->ymax = lat_max;

      break;
    }

    case 16:
      break;  // this is the case of objects with children
              // the parent has no geometry.....

    default: {
      wxPrintf("Unexpected geomtype %d for Feature %d\n", geomtype, iobject);
      break;
    }

  }  // switch

  return ret_ptr;
}
void Cm93Transcoder::transformPoint(cm93_point *s, double trans_x, double trans_y,
                          double *lat, double *lon) {
  //    Simple linear transform
  double valx = (s->x * m_cib->transform_x_rate) + m_cib->transform_x_origin;
  double valy = (s->y * m_cib->transform_y_rate) + m_cib->transform_y_origin;

  //    Add in the WGS84 offset corrections
  valx -= trans_x;
  valy -= trans_y;

  //    Convert to lat/lon
  *lat =
      (2.0 * atan(exp(valy / CM93_semimajor_axis_meters)) - PI / 2.) / DEGREE;
  *lon = (valx / (DEGREE * CM93_semimajor_axis_meters));
}
Cm93AttrBlock::Cm93AttrBlock(const void *block, const Cm93Dictionary *pdict) {
  m_cptr = 0;
  m_block = (unsigned char *)block;
  m_pDict = pdict;
}

unsigned char *Cm93AttrBlock::GetNextAttr() {
  //    return current pointer
  unsigned char *ret_val = m_block + m_cptr;

  //    Advance the pointer

  unsigned char iattr = *(m_block + m_cptr);
  m_cptr++;

  //      char vtype = m_pDict->m_ValTypeArray[iattr];
  char vtype = m_pDict->attrValueType(iattr);

  switch (vtype) {
    case 'I':  // never seen?
      m_cptr += 2;
      break;
    case 'B':
      m_cptr += 1;
      //                  pb = (unsigned char *)aval;
      //                  sprintf(val, "%d", *pb);
      //                  pvtype = 'I';                 // override
      break;
    case 'S':
      while (*(m_block + m_cptr)) m_cptr++;
      m_cptr++;  // skip terminator
                 //                  sprintf(val, "%s", aval);
      break;
    case 'R':
      m_cptr += 4;
      //                  pf = (float *)aval;
      //                  sprintf(val, "%g", *pf);
      break;
    case 'W':
      m_cptr += 2;
      break;
    case 'G':
      m_cptr += 4;
      break;
    case 'C':
      m_cptr += 3;
      while (*(m_block + m_cptr)) m_cptr++;
      m_cptr++;  // skip terminator
                 //                  sprintf(val, "%s", &aval[3]);
                 //                  pvtype = 'S';                 // override
      break;
    case 'L': {
      unsigned char nl = *(m_block + m_cptr);
      m_cptr++;
      m_cptr += nl;

      //                  pb = (unsigned char *)aval;
      //                  unsigned char nl = *pb++;
      //                  char vi[20];
      //                  val[0] = 0;
      //                  for(int i=0 ; i<nl ; i++)
      //                  {
      //                        sprintf(vi, "%d,", *pb++);
      //                        strcat(val, vi);
      //                  }
      //                  if(strlen(val))
      //                        val[strlen(val)-1] = 0;         // strip last
      //                        ","
      //                  pvtype = 'S';                 // override
      break;
    }
    default:
      //                  sprintf(val, "Unknown Value Type");
      break;
  }

  return ret_val;
}

}  // namespace ocpn::qtui
