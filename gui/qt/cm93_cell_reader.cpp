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
 * CM93 binary cell reader (P2.19 step (a)): the descramble tables, the
 * element-wise header/table readers and Ingest_CM93_Cell, extracted
 * VERBATIM from gui/src/cm93.cpp (lines ~509-1729) with only renames
 * (Object -> Cm93Object, Cell_Info_Block -> Cm93CellBlock) and the wx
 * M_COVR list dropped from the block (coverage extraction happens at the
 * object-decode stage, ported separately). Keep edits mechanical so the
 * two copies stay diffable until the wx file retires with P3.11.
 */

#include "cm93_cell_reader.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ocpn::qtui {

static const double CM93_semimajor_axis_meters = 6378388.0;  // CM93 datum
#ifndef PI
#define PI 3.1415926535897931160E0
#endif

static unsigned char Table_0[] = {
    0x0CD, 0x0EA, 0x0DC, 0x048, 0x03E, 0x06D, 0x0CA, 0x07B, 0x052, 0x0E1, 0x0A4,
    0x08E, 0x0AB, 0x005, 0x0A7, 0x097, 0x0B9, 0x060, 0x039, 0x085, 0x07C, 0x056,
    0x07A, 0x0BA, 0x068, 0x06E, 0x0F5, 0x05D, 0x002, 0x04E, 0x00F, 0x0A1, 0x027,
    0x024, 0x041, 0x034, 0x000, 0x05A, 0x0FE, 0x0CB, 0x0D0, 0x0FA, 0x0F8, 0x06C,
    0x074, 0x096, 0x09E, 0x00E, 0x0C2, 0x049, 0x0E3, 0x0E5, 0x0C0, 0x03B, 0x059,
    0x018, 0x0A9, 0x086, 0x08F, 0x030, 0x0C3, 0x0A8, 0x022, 0x00A, 0x014, 0x01A,
    0x0B2, 0x0C9, 0x0C7, 0x0ED, 0x0AA, 0x029, 0x094, 0x075, 0x00D, 0x0AC, 0x00C,
    0x0F4, 0x0BB, 0x0C5, 0x03F, 0x0FD, 0x0D9, 0x09C, 0x04F, 0x0D5, 0x084, 0x01E,
    0x0B1, 0x081, 0x069, 0x0B4, 0x009, 0x0B8, 0x03C, 0x0AF, 0x0A3, 0x008, 0x0BF,
    0x0E0, 0x09A, 0x0D7, 0x0F7, 0x08C, 0x067, 0x066, 0x0AE, 0x0D4, 0x04C, 0x0A5,
    0x0EC, 0x0F9, 0x0B6, 0x064, 0x078, 0x006, 0x05B, 0x09B, 0x0F2, 0x099, 0x0CE,
    0x0DB, 0x053, 0x055, 0x065, 0x08D, 0x007, 0x033, 0x004, 0x037, 0x092, 0x026,
    0x023, 0x0B5, 0x058, 0x0DA, 0x02F, 0x0B3, 0x040, 0x05E, 0x07F, 0x04B, 0x062,
    0x080, 0x0E4, 0x06F, 0x073, 0x01D, 0x0DF, 0x017, 0x0CC, 0x028, 0x025, 0x02D,
    0x0EE, 0x03A, 0x098, 0x0E2, 0x001, 0x0EB, 0x0DD, 0x0BC, 0x090, 0x0B0, 0x0FC,
    0x095, 0x076, 0x093, 0x046, 0x057, 0x02C, 0x02B, 0x050, 0x011, 0x00B, 0x0C1,
    0x0F0, 0x0E7, 0x0D6, 0x021, 0x031, 0x0DE, 0x0FF, 0x0D8, 0x012, 0x0A6, 0x04D,
    0x08A, 0x013, 0x043, 0x045, 0x038, 0x0D2, 0x087, 0x0A0, 0x0EF, 0x082, 0x0F1,
    0x047, 0x089, 0x06A, 0x0C8, 0x054, 0x01B, 0x016, 0x07E, 0x079, 0x0BD, 0x06B,
    0x091, 0x0A2, 0x071, 0x036, 0x0B7, 0x003, 0x03D, 0x072, 0x0C6, 0x044, 0x08B,
    0x0CF, 0x015, 0x09F, 0x032, 0x0C4, 0x077, 0x083, 0x063, 0x020, 0x088, 0x0F6,
    0x0AD, 0x0F3, 0x0E8, 0x04A, 0x0E9, 0x035, 0x01C, 0x05F, 0x019, 0x01F, 0x07D,
    0x070, 0x0FB, 0x0D1, 0x051, 0x010, 0x0D3, 0x02E, 0x061, 0x09D, 0x05C, 0x02A,
    0x042, 0x0BE, 0x0E6};

