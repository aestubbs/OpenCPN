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
 * Implement gshhs_world_provider.h.
 *
 * GSHHS poly-c-1.dat layout (little-endian, matches the legacy
 * GshhsPolyReader in gui/src/gshhs.cpp):
 *   - 48-byte header: 12 int32 {version, pasx, pasy, xmin, ymin, xmax,
 *     ymax, p1..p5}. For the crude file pasx=pasy=1 (1°x1° cells),
 *     longitude runs 0..360.
 *   - offset table: 360*180 int32, indexed tab = clon*180 + (clat+90),
 *     each the byte offset of that cell's data block.
 *   - per cell: five contour groups (level 1 land, 2 lake, 3..5), each:
 *       int32 num_contours; per contour { int32 hole; int32 n;
 *       n * { double X, double Y } }  -- coords are micro-degrees.
 * We read only the level-1 (land) group of every cell.
 */

#include "gshhs_world_provider.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>

#include "sg_helpers.h"
#include "tesselator.h"
#include "viewport.h"

namespace ocpn::qtui {

namespace {
constexpr int kHeaderBytes = 48;       // 12 int32
constexpr int kGridLon = 360;
constexpr int kGridLat = 180;
constexpr double kMicro = 1.0e-6;      // micro-degrees -> degrees

// Read a little-endian POD from a byte cursor (file is LE; arm64/x86 host
// is LE, so a plain copy is correct).
template <typename T>
inline T rd(const uchar*& p) {
  T v;
  memcpy(&v, p, sizeof(T));
  p += sizeof(T);
  return v;
}

// Normalise a GSHHS longitude (0..360) into the canvas convention -180..180.
inline double normLon(double lon) { return lon > 180.0 ? lon - 360.0 : lon; }
}  // namespace

GshhsWorldProvider::GshhsWorldProvider(const QString& poly_path,
                                       QObject* parent)
    : ChartProvider(parent) {
  load(poly_path);
}

void GshhsWorldProvider::load(const QString& poly_path) {
  QFile f(poly_path);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning("GSHHS: cannot open %s", qPrintable(poly_path));
    return;
  }
  const QByteArray blob = f.readAll();
  f.close();
  if (blob.size() < kHeaderBytes + kGridLon * kGridLat * 4) {
    qWarning("GSHHS: %s too small (%lld bytes)", qPrintable(poly_path),
             (long long)blob.size());
    return;
  }
  QElapsedTimer timer;
  timer.start();

  const uchar* base = reinterpret_cast<const uchar*>(blob.constData());
  const qint64 size = blob.size();
  const uchar* offtab = base + kHeaderBytes;  // int32[360*180]

  TESStesselator* tess = tessNewTess(nullptr);
  QList<float> contour;  // scratch: interleaved x,y for one contour

  int n_contours = 0;
  for (int clon = 0; clon < kGridLon; ++clon) {
    for (int clat = -90; clat < 90; ++clat) {
      const int tab = clon * kGridLat + (clat + 90);
      const uchar* tp = offtab + tab * 4;
      qint32 pos = rd<qint32>(tp);
      if (pos <= 0 || pos >= size) continue;

      const uchar* p = base + pos;
      const qint32 num_contours = rd<qint32>(p);   // level-1 (land) group
      if (num_contours <= 0) continue;

      // Tessellate this cell's land contours together (multiple islands ->
      // multiple contours; holes were dropped by the format, so all are
      // positive fills under NONZERO winding).
      QList<QList<QPointF>> cell_contours;
      bool overran = false;
      for (int c = 0; c < num_contours && !overran; ++c) {
        rd<qint32>(p);                     // hole flag (unused)
        const qint32 n = rd<qint32>(p);
        if (n <= 0) continue;
        if (p + n * 16 > base + size) { overran = true; break; }

        contour.clear();
        contour.reserve(n * 2);
        QList<QPointF> world;
        world.reserve(n);
        for (int v = 0; v < n; ++v) {
          const double X = rd<double>(p);
          const double Y = rd<double>(p);
          const float wx = static_cast<float>(normLon(X * kMicro));
          const float wy = static_cast<float>(-(Y * kMicro));
          contour.append(wx);
          contour.append(wy);
          world.append(QPointF(wx, wy));
        }
        tessAddContour(tess, 2, contour.constData(), sizeof(float) * 2, n);
        cell_contours.append(std::move(world));
        ++n_contours;
      }
      if (cell_contours.isEmpty()) continue;

      if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 2,
                        nullptr)) {
        const float* verts = tessGetVertices(tess);
        const TESSindex* elems = tessGetElements(tess);
        const int ne = tessGetElementCount(tess);
        for (int i = 0; i < ne; ++i) {
          for (int j = 0; j < 3; ++j) {
            const TESSindex idx = elems[i * 3 + j];
            if (idx == TESS_UNDEF) break;
            m_land_tris.append(
                QPointF(verts[idx * 2], verts[idx * 2 + 1]));
          }
        }
      }
      // Re-create the tesselator per cell (it accumulates contours).
      tessDeleteTess(tess);
      tess = tessNewTess(nullptr);

