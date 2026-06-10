/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "raster_chart.h"

#include <cmath>

#include <QFile>
#include <QRegularExpression>

#include "toolkit/viewport.h"

namespace ocpn::qtui {

namespace {

struct RefPoint {
  double xr, yr, lat, lon;
};

struct KapHeader {
  int width = 0, height = 0;
  int scale = 0;
  QString name;
  QRgb palette[128];
  int maxPaletteIdx = 0;
  QList<RefPoint> refs;
  qsizetype binaryStart = -1;  // offset just after the 0x1A terminator
};

// Parse the KAP text header (everything before the 0x1A byte). Header
// lines wrap with leading spaces -- joined before tokenising.
bool parseHeader(const QByteArray& all, KapHeader* h, QString* err) {
  const qsizetype hdrEnd = all.indexOf('\x1a');
  if (hdrEnd < 0) {
    *err = QStringLiteral("no header terminator");
    return false;
  }
  h->binaryStart = hdrEnd + 1;
  const QString text = QString::fromLatin1(all.left(hdrEnd));
  QStringList lines;
  for (const QString& raw : text.split(QRegularExpression(
           QStringLiteral("\r?\n")), Qt::SkipEmptyParts)) {
    if (!lines.isEmpty() && raw.startsWith(QLatin1Char(' ')))
      lines.last() += raw.trimmed();
    else
      lines << raw.trimmed();
  }
  for (int i = 0; i < 128; ++i) h->palette[i] = qRgb(0, 0, 0);
  static const QRegularExpression raRe(
      QStringLiteral("RA=(\\d+)\\s*,\\s*(\\d+)"));
  static const QRegularExpression scRe(QStringLiteral("SC=(\\d+)"));
  static const QRegularExpression naRe(QStringLiteral("NA=([^,]+)"));
  for (const QString& line : lines) {
    if (line.startsWith(QLatin1String("BSB/")) ||
        line.startsWith(QLatin1String("KNP/")) ||
        line.startsWith(QLatin1String("NOS/"))) {
      if (const auto m = raRe.match(line); m.hasMatch()) {
        h->width = m.captured(1).toInt();
        h->height = m.captured(2).toInt();
      }
      if (const auto m = scRe.match(line); m.hasMatch())
        h->scale = m.captured(1).toInt();
      if (h->name.isEmpty()) {
        if (const auto m = naRe.match(line); m.hasMatch())
          h->name = m.captured(1).trimmed();
      }
    } else if (line.startsWith(QLatin1String("RGB/"))) {
      const QStringList p = line.mid(4).split(',');
      if (p.size() == 4) {
        const int idx = p[0].toInt();
        if (idx >= 0 && idx < 128) {
          h->palette[idx] = qRgb(p[1].toInt(), p[2].toInt(), p[3].toInt());
          h->maxPaletteIdx = qMax(h->maxPaletteIdx, idx);
        }
      }
    } else if (line.startsWith(QLatin1String("REF/"))) {
      const QStringList p = line.mid(4).split(',');
      if (p.size() >= 5) {
        RefPoint r;
        r.xr = p[1].toDouble();
        r.yr = p[2].toDouble();
        r.lat = p[3].toDouble();
        r.lon = p[4].toDouble();
        h->refs.append(r);
      }
    }
  }
  if (h->width <= 0 || h->height <= 0) {
    *err = QStringLiteral("no RA size");
    return false;
  }
  if (h->refs.size() < 2) {
    *err = QStringLiteral("fewer than 2 REF points");
    return false;
  }
  return true;
}

// Least-squares 1-D linear fit y = a*x + b.
void fit1d(const QList<QPointF>& pts, double* a, double* b) {
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  const int n = pts.size();
  for (const QPointF& p : pts) {
    sx += p.x();
    sy += p.y();
    sxx += p.x() * p.x();
    sxy += p.x() * p.y();
  }
  const double det = n * sxx - sx * sx;
  if (std::fabs(det) < 1e-9) {
    *a = 0;
    *b = n ? sy / n : 0;
    return;
  }
  *a = (n * sxy - sx * sy) / det;
  *b = (sy - *a * sx) / n;
}

// Fit the linear world mapping from the REF points; false when the fit
// residuals say this chart is not a north-up Mercator raster.
bool fitGeoref(const KapHeader& h, RasterChart* out, QString* err) {
  QList<QPointF> xs, ys;
  for (const RefPoint& r : h.refs) {
    xs.append(QPointF(r.xr, r.lon));
    ys.append(QPointF(r.yr, Viewport::latToWorldY(r.lat)));
  }
  double ax, bx, ay, by;
  fit1d(xs, &ax, &bx);
  fit1d(ys, &ay, &by);
  // Residual check: every REF must land within ~0.5% of the chart span.
  double maxRx = 0, maxRy = 0;
  for (const RefPoint& r : h.refs) {
    maxRx = qMax(maxRx, std::fabs(ax * r.xr + bx - r.lon));
    maxRy = qMax(maxRy,
                 std::fabs(ay * r.yr + by - Viewport::latToWorldY(r.lat)));
  }
  const double spanX = std::fabs(ax) * h.width;
  const double spanY = std::fabs(ay) * h.height;
  if (maxRx > spanX * 0.005 || maxRy > spanY * 0.005) {
    *err = QStringLiteral("not linear-Mercator (skewed/other projection)");
    return false;
  }
  out->west = bx;
  out->east = ax * h.width + bx;
  out->worldYTop = by;
  out->worldYBottom = ay * h.height + by;
  out->north = Viewport::worldYToLat(qMin(out->worldYTop, out->worldYBottom));
  out->south = Viewport::worldYToLat(qMax(out->worldYTop, out->worldYBottom));
  return true;
}

}  // namespace

RasterChart RasterChartReader::scanHeader(const QString& path) {
  RasterChart out;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    out.error = QStringLiteral("open failed");
    return out;
  }
  // Headers are small; 64 KiB covers every real chart.
  const QByteArray head = f.read(64 * 1024);
  KapHeader h;
  if (!parseHeader(head, &h, &out.error)) return out;
  if (!fitGeoref(h, &out, &out.error)) return out;
  out.name = h.name;
  out.nativeScale = h.scale;
  out.ok = true;
  return out;
}