static unsigned char Encode_table[256];
static unsigned char Decode_table[256];
void CreateDecodeTable() {
  int i;
  for (i = 0; i < 256; i++) {
    Encode_table[i] = Table_0[i] ^ 8;
  }

  for (i = 0; i < 256; i++) {
    unsigned char a = Encode_table[i];
    Decode_table[(int)a] = (unsigned char)i;
  }
}

static int read_and_decode_bytes(FILE *stream, void *p, int nbytes) {
  if (0 == nbytes)  // declare victory if no bytes requested
    return 1;

  //    read into callers buffer
  if (fread(p, nbytes, 1, stream) != 1) return 0;

  //    decode inplace
  unsigned char *q = (unsigned char *)p;

  for (int i = 0; i < nbytes; i++) {
    unsigned char a = *q;
    int b = a;
    unsigned char c = Decode_table[b];
    *q = c;

    q++;
  }
  return 1;
}

static int read_and_decode_double(FILE *stream, double *p) {
  double t;
  //    read into temp buffer
  if (fread(&t, sizeof(double), 1, stream) != 1) return 0;

  //    decode inplace
  unsigned char *q = (unsigned char *)&t;

  for (unsigned int i = 0; i < sizeof(double); i++) {
    unsigned char a = *q;
    int b = a;
    unsigned char c = Decode_table[b];
    *q = c;

    q++;
  }

  //    copy to target
  *p = t;

  return 1;
}

static int read_and_decode_int(FILE *stream, int *p) {
  int t;
  //    read into temp buffer
  if (fread(&t, sizeof(int), 1, stream) != 1) return 0;

  //    decode inplace
  unsigned char *q = (unsigned char *)&t;

  for (unsigned int i = 0; i < sizeof(int); i++) {
    unsigned char a = *q;
    int b = a;
    unsigned char c = Decode_table[b];
    *q = c;

    q++;
  }

  //    copy to target
  *p = t;

  return 1;
}

static int read_and_decode_ushort(FILE *stream, unsigned short *p) {
  unsigned short t;
  //    read into temp buffer
  if (fread(&t, sizeof(unsigned short), 1, stream) != 1) return 0;

  //    decode inplace
  unsigned char *q = (unsigned char *)&t;

  for (unsigned int i = 0; i < sizeof(unsigned short); i++) {
    unsigned char a = *q;
    int b = a;
    unsigned char c = Decode_table[b];
    *q = c;

    q++;
  }

  //    copy to target
  *p = t;

  return 1;
}
//    Calculate the CM93 CellIndex integer for a given Lat/Lon, at a given scale