      m_coastlines.append(std::move(cell_contours));
    }
  }
  tessDeleteTess(tess);

  m_loaded = !m_land_tris.isEmpty();
  qWarning("GSHHS: %d land contours, %lld triangles in %lld ms", n_contours,
           (long long)(m_land_tris.size() / 3), (long long)timer.elapsed());
}

QSGNode* GshhsWorldProvider::renderChart(QSGNode* old_subtree,
                                         const Viewport& /*viewport*/,
                                         QQuickWindow* /*window*/) {
  // Static geometry -- build the subtree once and reuse it every frame
  // (the viewport transform on the world root reprojects it for free).
  if (old_subtree) return old_subtree;
  if (!m_loaded) return nullptr;

  auto* root = new QSGNode();

  // 1. Sea backdrop: a single quad over the whole world rect, drawn first
  //    (bottom of this subtree) so land + coastline sit on top.
  {
    auto* sea = sg::makeFlatColorNode(m_sea, QSGGeometry::DrawTriangles, 6);
    QSGGeometry::Point2D* v = sea->geometry()->vertexDataAsPoint2D();
    // World rect: x in [-180,180], y=-lat in [-90,90].
    const float xl = -180, xr = 180, yt = -90, yb = 90;
    v[0].set(xl, yt); v[1].set(xr, yt); v[2].set(xr, yb);
    v[3].set(xl, yt); v[4].set(xr, yb); v[5].set(xl, yb);
    root->appendChildNode(sea);
  }

  // 2. Land fill: the tessellated triangle list.
  {
    auto* land = sg::makeFlatColorNode(m_land, QSGGeometry::DrawTriangles,
                                       m_land_tris.size());
    QSGGeometry::Point2D* v = land->geometry()->vertexDataAsPoint2D();
    for (int i = 0; i < m_land_tris.size(); ++i)
      v[i].set(static_cast<float>(m_land_tris[i].x()),
               static_cast<float>(m_land_tris[i].y()));
    root->appendChildNode(land);
  }

  // 3. Coastline outlines: every contour as a closed 1px line loop, batched
  //    into one DrawLines geometry (segment pairs).
  {
    int seg_verts = 0;
    for (const auto& c : m_coastlines)
      if (c.size() >= 2) seg_verts += c.size() * 2;  // closed loop
    if (seg_verts > 0) {
      auto* coast =
          sg::makeFlatColorNode(m_coast, QSGGeometry::DrawLines, seg_verts);
      QSGGeometry::Point2D* v = coast->geometry()->vertexDataAsPoint2D();
      int k = 0;
      for (const auto& c : m_coastlines) {
        const int n = c.size();
        if (n < 2) continue;
        for (int i = 0; i < n; ++i) {
          const QPointF& a = c[i];
          const QPointF& b = c[(i + 1) % n];  // wrap to close
          v[k++].set(static_cast<float>(a.x()), static_cast<float>(a.y()));
          v[k++].set(static_cast<float>(b.x()), static_cast<float>(b.y()));
        }
      }
      root->appendChildNode(coast);
    }
  }

  return root;
}

}  // namespace ocpn::qtui