RasterChart RasterChartReader::load(const QString& path) {
  RasterChart out;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    out.error = QStringLiteral("open failed");
    return out;
  }
  const QByteArray all = f.readAll();
  KapHeader h;
  if (!parseHeader(all, &h, &out.error)) return out;
  if (!fitGeoref(h, &out, &out.error)) return out;

  // Binary section: after 0x1A skip 0x0D/0x0A/0x00 filler, then the bit
  // depth (1..7).
  qsizetype p = h.binaryStart;
  while (p < all.size() &&
         (all[p] == '\x0d' || all[p] == '\x0a' || all[p] == '\x00'))
    ++p;
  if (p >= all.size()) {
    out.error = QStringLiteral("truncated binary section");
    return out;
  }
  const int depth = static_cast<unsigned char>(all[p]);
  if (depth < 1 || depth > 7) {
    out.error = QStringLiteral("bad bit depth %1").arg(depth);
    return out;
  }

  // EOF line-offset table: (height+1) big-endian 32-bit entries; the wx
  // reader takes the last entry slot as the table position itself.
  const qsizetype tableStart = all.size() - 4LL * (h.height + 1);
  if (tableStart <= p) {
    out.error = QStringLiteral("no line-offset table");
    return out;
  }
  QList<qsizetype> rows;
  rows.reserve(h.height);
  const unsigned char* tb =
      reinterpret_cast<const unsigned char*>(all.constData()) + tableStart;
  for (int i = 0; i < h.height; ++i) {
    const qsizetype off = (qsizetype(tb[0]) << 24) | (qsizetype(tb[1]) << 16) |
                          (qsizetype(tb[2]) << 8) | qsizetype(tb[3]);
    tb += 4;
    if (off <= 0 || off >= all.size()) {
      out.error = QStringLiteral("corrupt offset table");
      return out;
    }
    rows.append(off);
  }

  QImage img(h.width, h.height, QImage::Format_ARGB32);
  img.fill(Qt::transparent);
  const unsigned char* base =
      reinterpret_cast<const unsigned char*>(all.constData());
  const int valueShift = 7 - depth;
  const unsigned char valueMask = ((1 << depth) - 1) << valueShift;
  const unsigned char countMask = (1 << (7 - depth)) - 1;
  for (int y = 0; y < h.height; ++y) {
    const unsigned char* lp = base + rows[y];
    const unsigned char* end =
        base + (y + 1 < h.height ? rows[y + 1] : tableStart);
    // Skip the leading line-number varint (high-bit continuation).
    unsigned char byNext;
    do {
      byNext = (lp < end) ? *lp++ : 0;
    } while (byNext & 0x80);
    QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
    int x = 0;
    while (x < h.width && lp <= end) {
      byNext = (lp < end) ? *lp++ : 0;
      if (byNext == 0) break;  // row terminator
      const int value = (byNext & valueMask) >> valueShift;
      qulonglong run = byNext & countMask;
      while (byNext & 0x80) {
        byNext = (lp < end) ? *lp++ : 0;
        run = run * 128 + (byNext & 0x7f);
      }
      run += 1;
      const QRgb c = h.palette[value & 0x7f];
      for (qulonglong k = 0; k < run && x < h.width; ++k) dst[x++] = c;
    }
  }

  out.image = std::move(img);
  out.name = h.name;
  out.nativeScale = h.scale;
  out.ok = true;
  return out;
}

}  // namespace ocpn::qtui