int Get_CM93_CellIndex(double lat, double lon, int scale) {
  int retval = 0;

  int dval;
  switch (scale) {
    case 20000000:
      dval = 120;
      break;  // Z
    case 3000000:
      dval = 60;
      break;  // A
    case 1000000:
      dval = 30;
      break;  // B
    case 200000:
      dval = 12;
      break;  // C
    case 100000:
      dval = 3;
      break;  // D
    case 50000:
      dval = 1;
      break;  // E
    case 20000:
      dval = 1;
      break;  // F
    case 7500:
      dval = 1;
      break;  // G
    default:
      dval = 1;
      break;
  }

  //    Longitude
  double lon1 = (lon + 360.) * 3.;  // basic cell size is 20 minutes
  while (lon1 >= 1080.0) lon1 -= 1080.0;
  unsigned short lon2 = (unsigned short)floor(lon1 / dval);  // normalize
  unsigned short lon3 = lon2 * dval;

  retval = lon3;

  //    Latitude
  double lat1 = (lat * 3.) + 270. - 30;
  unsigned short lat2 = (unsigned short)floor(lat1 / dval);  // normalize
  unsigned short lat3 = lat2 * dval;

  retval += (lat3 + 30) * 10000;

  return retval;
}

//    Calculate the Lat/Lon of the lower left corner of a CM93 cell,
//    given a CM93 CellIndex and scale
//    Returned longitude value is always > 0
void Get_CM93_Cell_Origin(int cellindex, int scale, double *lat, double *lon) {
  //    Longitude
  double idx1 = cellindex % 10000;
  double lont = (idx1 / 3.);

  *lon = lont;

  //    Latitude
  int idx2 = cellindex / 10000;
  double lat1 = idx2 - 270.;
  *lat = lat1 / 3.;
}
static bool read_header_and_populate_cib(FILE *stream, Cm93CellBlock *pCIB) {
  //    Read header, populate Cm93CellBlock

  //    This 128 byte block is read element-by-element, to allow for
  //    endian-ness correction by element.
  //    Unused elements are read and, well, unused.

  header_struct header;

  memset((void *)&header, 0, sizeof(header));

  read_and_decode_double(stream, &header.lon_min);
  read_and_decode_double(stream, &header.lat_min);
  read_and_decode_double(stream, &header.lon_max);
  read_and_decode_double(stream, &header.lat_max);

  read_and_decode_double(stream, &header.easting_min);
  read_and_decode_double(stream, &header.northing_min);
  read_and_decode_double(stream, &header.easting_max);
  read_and_decode_double(stream, &header.northing_max);

  read_and_decode_ushort(stream, &header.usn_vector_records);
  read_and_decode_int(stream, &header.n_vector_record_points);
  read_and_decode_int(stream, &header.m_46);
  read_and_decode_int(stream, &header.m_4a);
  read_and_decode_ushort(stream, &header.usn_point3d_records);
  read_and_decode_int(stream, &header.m_50);
  read_and_decode_int(stream, &header.m_54);
  read_and_decode_ushort(stream, &header.usn_point2d_records);
  read_and_decode_ushort(stream, &header.m_5a);
  read_and_decode_ushort(stream, &header.m_5c);
  read_and_decode_ushort(stream, &header.usn_feature_records);

  read_and_decode_int(stream, &header.m_60);
  read_and_decode_int(stream, &header.m_64);
  read_and_decode_ushort(stream, &header.m_68);
  read_and_decode_ushort(stream, &header.m_6a);
  read_and_decode_ushort(stream, &header.m_6c);
  read_and_decode_int(stream, &header.m_nrelated_object_pointers);

  read_and_decode_int(stream, &header.m_72);
  read_and_decode_ushort(stream, &header.m_76);

  read_and_decode_int(stream, &header.m_78);
  read_and_decode_int(stream, &header.m_7c);

  //    Calculate and record the cell coordinate transform coefficients

  double delta_x = header.easting_max - header.easting_min;
  if (delta_x < 0)
    delta_x += CM93_semimajor_axis_meters * 2.0 * PI;  // add one trip around

  pCIB->transform_x_rate = delta_x / 65535;
  pCIB->transform_y_rate = (header.northing_max - header.northing_min) / 65535;

  pCIB->transform_x_origin = header.easting_min;
  pCIB->transform_y_origin = header.northing_min;

  pCIB->min_lat = header.lat_min;
  pCIB->min_lon = header.lon_min;

  //      pCIB->m_cell_mcovr_array.Empty();

  //    Extract some table sizes from the header, and pre-allocate the tables
  //    We do it this way to avoid incremental realloc() calls, which are
  //    expensive

  pCIB->m_nfeature_records = header.usn_feature_records;
  pCIB->pobject_block =
      (Cm93Object *)calloc(pCIB->m_nfeature_records * sizeof(Cm93Object), 1);

  pCIB->m_n_point2d_records = header.usn_point2d_records;
  pCIB->p2dpoint_array =
      (cm93_point *)malloc(pCIB->m_n_point2d_records * sizeof(cm93_point));

  pCIB->pprelated_object_block =
      (Cm93Object **)malloc(header.m_nrelated_object_pointers * sizeof(Cm93Object *));

  pCIB->object_vector_record_descriptor_block =
      (vector_record_descriptor *)malloc((header.m_4a + header.m_46) *
                                         sizeof(vector_record_descriptor));

  pCIB->attribute_block_top = (unsigned char *)calloc(header.m_78, 1);

  pCIB->m_nvector_records = header.usn_vector_records;
  pCIB->edge_vector_descriptor_block = (geometry_descriptor *)malloc(
      header.usn_vector_records * sizeof(geometry_descriptor));

  pCIB->pvector_record_block_top =
      (cm93_point *)malloc(header.n_vector_record_points * sizeof(cm93_point));

  pCIB->m_n_point3d_records = header.usn_point3d_records;
  pCIB->point3d_descriptor_block = (geometry_descriptor *)malloc(
      pCIB->m_n_point3d_records * sizeof(geometry_descriptor));

  pCIB->p3dpoint_array =
      (cm93_point_3d *)malloc(header.m_50 * sizeof(cm93_point_3d));

  return true;
}

