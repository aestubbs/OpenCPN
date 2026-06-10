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

#include <algorithm>  // std::max/min (de-wx'd from the verbatim body, P5.6)

#include <QDateTime>

#include "bbox.h"            // LLBBox (mygeom.h dependency)
#include "mygeom.h"          // Extended_Geometry, PolyTessGeo (libs/s52plib)
#include "s52s57.h"          // S57Obj, S57attVal, OGR_* value types
#include "model/georef.h"    // DEGREE, mercator_k0, WGS84 axis
#include "model/ocpn_types.h"    // CHART_TYPE_CM93 (auxParm3 tag)
#include "model/wx_qt_string.h"  // QString_to_wxString
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

        n_max_points = std::max<int>(n_max_points, npoints);

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
              lon_max = std::max<int>(lon_max, rseg[j].x);
              lon_min = std::min<int>(lon_min, rseg[j].x);
              lat_max = std::max<int>(lat_max, rseg[j].y);
              lat_min = std::min<int>(lat_min, rseg[j].y);
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
              lon_max = std::max<int>(lon_max, rseg[j].x);
              lon_min = std::min<int>(lon_min, rseg[j].x);
              lat_max = std::max<int>(lat_max, rseg[j].y);
              lat_min = std::min<int>(lat_min, rseg[j].y);
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

        n_max_points = std::max<int>(n_max_points, npoints);

        if (((type_seg & 4) != 4)) {
          for (int j = 0; j < npoints; j++) {
            lon_max = std::max<int>(lon_max, rseg[j].x);
            lon_min = std::min<int>(lon_min, rseg[j].x);
            lat_max = std::max<int>(lat_max, rseg[j].y);
            lat_min = std::min<int>(lat_min, rseg[j].y);

            pPoints[ip].m_x = rseg[j].x;
            pPoints[ip].m_y = rseg[j].y;
            ip++;
          }
        }

        else if ((type_seg & 4) == 4)  // backwards
        {
          for (int j = npoints - 1; j >= 0; j--) {
            lon_max = std::max<int>(lon_max, rseg[j].x);
            lon_min = std::min<int>(lon_min, rseg[j].x);
            lat_max = std::max<int>(lat_max, rseg[j].y);
            lat_min = std::min<int>(lat_min, rseg[j].y);

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

        lon_max = std::max<int>(lon_max, rseg[ip].x);
        lon_min = std::min<int>(lon_min, rseg[ip].x);
        lat_max = std::max<int>(lat_max, rseg[ip].y);
        lat_min = std::min<int>(lat_min, rseg[ip].y);
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
      qWarning("cm93: unexpected geomtype %d for feature %d", geomtype, iobject);
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

void Cm93Transcoder::translateColmar(const wxString &sclass,
                                 S57attVal *pattValTmp) {
  int *pcur_attr = (int *)pattValTmp->value;
  int cur_attr = *pcur_attr;

  wxString lstring;

  switch (cur_attr) {
    case 1:
      lstring = "4";
      break;  // green
    case 2:
      lstring = "2";
      break;  // black
    case 3:
      lstring = "3";
      break;  // red
    case 4:
      lstring = "6";
      break;  // yellow
    case 5:
      lstring = "1";
      break;  // white
    case 6:
      lstring = "11";
      break;  // orange
    case 7:
      lstring = "2,6";
      break;  // black/yellow
    case 8:
      lstring = "2,6,2";
      break;  // black/yellow/black
    case 9:
      lstring = "6,2";
      break;  // yellow/black
    case 10:
      lstring = "6,2,6";
      break;  // yellow/black/yellow
    case 11:
      lstring = "3,1";
      break;  // red/white
    case 12:
      lstring = "4,3,4";
      break;  // green/red/green
    case 13:
      lstring = "3,4,3";
      break;  // red/green/red
    case 14:
      lstring = "2,3,2";
      break;  // black/red/black
    case 15:
      lstring = "6,3,6";
      break;  // yellow/red/yellow
    case 16:
      lstring = "4,3";
      break;  // green/red
    case 17:
      lstring = "3,4";
      break;  // red/green
    case 18:
      lstring = "4,1";
      break;  // green/white
    default:
      break;
  }

  if (lstring.Len()) {
    free(pattValTmp->value);  // free the old int pointer

    pattValTmp->valType = OGR_STR;
    pattValTmp->value = strdup(lstring.mb_str());
  }
}

S57Obj *Cm93Transcoder::createS57Obj(int cell_index, int iobject, int subcell,
                                     Cm93Object *pobject,
                                     Extended_Geometry *xgeom,
                                     double view_scale_ppm) {
#define MAX_HDR_LINE 4000

  // printf("%d\n", iobject);

  int npub_year = 1993;  // silly default

  int iclass = pobject->otype;
  int geomtype = pobject->geotype & 0x0f;

  double tmp_transform_x = 0.;
  double tmp_transform_y = 0.;

  //    Per object transfor offsets,
  double trans_WGS84_offset_x = 0.;
  double trans_WGS84_offset_y = 0.;

  wxString sclass = QString_to_wxString(m_dict->className(iclass));
  if (sclass == "Unknown") {
    wxString msg;
    msg.Printf("   CM93 Error...object type %d not found in CM93OBJ.DIC",
               iclass);
    wxLogMessage(msg);
    delete xgeom;
    return NULL;
  }

  wxString sclass_sub = sclass;

  //  Going to make some substitutions here
  if (sclass.IsSameAs("ITDARE")) sclass_sub = "DEPARE";

  if (sclass.IsSameAs("_m_sor")) sclass_sub = "M_COVR";

  if (sclass.IsSameAs("SPOGRD")) sclass_sub = "DMPGRD";

  if (sclass.IsSameAs("FSHHAV")) sclass_sub = "FSHFAC";

  if (sclass.IsSameAs("OFSPRD")) sclass_sub = "CTNARE";

  //    Create the S57 Object
  S57Obj *pobj = new S57Obj();

  pobj->Index = iobject;

  char u[201];
  strncpy(u, sclass_sub.mb_str(), 199);
  u[200] = '\0';
  memcpy(pobj->FeatureName, u, 7);

  pobj->attVal = new wxArrayOfS57attVal();

  Cm93AttrBlock pab(pobject->attributes_block, m_dict);

  for (int jattr = 0; jattr < pobject->n_attributes; jattr++) {
    unsigned char *curr_attr = pab.GetNextAttr();

    unsigned char iattr = *curr_attr;

    wxString sattr = QString_to_wxString(m_dict->attrName(iattr));

    char vtype = m_dict->attrValueType(iattr);

    unsigned char *aval = curr_attr + 1;

    char val[4000];
    int *pi;
    float *pf;
    unsigned short *pw;
    unsigned char *pb;
    int *pAVI;
    char *pAVS;
    double *pAVR;
    double dival;
    int ival;

    S57attVal *pattValTmp = new S57attVal;

    switch (vtype) {
      case 'I':  // never seen?
        pi = (int *)aval;
        pAVI = (int *)malloc(sizeof(int));  // new int;
        *pAVI = *pi;
        pattValTmp->valType = OGR_INT;
        pattValTmp->value = pAVI;
        break;
      case 'B':
        pb = (unsigned char *)aval;
        pAVI = (int *)malloc(sizeof(int));  // new int;
        *pAVI = (int)(*pb);
        pattValTmp->valType = OGR_INT;
        pattValTmp->value = pAVI;
        break;
      case 'W':  // aWORD10
        pw = (unsigned short *)aval;
        ival = (int)(*pw);
        dival = ival;

        pAVR = (double *)malloc(sizeof(double));  // new double;
        *pAVR = dival / 10.;
        pattValTmp->valType = OGR_REAL;
        pattValTmp->value = pAVR;
        break;
      case 'G':
        pi = (int *)aval;
        pAVI = (int *)malloc(sizeof(int));  // new int;
        *pAVI = (int)(*pi);
        pattValTmp->valType = OGR_INT;
        pattValTmp->value = pAVI;
        break;

      case 'S':
        pAVS = strdup((char *)aval);
        pattValTmp->valType = OGR_STR;
        pattValTmp->value = pAVS;
        break;

      case 'C':
        pAVS = strdup((const char *)&aval[3]);
        pattValTmp->valType = OGR_STR;
        pattValTmp->value = pAVS;
        break;
      case 'L': {
        pb = (unsigned char *)aval;
        unsigned char nl = *pb++;
        char vi[20];
        val[0] = 0;
        for (int i = 0; i < nl; i++) {
          sprintf(vi, "%d,", *pb++);
          strcat(val, vi);
        }
        if (strlen(val)) val[strlen(val) - 1] = 0;  // strip last ","

        pAVS = strdup(val);
        pattValTmp->valType = OGR_STR;
        pattValTmp->value = pAVS;
        break;
      }
      case 'R': {
        pAVR = (double *)malloc(sizeof(double));  // new double;
        pf = (float *)aval;
#ifdef __ARM_ARCH
        {
          float __attribute__((aligned(16))) tf1;
          unsigned char *pucf = (unsigned char *)pf;

          memcpy(&tf1, pucf, sizeof(float));
          *pAVR = tf1;
        }
#else
        *pAVR = *pf;
#endif
        pattValTmp->valType = OGR_REAL;
        pattValTmp->value = pAVR;
        break;
      }
      default:
        sattr.Clear();  // Unknown, TODO track occasional case '?'
        break;
    }  // switch

    if (sattr.IsSameAs("COLMAR")) {
      translateColmar(sclass, pattValTmp);
      sattr = "COLOUR";
    }
    // XXX should be done from s57 list ans cm93 list for any mismatch
    // ie cm93 QUASOU is an enum s57 is a list
    if (pattValTmp->valType == OGR_INT &&
        (sattr.IsSameAs("QUASOU") || sattr.IsSameAs("CATLIT"))) {
      int v = *(int *)pattValTmp->value;
      free(pattValTmp->value);
      sprintf(val, "%d", v);
      pAVS = strdup(val);
      pattValTmp->valType = OGR_STR;
      pattValTmp->value = pAVS;
    }

    //    Do CM93 $SCODE attribute substitutions
    if (sclass.IsSameAs("$AREAS") && (vtype == 'S') &&
        sattr.IsSameAs("$SCODE")) {
      if (!strcmp((char *)pattValTmp->value, "II25")) {
        free(pattValTmp->value);
        pattValTmp->value = strdup("BACKGROUND");
      }
    }

    //    Capture some attributes on the fly as needed
    if (sattr.IsSameAs("RECDAT") || sattr.IsSameAs("_dgdat")) {
      if (sclass_sub.IsSameAs("M_COVR") && (vtype == 'S')) {
        wxString pub_date((char *)pattValTmp->value, wxConvUTF8);

        QDateTime upd = QDateTime::fromString(
            wxString_to_QString(pub_date), "yyyyMMdd");
        if (!upd.isValid())
          upd = QDateTime::fromString("20000101", "yyyyMMdd");
        m_ed_date = upd;

        pub_date.Truncate(4);

        long nyear = 0;
        pub_date.ToLong(&nyear);
        npub_year = nyear;
      }
    }

    //    Capture the potential WGS84 transform offset for later use
    if (sclass_sub.IsSameAs("M_COVR") && (vtype == 'R')) {
      if (sattr.IsSameAs("_wgsox")) {
        tmp_transform_x = *(double *)pattValTmp->value;
        if (fabs(tmp_transform_x) > 1.0)  // metres
          m_cib->b_have_offsets = true;
      } else if (sattr.IsSameAs("_wgsoy")) {
        tmp_transform_y = *(double *)pattValTmp->value;
        if (fabs(tmp_transform_y) > 1.0) m_cib->b_have_offsets = true;
      }
    }

    if (sattr.Len()) {
      wxASSERT(sattr.Len() == 6);
      wxCharBuffer dbuffer = sattr.ToUTF8();
      if (dbuffer.data()) {
        pobj->att_array =
            (char *)realloc(pobj->att_array, 6 * (pobj->n_attr + 1));

        strncpy(pobj->att_array + (6 * sizeof(char) * pobj->n_attr),
                dbuffer.data(), 6);
        pobj->n_attr++;

        pobj->attVal->Add(pattValTmp);
      } else
        delete pattValTmp;
    } else
      delete pattValTmp;

  }  // for

  //    ATON label optimization:
  //    Some CM93 ATON objects do not contain OBJNAM attribute, which means that
  //    no label is shown for these objects when ATON labals are requested Look
  //    for these cases, and change the INFORM attribute label to OBJNAM, if
  //    present.

  if (1 == geomtype) {
    if ((!strncmp(pobj->FeatureName, "LIT", 3)) ||
        (!strncmp(pobj->FeatureName, "LIGHTS", 6)) ||
        (!strncmp(pobj->FeatureName, "BCN", 3)) ||
        (!strncmp(pobj->FeatureName, "_slgto", 6)) ||
        (!strncmp(pobj->FeatureName, "_boygn", 6)) ||
        (!strncmp(pobj->FeatureName, "_bcngn", 6)) ||
        (!strncmp(pobj->FeatureName, "_extgn", 6)) ||
        (!strncmp(pobj->FeatureName, "TOWERS", 6)) ||
        (!strncmp(pobj->FeatureName, "BOY", 3))) {
      bool bfound_OBJNAM = (pobj->GetAttributeIndex("OBJNAM") != -1);
      bool bfound_INFORM = (pobj->GetAttributeIndex("INFORM") != -1);

      if ((!bfound_OBJNAM) && (bfound_INFORM))  // can make substitution
      {
        char *patl = pobj->att_array;
        for (int i = 0; i < pobj->n_attr; i++) {  // find "INFORM"
          if (!strncmp(patl, "INFORM", 6)) {
            memcpy(patl, "OBJNAM", 6);  // change to "OBJNAM"
            break;
          }

          patl += 6;
        }
      }
    }
  }

  switch (geomtype) {
    case 4: {
      pobj->Primitive_type = GEO_AREA;

      //    Capture M_COVR coverage + offsets (lightweight Qt port of the
      //    wx covr_set machinery): record the exterior ring in lat/lon, the
      //    cell/object ids, the publication year and the _wgsox/_wgsoy
      //    transform offsets onto the transcoder's coverage list. User
      //    offsets (the CM93 offset dialog) are 0 until that UI is ported.
      if (sclass_sub.IsSameAs("M_COVR")) {
        Cm93Covr covr;
        covr.cell_index = cell_index;
        covr.object_id = iobject;
        covr.subcell = subcell;
        covr.pub_year = npub_year;
        covr.wgsox = tmp_transform_x;
        covr.wgsoy = tmp_transform_y;
        const int npta = xgeom->contour_array[0];
        covr.ring.reserve(npta);
        double lat, lon;
        for (int ip = 0; ip < npta; ip++) {
          cm93_point p;
          p.x = (int)xgeom->vertex_array[ip + 1].m_x;
          p.y = (int)xgeom->vertex_array[ip + 1].m_y;
          transformPoint(&p, 0, 0, &lat, &lon);
          covr.lon_max = std::max<int>(covr.lon_max, lon);
          covr.lon_min = std::min<int>(covr.lon_min, lon);
          covr.lat_max = std::max<int>(covr.lat_max, lat);
          covr.lat_min = std::min<int>(covr.lat_min, lat);
          covr.ring.append(QPointF(lon, lat));
        }
        m_covrs.append(covr);
      }

      //  Declare x/y of the object to be average of all cm93points
      pobj->x = (xgeom->xmin + xgeom->xmax) / 2.;
      pobj->y = (xgeom->ymin + xgeom->ymax) / 2.;

      //    associate the vector(edge) index table
      pobj->m_n_lsindex = xgeom->n_vector_indices;
      pobj->m_lsindex_array =
          xgeom->pvector_index;       // object now owns the array
      pobj->m_n_edge_max_points = 0;  // xgeom->n_max_edge_points;

      //    Find the proper WGS offset for this object
      if (m_cib->b_have_offsets || m_cib->b_have_user_offsets) {
        double latc, lonc;
        cm93_point pc;
        pc.x = (short unsigned int)pobj->x;
        pc.y = (short unsigned int)pobj->y;
        transformPoint(&pc, 0., 0., &latc, &lonc);

        const Cm93Covr *pmcd = findCovrAt(latc, lonc);
        if (pmcd) {
          trans_WGS84_offset_x = pmcd->user_xoff;
          trans_WGS84_offset_y = pmcd->user_yoff;
        }
      }

      //  Set the s57obj bounding box as lat/lon
      double lat1, lon1, lat2, lon2;
      cm93_point p;

      p.x = (int)xgeom->xmin;
      p.y = (int)xgeom->ymin;
      transformPoint(&p, trans_WGS84_offset_x, trans_WGS84_offset_y, &lat1, &lon1);
      xgeom->ref_lat = lat1;
      xgeom->ref_lon = lon1;

      p.x = (int)xgeom->xmax;
      p.y = (int)xgeom->ymax;
      transformPoint(&p, trans_WGS84_offset_x, trans_WGS84_offset_y, &lat2, &lon2);
      pobj->BBObj.Set(lat1, lon1, lat2, lon2);

      //  Set the object base point
      p.x = (int)pobj->x;
      p.y = (int)pobj->y;
      transformPoint(&p, trans_WGS84_offset_x, trans_WGS84_offset_y, &lat1, &lon1);
      pobj->m_lon = lon1;
      pobj->m_lat = lat1;

      if (1) {
        //    This will be a deferred tesselation.....

        // Set up the conversion factors for use in the tesselator
        xgeom->x_rate = m_cib->transform_x_rate;
        xgeom->x_offset = m_cib->transform_x_origin - trans_WGS84_offset_x;
        xgeom->y_rate = m_cib->transform_y_rate;
        xgeom->y_offset = m_cib->transform_y_origin - trans_WGS84_offset_y;

        pobj->pPolyTessGeo = new PolyTessGeo(xgeom);
      }

      break;
    }

    case 1: {
      pobj->Primitive_type = GEO_POINT;
      pobj->npt = 1;

      pobj->x = xgeom->pointx;
      pobj->y = xgeom->pointy;

      double lat, lon;
      cm93_point p;
      p.x = xgeom->pointx;
      p.y = xgeom->pointy;
      transformPoint(&p, 0., 0., &lat, &lon);

      //    Find the proper WGS offset for this object
      if (m_cib->b_have_offsets || m_cib->b_have_user_offsets) {
        const Cm93Covr *pmcd = findCovrAt(lat, lon);
        if (pmcd) {
          trans_WGS84_offset_x = pmcd->user_xoff;
          trans_WGS84_offset_y = pmcd->user_yoff;
        }
      }

      //    Transform again to pick up offsets
      transformPoint(&p, trans_WGS84_offset_x, trans_WGS84_offset_y, &lat, &lon);

      pobj->m_lat = lat;
      pobj->m_lon = lon;

      // make initial bounding box large enough for worst possible case
      // it's not possible to know unless we knew the font, but this works
      // except for huge font sizes
      // this is not very good or accurate or efficient and hopefully we can
      // replace the current bounding box logic with calculating logic
      double llsize = 1e-3 / view_scale_ppm;

      pobj->BBObj.Set(lat, lon, lat, lon);
      pobj->BBObj.EnLarge(llsize);

      break;
    }

    case 8:  // wkbMultiPoint25D:
    {
      pobj->Primitive_type = GEO_POINT;

      //  Set the s57obj bounding box as lat/lon
      double lat1, lon1, lat2, lon2;
      cm93_point p;

      p.x = (int)xgeom->xmin;
      p.y = (int)xgeom->ymin;
      transformPoint(&p, 0., 0., &lat1, &lon1);

      p.x = (int)xgeom->xmax;
      p.y = (int)xgeom->ymax;
      transformPoint(&p, 0., 0., &lat2, &lon2);
      pobj->BBObj.Set(lat1, lon1, lat2, lon2);

      //  and declare x/y of the object to be average of all cm93points
      pobj->x = (xgeom->xmin + xgeom->xmax) / 2.;
      pobj->y = (xgeom->ymin + xgeom->ymax) / 2.;

      OGRMultiPoint *pGeo = (OGRMultiPoint *)xgeom->pogrGeom;
      pobj->npt = pGeo->getNumGeometries();

      pobj->geoPtz = (double *)malloc(pobj->npt * 3 * sizeof(double));
      pobj->geoPtMulti = (double *)malloc(pobj->npt * 2 * sizeof(double));

      double *pdd = pobj->geoPtz;
      double *pdl = pobj->geoPtMulti;

      for (int ip = 0; ip < pobj->npt; ip++) {
        OGRPoint *ppt = (OGRPoint *)(pGeo->getGeometryRef(ip));

        cm93_point p;
        p.x = (int)ppt->getX();
        p.y = (int)ppt->getY();
        double depth = ppt->getZ();

        double east = p.x;
        double north = p.y;

        double snd_trans_x = 0.;
        double snd_trans_y = 0.;

        //    Find the proper offset for this individual sounding
        if (m_cib->b_have_user_offsets) {
          double lats, lons;
          transformPoint(&p, 0., 0., &lats, &lons);

          const Cm93Covr *pmcd = findCovrAt(lats, lons);
          if (pmcd) {
            // For lat/lon calculation below
            snd_trans_x = pmcd->user_xoff;
            snd_trans_y = pmcd->user_yoff;

            // Actual cm93 point of this sounding, back-converted from metres
            // e/n
            east -= pmcd->user_xoff / m_cib->transform_x_rate;
            north -= pmcd->user_yoff / m_cib->transform_y_rate;
          }
        }

        *pdd++ = east;
        *pdd++ = north;
        *pdd++ = depth;

        //  Save offset lat/lon of point in obj->geoPtMulti for later use in
        //  decomposed bboxes
        transformPoint(&p, snd_trans_x, snd_trans_y, &lat1, &lon1);
        *pdl++ = lon1;
        *pdl++ = lat1;
      }

      //  Set the object base point
      p.x = (int)pobj->x;
      p.y = (int)pobj->y;
      transformPoint(&p, trans_WGS84_offset_x, trans_WGS84_offset_y, &lat1, &lon1);
      pobj->m_lon = lon1;
      pobj->m_lat = lat1;

      delete pGeo;

      break;
    }  // case 8

    case 2: {
      pobj->Primitive_type = GEO_LINE;

      pobj->npt = xgeom->n_max_vertex;
      pobj->geoPt = (pt *)xgeom->vertex_array;
      xgeom->vertex_array = NULL;  // object now owns the array

      //  Declare x/y of the object to be average of all cm93points
      pobj->x = (xgeom->xmin + xgeom->xmax) / 2.;
      pobj->y = (xgeom->ymin + xgeom->ymax) / 2.;

      //    associate the vector(edge) index table
      pobj->m_n_lsindex = xgeom->n_vector_indices;
      pobj->m_lsindex_array =
          xgeom->pvector_index;       // object now owns the array
      pobj->m_n_edge_max_points = 0;  // xgeom->n_max_edge_points;

      //    Find the proper WGS offset for this object
      if (m_cib->b_have_offsets || m_cib->b_have_user_offsets) {
        double latc, lonc;
        cm93_point pc;
        pc.x = (short unsigned int)pobj->x;
        pc.y = (short unsigned int)pobj->y;
        transformPoint(&pc, 0., 0., &latc, &lonc);

        const Cm93Covr *pmcd = findCovrAt(latc, lonc);
        if (pmcd) {
          trans_WGS84_offset_x = pmcd->user_xoff;
          trans_WGS84_offset_y = pmcd->user_yoff;
        }
      }

      //  Set the s57obj bounding box as lat/lon
      double lat1, lon1, lat2, lon2;
      cm93_point p;

      p.x = (int)xgeom->xmin;
      p.y = (int)xgeom->ymin;
      transformPoint(&p, trans_WGS84_offset_x, trans_WGS84_offset_y, &lat1, &lon1);

      p.x = (int)xgeom->xmax;
      p.y = (int)xgeom->ymax;
      transformPoint(&p, trans_WGS84_offset_x, trans_WGS84_offset_y, &lat2, &lon2);
      pobj->BBObj.Set(lat1, lon1, lat2, lon2);

      //  Set the object base point
      p.x = (int)pobj->x;
      p.y = (int)pobj->y;
      transformPoint(&p, trans_WGS84_offset_x, trans_WGS84_offset_y, &lat1, &lon1);
      pobj->m_lon = lon1;
      pobj->m_lat = lat1;

      break;

    }  // case 2
    default: {
      // TODO GEO_PRIM here is a placeholder.  Trace this code....
      pobj->Primitive_type = GEO_PRIM;
      break;
    }

  }  // geomtype switch

  //  Is this a catagory-movable object?
  if (!strncmp(pobj->FeatureName, "OBSTRN", 6) ||
      !strncmp(pobj->FeatureName, "WRECKS", 6) ||
      !strncmp(pobj->FeatureName, "DEPCNT", 6) ||
      !strncmp(pobj->FeatureName, "UWTROC", 6)) {
    pobj->m_bcategory_mutable = true;
  } else {
    pobj->m_bcategory_mutable = false;
  }

  //      Build/Maintain a list of found OBJL types for later use
  //      And back-reference the appropriate list index in S57Obj for Display
  //      Filtering

  pobj->iOBJL = -1;  // deferred, done by OBJL filtering in the PLIB as needed

  // Everything in Xgeom that is needed later has been given to the object
  // So, the xgeom object can be deleted
  // Except for area features, which will get deferred tesselation, and so need
  // the Extended geometry point Those features will own the xgeom...
  if (geomtype != 4) delete xgeom;

  //    Set the per-object transform coefficients
  pobj->x_rate =
      m_cib->transform_x_rate *
      (mercator_k0 * WGS84_semimajor_axis_meters / CM93_semimajor_axis_meters);
  pobj->y_rate =
      m_cib->transform_y_rate *
      (mercator_k0 * WGS84_semimajor_axis_meters / CM93_semimajor_axis_meters);
  pobj->x_origin =
      m_cib->transform_x_origin *
      (mercator_k0 * WGS84_semimajor_axis_meters / CM93_semimajor_axis_meters);
  pobj->y_origin =
      m_cib->transform_y_origin *
      (mercator_k0 * WGS84_semimajor_axis_meters / CM93_semimajor_axis_meters);

  //    Add in the possible offsets to WGS84 which come from the proper M_COVR
  //    containing this feature
  pobj->x_origin -= trans_WGS84_offset_x;
  pobj->y_origin -= trans_WGS84_offset_y;

  // Mark the object chart type, for the convenience of S52PLIB
  pobj->auxParm3 = CHART_TYPE_CM93;

  return pobj;
}

const Cm93Covr *Cm93Transcoder::findCovrAt(double lat, double lon) const {
  if (m_covrs.isEmpty()) return nullptr;
  if (m_covrs.size() == 1) return &m_covrs[0];  // the usual case (wx parity)
  for (const Cm93Covr &c : m_covrs) {
    if (lat < c.lat_min || lat > c.lat_max || lon < c.lon_min ||
        lon > c.lon_max)
      continue;
    // Ray-cast point-in-ring ((lon, lat) vertices).
    bool in = false;
    const int n = c.ring.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
      const QPointF &a = c.ring[i], &b = c.ring[j];
      if (((a.y() > lat) != (b.y() > lat)) &&
          (lon < (b.x() - a.x()) * (lat - a.y()) / (b.y() - a.y()) + a.x()))
        in = !in;
    }
    if (in) return &c;
  }
  return nullptr;
}

}  // namespace ocpn::qtui
