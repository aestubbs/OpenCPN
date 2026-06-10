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
 * CM93 binary cell structures + reader (P2.19 step (a)). The structs mirror
 * gui/include/gui/cm93.h (renamed: Object -> Cm93Object, Cell_Info_Block ->
 * Cm93CellBlock) minus the wx M_COVR list; the reader wraps the verbatim
 * descramble/ingest extraction in cm93_cell_reader.cpp. A loaded block owns
 * its raw tables and frees them in the destructor.
 */

#ifndef OCPN_QT_CM93_CELL_READER_H_
#define OCPN_QT_CM93_CELL_READER_H_

#include <QString>

namespace ocpn::qtui {

typedef struct {
  unsigned short x;
  unsigned short y;
} cm93_point;

typedef struct {
  unsigned short x;
  unsigned short y;
  unsigned short z;
} cm93_point_3d;

typedef struct {
  double lon_min;
  double lat_min;
  double lon_max;
  double lat_max;
  // Bounding Box, in Mercator transformed co-ordinates
  double easting_min;
  double northing_min;
  double easting_max;
  double northing_max;

  unsigned short usn_vector_records;  //  number of spacial(vector) records
  int n_vector_record_points;  //  number of cm93 points in vector record block
  int m_46;
  int m_4a;
  unsigned short usn_point3d_records;
  int m_50;
  int m_54;
  unsigned short usn_point2d_records;  // m_58;
  unsigned short m_5a;
  unsigned short m_5c;
  unsigned short usn_feature_records;  // m_5e, number of feature records

  int m_60;
  int m_64;
  unsigned short m_68;
  unsigned short m_6a;
  unsigned short m_6c;
  int m_nrelated_object_pointers;

  int m_72;
  unsigned short m_76;

  int m_78;
  int m_7c;
} header_struct;

typedef struct {
  unsigned short n_points;
  unsigned short x_min;
  unsigned short y_min;
  unsigned short x_max;
  unsigned short y_max;
  int index;
  cm93_point *p_points;
} geometry_descriptor;

typedef struct {
  geometry_descriptor *pGeom_Description;
  unsigned char segment_usage;
} vector_record_descriptor;

typedef struct {
  unsigned char otype;
  unsigned char geotype;
  unsigned short n_geom_elements;
  void *pGeometry;  // may be a (cm93_point*) or other geom;
  unsigned char n_related_objects;
  void *p_related_object_pointer_array;
  unsigned char n_attributes;       // number of attributes
  unsigned char *attributes_block;  // encoded attributes
} Cm93Object;

struct Cm93CellBlock {
  //          Georeferencing transform coefficients
  double transform_x_rate = 0;
  double transform_y_rate = 0;
  double transform_x_origin = 0;
  double transform_y_origin = 0;

  cm93_point *p2dpoint_array = nullptr;
  Cm93Object **pprelated_object_block = nullptr;
  unsigned char *attribute_block_top = nullptr;  // attributes block
  geometry_descriptor *edge_vector_descriptor_block = nullptr;
  geometry_descriptor *point3d_descriptor_block = nullptr;
  cm93_point *pvector_record_block_top = nullptr;
  cm93_point_3d *p3dpoint_array = nullptr;

  int m_nvector_records = 0;
  int m_nfeature_records = 0;
  int m_n_point3d_records = 0;
  int m_n_point2d_records = 0;

  // WGS84 / user offset bookkeeping (set during object transcoding; the
  // per-cell M_COVR records live on the transcoder's coverage list).
  bool b_have_offsets = false;
  bool b_have_user_offsets = false;
  double user_xoff = 0;
  double user_yoff = 0;

  double min_lat = 0, min_lon = 0;

  //    Allocated working blocks
  vector_record_descriptor *object_vector_record_descriptor_block = nullptr;
  Cm93Object *pobject_block = nullptr;

  Cm93CellBlock() = default;
  ~Cm93CellBlock();
  Cm93CellBlock(const Cm93CellBlock &) = delete;
  Cm93CellBlock &operator=(const Cm93CellBlock &) = delete;
};

class Cm93CellReader {
public:
  /** Read + descramble one CM93 cell file into `block` (which then owns
   *  the decoded tables). False on open failure or integrity mismatch. */
  static bool ingest(const QString &cell_file_name, Cm93CellBlock *block);

  /** The CM93 cell-grid index for a position at a native scale (1:N). */
  static int cellIndex(double lat, double lon, int scale);
  /** Lower-left corner of a cell given its index + scale. */
  static void cellOrigin(int cellindex, int scale, double *lat, double *lon);
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CM93_CELL_READER_H_