static bool read_vector_record_table(FILE *stream, int count,
                                     Cm93CellBlock *pCIB) {
  bool brv;

  geometry_descriptor *p = pCIB->edge_vector_descriptor_block;
  cm93_point *q = pCIB->pvector_record_block_top;

  for (int iedge = 0; iedge < count; iedge++) {
    p->index = iedge;

    unsigned short npoints;
    brv = !(read_and_decode_ushort(stream, &npoints) == 0);
    if (!brv) return false;

    p->n_points = npoints;
    p->p_points = q;

    //           brv = read_and_decode_bytes(stream, q, p->n_points *
    //           sizeof(cm93_point));
    //            if(!brv)
    //                  return false;

    unsigned short x, y;
    for (int index = 0; index < p->n_points; index++) {
      if (!read_and_decode_ushort(stream, &x)) return false;
      if (!read_and_decode_ushort(stream, &y)) return false;

      q[index].x = x;
      q[index].y = y;
    }

    //    Compute and store the min/max of this block of n_points
    cm93_point *t = p->p_points;

    p->x_max = t->x;
    p->x_min = t->x;
    p->y_max = t->y;
    p->y_min = t->y;

    t++;

    for (int j = 0; j < p->n_points - 1; j++) {
      if (t->x >= p->x_max) p->x_max = t->x;

      if (t->x <= p->x_min) p->x_min = t->x;

      if (t->y >= p->y_max) p->y_max = t->y;

      if (t->y <= p->y_max) p->y_min = t->y;

      t++;
    }

    //    Advance the block pointer
    q += p->n_points;

    //    Advance the geometry descriptor pointer
    p++;
  }

  return true;
}

