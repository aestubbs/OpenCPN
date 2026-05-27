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
 * OsencReader -- a native (wx-free) reader for the OpenCPN OSENC ("SENC")
 * record stream. The wx Osenc (gui/src/o_senc.cpp) is part of the monolithic
 * wx app and is wx-string coupled, so we port a minimal reader here.
 *
 * The same OSENC stream is produced from three sources, all converging on this
 * reader:
 *   - the wx SENC cache files (*.S57) and our own future SENC cache;
 *   - o-charts `.oesenc`/`.oeu` cells, decrypted by oexserverd (a byte stream);
 *   - (eventually) SENC we build from raw .000 ENC.
 *
 * This first slice parses the HEADER records into a catalog entry (cell name,
 * extent, native scale, SENC version) -- enough to catalogue and bound a cell.
 * Feature/geometry ingestion into S57Obj (reusing the s52_engine emit
 * pipeline) layers on top.
 *
 * Input is abstracted as an OsencStream so the same parser serves a file and
 * the oexserverd FIFO (which yields a byte stream, not a seekable file).
 */

#ifndef OCPN_QT_OSENC_READER_H_
#define OCPN_QT_OSENC_READER_H_

#include <cstdint>

#include <QByteArray>
#include <QString>

class QIODevice;

namespace ocpn::qtui {

// --- OSENC record types (subset; see gui/include/gui/o_senc.h) -------------
enum OsencRecordType : uint16_t {
  HEADER_SENC_VERSION = 1,
  HEADER_CELL_NAME = 2,
  HEADER_CELL_PUBLISHDATE = 3,
  HEADER_CELL_EDITION = 4,
  HEADER_CELL_UPDATEDATE = 5,
  HEADER_CELL_UPDATE = 6,
  HEADER_CELL_NATIVESCALE = 7,
  HEADER_CELL_SENCCREATEDATE = 8,
  FEATURE_ID_RECORD = 64,
  FEATURE_ATTRIBUTE_RECORD = 65,
  FEATURE_GEOMETRY_RECORD_POINT = 80,
  FEATURE_GEOMETRY_RECORD_LINE = 81,
  FEATURE_GEOMETRY_RECORD_AREA = 82,
  FEATURE_GEOMETRY_RECORD_MULTIPOINT = 83,
  VECTOR_EDGE_NODE_TABLE_RECORD = 96,
  VECTOR_CONNECTED_NODE_TABLE_RECORD = 97,
  CELL_COVR_RECORD = 98,
  CELL_NOCOVR_RECORD = 99,
  CELL_EXTENT_RECORD = 100,
};

/** Header catalog info parsed from an OSENC stream. */
struct OsencHeader {
  bool valid = false;
  int version = 0;       // SENC format version (e.g. 201)
  QString cellName;      // HEADER_CELL_NAME
  int nativeScale = 0;   // HEADER_CELL_NATIVESCALE (1:N)
  // Cell extent (degrees). north/south/east/west.
  double north = 0.0, south = 0.0, east = 0.0, west = 0.0;
  bool hasExtent = false;
};

/**
 * Read just the leading HEADER records of an OSENC stream (up to the first
 * feature/geometry record) into `out`. Returns true if a SENC version + a
 * usable extent were found. Cheap: stops as soon as the header block ends.
 */
bool scanOsencHeader(QIODevice& in, OsencHeader& out);

/** Convenience: open `path` (a *.S57 / *.oesenc / *.oeu SENC file) and scan. */
bool scanOsencHeaderFile(const QString& path, OsencHeader& out);

}  // namespace ocpn::qtui

#endif  // OCPN_QT_OSENC_READER_H_
