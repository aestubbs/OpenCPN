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
 * Implement osenc_reader.h. OSENC is written little-endian; macOS targets
 * (arm64/x86_64) are LE, so fixed-width fields are read by memcpy after a
 * length check. The record base is {uint16 type; uint32 length} packed (6
 * bytes); `length` includes the base, so the payload is `length - 6`.
 */

#include "osenc_reader.h"

#include <cstring>

#include <QFile>
#include <QIODevice>

namespace ocpn::qtui {

namespace {
constexpr int kRecordBaseLen = 6;  // uint16 type + uint32 length (packed)

bool readExact(QIODevice& in, char* buf, qint64 n) {
  qint64 got = 0;
  while (got < n) {
    const qint64 r = in.read(buf + got, n - got);
    if (r <= 0) return false;
    got += r;
  }
  return true;
}

uint16_t le16(const char* p) {
  return static_cast<uint16_t>(static_cast<unsigned char>(p[0]) |
                               (static_cast<unsigned char>(p[1]) << 8));
}
uint32_t le32(const char* p) {
  return static_cast<uint32_t>(static_cast<unsigned char>(p[0])) |
         (static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
         (static_cast<uint32_t>(static_cast<unsigned char>(p[2])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}
}  // namespace

bool scanOsencHeader(QIODevice& in, OsencHeader& out) {
  out = OsencHeader{};
  for (;;) {
    char base[kRecordBaseLen];
    if (!readExact(in, base, kRecordBaseLen)) break;
    const uint16_t type = le16(base);
    const uint32_t length = le32(base + 2);
    if (length < kRecordBaseLen) break;  // malformed
    const uint32_t plen = length - kRecordBaseLen;

    // The header block (version/name/dates/scale + extent + coverage) precedes
    // the first feature record. Stop at FEATURE_ID_RECORD exactly -- NOT at a
    // numeric threshold, since CELL_EXTENT_RECORD (100) / coverage (98,99) sit
    // before the features yet have higher type codes than FEATURE_ID (64).
    if (type == FEATURE_ID_RECORD) break;

    QByteArray payload;
    if (plen) {
      payload.resize(static_cast<int>(plen));
      if (!readExact(in, payload.data(), plen)) break;
    }
    switch (type) {
      case HEADER_SENC_VERSION:
        if (plen >= 2) out.version = le16(payload.constData());
        break;
      case HEADER_CELL_NAME: {
        int z = payload.indexOf('\0');
        if (z >= 0) payload.truncate(z);
        out.cellName = QString::fromUtf8(payload).trimmed();
        break;
      }
      case HEADER_CELL_NATIVESCALE:
        if (plen >= 4)
          out.nativeScale = static_cast<int>(le32(payload.constData()));
        break;
      case CELL_EXTENT_RECORD:
        if (plen >= 8 * sizeof(double)) {
          double d[8];
          std::memcpy(d, payload.constData(), 8 * sizeof(double));
          // Order: sw_lat, sw_lon, nw_lat, nw_lon, ne_lat, ne_lon, se_lat,
          // se_lon (see _OSENC_EXTENT_Record_Payload). Match wx ingest200:
          // NLAT=nw_lat, SLAT=se_lat, WLON=nw_lon, ELON=se_lon.
          out.north = d[2];
          out.south = d[6];
          out.west = d[3];
          out.east = d[5];
          out.hasExtent = true;
        }
        break;
      default:
        break;
    }
  }
  out.valid = (out.version > 0) && out.hasExtent;
  return out.valid;
}

bool scanOsencHeaderFile(const QString& path, OsencHeader& out) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    out = OsencHeader{};
    return false;
  }
  return scanOsencHeader(f, out);
}

}  // namespace ocpn::qtui