static bool read_3dpoint_table(FILE *stream, int count, Cm93CellBlock *pCIB) {
  geometry_descriptor *p = pCIB->point3d_descriptor_block;
  cm93_point_3d *q = pCIB->p3dpoint_array;

  for (int i = 0; i < count; i++) {
    unsigned short npoints;
    if (!read_and_decode_ushort(stream, &npoints)) return false;

    p->n_points = npoints;
    p->p_points = (cm93_point *)q;  // might not be the right cast

    //            unsigned short t = p->n_points;

    //            if(!read_and_decode_bytes(stream, q, t*6))
    //                  return false;

    unsigned short x, y, z;
    for (int index = 0; index < p->n_points; index++) {
      if (!read_and_decode_ushort(stream, &x)) return false;
      if (!read_and_decode_ushort(stream, &y)) return false;
      if (!read_and_decode_ushort(stream, &z)) return false;

      q[index].x = x;
      q[index].y = y;
      q[index].z = z;
    }

    p++;
    q++;
  }

  return true;
}

static bool read_2dpoint_table(FILE *stream, int count, Cm93CellBlock *pCIB) {
  //      int rv = read_and_decode_bytes(stream, pCIB->p2dpoint_array, count *
  //      4);

  unsigned short x, y;
  for (int index = 0; index < count; index++) {
    if (!read_and_decode_ushort(stream, &x)) return false;
    if (!read_and_decode_ushort(stream, &y)) return false;

    pCIB->p2dpoint_array[index].x = x;
    pCIB->p2dpoint_array[index].y = y;
  }

  return true;
}

static bool read_feature_record_table(FILE *stream, int n_features,
                                      Cm93CellBlock *pCIB) {
  try {
    Cm93Object *pobj = pCIB->pobject_block;  // head of object array

    vector_record_descriptor *pobject_vector_collection =
        pCIB->object_vector_record_descriptor_block;

    Cm93Object **p_relob =
        pCIB->pprelated_object_block;  // head of previously allocated related
                                       // object pointer block

    unsigned char *puc_var10 = pCIB->attribute_block_top;  // m_3a;
    int puc10count = 0;  // should be same as header.m_78

    unsigned char object_type;
    unsigned char geom_prim;
    unsigned short obj_desc_bytes = 0;

    unsigned int t;
    unsigned short index;
    unsigned short n_elements;

    for (int iobject = 0; iobject < n_features; iobject++) {
      // read the object definition
      read_and_decode_bytes(stream, &object_type, 1);  // read the object type
      read_and_decode_bytes(stream, &geom_prim,
                            1);  // read the object geometry primitive type
      read_and_decode_ushort(stream,
                             &obj_desc_bytes);  // read the object byte count

      pobj->otype = object_type;
      pobj->geotype = geom_prim;

      switch (pobj->geotype & 0x0f) {
        case 4:  // AREA
        {
          if (!read_and_decode_ushort(stream, &n_elements)) return false;

          pobj->n_geom_elements = n_elements;
          t = (pobj->n_geom_elements * 2) + 2;
          obj_desc_bytes -= t;

          pobj->pGeometry =
              pobject_vector_collection;  // save pointer to created
                                          // vector_record_descriptor in the
                                          // object

          for (unsigned short i = 0; i < pobj->n_geom_elements; i++) {
            if (!read_and_decode_ushort(stream, &index)) return false;

            if ((index & 0x1fff) > pCIB->m_nvector_records)
              return false;  // error in this cell, ignore all of it

            geometry_descriptor *u = &pCIB->edge_vector_descriptor_block[(
                index & 0x1fff)];  // point to the vector descriptor

            pobject_vector_collection->pGeom_Description = u;
            pobject_vector_collection->segment_usage =
                (unsigned char)(index >> 13);

            pobject_vector_collection++;
          }

          break;
        }  // AREA geom

        case 2:  // LINE geometry
        {
          if (!read_and_decode_ushort(
                  stream, &n_elements))  // read geometry element count
            return false;

          pobj->n_geom_elements = n_elements;
          t = (pobj->n_geom_elements * 2) + 2;
          obj_desc_bytes -= t;

          pobj->pGeometry =
              pobject_vector_collection;  // save pointer to created
                                          // vector_record_descriptor in the
                                          // object

          for (unsigned short i = 0; i < pobj->n_geom_elements; i++) {
            unsigned short geometry_index;

            if (!read_and_decode_ushort(stream, &geometry_index)) return false;

            if ((geometry_index & 0x1fff) > pCIB->m_nvector_records)
              //                                    *(int *)(0) = 0; // error
              return 0;  // error, bad pointer

            geometry_descriptor *u = &pCIB->edge_vector_descriptor_block[(
                geometry_index & 0x1fff)];  // point to the vector descriptor

            pobject_vector_collection->pGeom_Description = u;
            pobject_vector_collection->segment_usage =
                (unsigned char)(geometry_index >> 13);

            pobject_vector_collection++;
          }

          break;
        }

        case 1: {
          if (!read_and_decode_ushort(stream, &index)) return false;

          obj_desc_bytes -= 2;

          pobj->n_geom_elements = 1;  // one point

          pobj->pGeometry = &pCIB->p2dpoint_array[index];  // cm93_point *

          break;
        }

        case 8: {
          if (!read_and_decode_ushort(stream, &index)) return false;
          obj_desc_bytes -= 2;

          pobj->n_geom_elements = 1;  // one point

          pobj->pGeometry =
              &pCIB->point3d_descriptor_block[index];  // geometry_descriptor *

          break;
        }

      }  // switch

      if ((pobj->geotype & 0x10) == 0x10)  // children/related
      {
        unsigned char nrelated;
        if (!read_and_decode_bytes(stream, &nrelated, 1)) return false;

        pobj->n_related_objects = nrelated;
        t = (pobj->n_related_objects * 2) + 1;
        obj_desc_bytes -= t;

        pobj->p_related_object_pointer_array = p_relob;
        p_relob += pobj->n_related_objects;

        Cm93Object **w = (Cm93Object **)pobj->p_related_object_pointer_array;
        for (unsigned char j = 0; j < pobj->n_related_objects; j++) {
          if (!read_and_decode_ushort(stream, &index)) return false;

          if (index > pCIB->m_nfeature_records)
            //                              *(int *)(0) = 0; // error
            return false;

          Cm93Object *prelated_object = &pCIB->pobject_block[index];
          *w = prelated_object;  // fwd link

          prelated_object->p_related_object_pointer_array =
              pobj;  // back link, array of 1 element
          w++;
        }
      }

      if ((pobj->geotype & 0x20) == 0x20) {
        unsigned short nrelated;
        if (!read_and_decode_ushort(stream, &nrelated)) return false;

        pobj->n_related_objects = (unsigned char)(nrelated & 0xFF);
        obj_desc_bytes -= 2;
      }

      if ((pobj->geotype & 0x40) == 0x40) {
      }

      if ((pobj->geotype & 0x80) == 0x80)  // attributes
      {
        unsigned char nattr;
        if (!read_and_decode_bytes(stream, &nattr, 1)) return false;  // m_od

        pobj->n_attributes = nattr;
        obj_desc_bytes -= 5;

        pobj->attributes_block = puc_var10;
        puc_var10 += obj_desc_bytes;

        puc10count += obj_desc_bytes;

        if (!read_and_decode_bytes(stream, pobj->attributes_block,
                                   obj_desc_bytes))
          return false;  // the attributes....

        if ((pobj->geotype & 0x0f) == 1) {
        }
      }

      pobj++;  // next object
    }

    //      wxASSERT(puc10count == pCIB->m_22->m_78);
  }

  catch (...) {
    printf("catch on read_feature_record_table\n");
  }

  return true;
}
bool Ingest_CM93_Cell(const char *cell_file_name, Cm93CellBlock *pCIB) {
  try {
    int file_length;

    //    Get the file length
    FILE *flstream = fopen(cell_file_name, "rb");
    if (!flstream) return false;

    fseek(flstream, 0, SEEK_END);
    file_length = ftell(flstream);
    fclose(flstream);

    //    Open the file
    FILE *stream = fopen(cell_file_name, "rb");
    if (!stream) return false;

    //    Validate the integrity of the cell file

    unsigned short word0 = 0;
    ;
    int int0 = 0;
    int int1 = 0;
    ;

    read_and_decode_ushort(stream,
                           &word0);      // length of prolog + header (10 + 128)
    read_and_decode_int(stream, &int0);  // length of table 1
    read_and_decode_int(stream, &int1);  // length of table 2

    int test = word0 + int0 + int1;
    if (test != file_length) {
      fclose(stream);
      return false;  // file is corrupt
    }

    //    Cell is OK, proceed to ingest

    if (!read_header_and_populate_cib(stream, pCIB)) {
      fclose(stream);
      return false;
    }

    if (!read_vector_record_table(stream, pCIB->m_nvector_records, pCIB)) {
      fclose(stream);
      return false;
    }

    if (!read_3dpoint_table(stream, pCIB->m_n_point3d_records, pCIB)) {
      fclose(stream);
      return false;
    }

    if (!read_2dpoint_table(stream, pCIB->m_n_point2d_records, pCIB)) {
      fclose(stream);
      return false;
    }

    if (!read_feature_record_table(stream, pCIB->m_nfeature_records, pCIB)) {
      fclose(stream);
      return false;
    }

    //      int file_end = ftell(stream);

    //      wxASSERT(file_end == file_length);

    fclose(stream);

    return true;
  }

  catch (...) {
    return false;
  }
}


bool Cm93CellReader::readHeaderExtent(const QString& cell_file_name,
                                      double* lat_min, double* lat_max,
                                      double* lon_min, double* lon_max) {
  CreateDecodeTable();
  FILE* stream = fopen(cell_file_name.toLocal8Bit().constData(), "rb");
  if (!stream) return false;
  // Prolog: total length words (integrity check skipped -- header only).
  unsigned short word0 = 0;
  int int0 = 0, int1 = 0;
  if (!read_and_decode_ushort(stream, &word0) ||
      !read_and_decode_int(stream, &int0) ||
      !read_and_decode_int(stream, &int1)) {
    fclose(stream);
    return false;
  }
  double lon_mn = 0, lat_mn = 0, lon_mx = 0, lat_mx = 0;
  const bool ok = read_and_decode_double(stream, &lon_mn) &&
                  read_and_decode_double(stream, &lat_mn) &&
                  read_and_decode_double(stream, &lon_mx) &&
                  read_and_decode_double(stream, &lat_mx);
  fclose(stream);
  if (!ok) return false;
  if (lat_min) *lat_min = lat_mn;
  if (lat_max) *lat_max = lat_mx;
  if (lon_min) *lon_min = lon_mn;
  if (lon_max) *lon_max = lon_mx;
  return true;
}

bool Cm93CellReader::ingest(const QString& cell_file_name,
                            Cm93CellBlock* block) {
  CreateDecodeTable();  // idempotent; cheap
  return Ingest_CM93_Cell(cell_file_name.toLocal8Bit().constData(), block);
}

int Cm93CellReader::cellIndex(double lat, double lon, int scale) {
  return Get_CM93_CellIndex(lat, lon, scale);
}

void Cm93CellReader::cellOrigin(int cellindex, int scale, double* lat,
                                double* lon) {
  Get_CM93_Cell_Origin(cellindex, scale, lat, lon);
}

Cm93CellBlock::~Cm93CellBlock() {
  free(pobject_block);
  free(p2dpoint_array);
  free(pprelated_object_block);
  free(object_vector_record_descriptor_block);
  free(attribute_block_top);
  free(edge_vector_descriptor_block);
  free(pvector_record_block_top);
  free(point3d_descriptor_block);
  free(p3dpoint_array);
}

}  // namespace ocpn::qtui
