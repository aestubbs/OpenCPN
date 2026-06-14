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
 * Implement decoded_cell_cache.h.
 */

#include "decoded_cell_cache.h"

#include <QByteArray>

namespace ocpn::qtui {

namespace {
// Approximate bytes held by a QImage (RGBA).
qint64 imageBytes(const QImage& img) {
  return img.isNull() ? 0 : static_cast<qint64>(img.sizeInBytes());
}
constexpr qint64 kPt = sizeof(QPointF);  // 16 bytes (two doubles)
}  // namespace

qint64 approxDecodedBytes(const s52sg::Buffer& buf) {
  qint64 b = 0;
  for (const s52sg::Prim& p : buf.prims)
    b += p.verts.size() * kPt + 64;  // verts + struct/QList overhead
  for (const s52sg::PatternFill& pf : buf.patternFills)
    b += pf.tris.size() * kPt + imageBytes(pf.pattern) + 64;
  for (const s52sg::Symbol& s : buf.symbols) b += imageBytes(s.image) + 64;
  for (const s52sg::VectorSymbol& vs : buf.vectorSymbols) {
    for (const s52sg::VectorOp& op : vs.ops) b += op.verts.size() * kPt + 32;
    b += 64;
  }
  for (const s52sg::ComplexLine& cl : buf.complexLines) {
    b += cl.path.size() * kPt;
    for (const s52sg::VectorOp& op : cl.symbol) b += op.verts.size() * kPt + 32;
    b += 64;
  }
  for (const s52sg::Label& l : buf.labels)
    b += static_cast<qint64>(l.text.size()) * 2 + 96;
  for (const QList<QPointF>& ring : buf.landContours) b += ring.size() * kPt + 16;
  for (const s52sg::QueryObject& qo : buf.queryObjects) {
    b += qo.shape.size() * kPt;
    for (const s52sg::QueryAttr& a : qo.attrs)
      b += static_cast<qint64>(a.name.size() + a.value.size()) * 2 + 16;
    b += static_cast<qint64>(qo.className.size()) * 2 + 96;
  }
  return b;
}

DecodedCellCache::DecodedCellCache(qint64 budget_bytes)
    : m_budget(budget_bytes > 0 ? budget_bytes : kDefaultBudgetBytes) {
  // Per-process override (e.g. a smaller budget on a Raspberry Pi) without a
  // rebuild: OCPN_QT_CHART_CACHE_MB=64.
  if (budget_bytes <= 0) {
    bool ok = false;
    const qint64 mb =
        qgetenv("OCPN_QT_CHART_CACHE_MB").toLongLong(&ok);
    if (ok && mb > 0) m_budget = mb * 1024 * 1024;
  }
}

void DecodedCellCache::touch(const QString& path) {
  // Move-to-front; the list is short (a region's worth of cells) so the linear
  // removeOne is cheap relative to a decode.
  m_lru.removeOne(path);
  m_lru.prepend(path);
}

bool DecodedCellCache::get(const QString& path, qint64 mtime, Entry* out) {
  auto it = m_map.find(path);
  if (it == m_map.end()) return false;
  if (it->mtime != mtime) {  // source changed on disk -> stale
    m_bytes -= it->bytes;
    m_lru.removeOne(path);
    m_map.erase(it);
    return false;
  }
  if (out) *out = it->entry;
  touch(path);
  return true;
}

void DecodedCellCache::put(const QString& path, qint64 mtime,
                           const Entry& entry) {
  auto it = m_map.find(path);
  if (it != m_map.end()) m_bytes -= it->bytes;  // replacing -- drop old size

  Node node;
  node.mtime = mtime;
  node.entry = entry;
  node.bytes = approxDecodedBytes(entry.buffer);
  m_bytes += node.bytes;
  m_map.insert(path, node);
  touch(path);
  evictToBudget();
}

void DecodedCellCache::evictToBudget() {
  // Never evict the just-inserted MRU entry (front), even if a single cell
  // somehow exceeds the budget -- it is what the caller is about to render.
  while (m_bytes > m_budget && m_lru.size() > 1) {
    const QString victim = m_lru.takeLast();
    auto it = m_map.find(victim);
    if (it != m_map.end()) {
      m_bytes -= it->bytes;
      m_map.erase(it);
    }
  }
}

void DecodedCellCache::clear() {
  m_map.clear();
  m_lru.clear();
  m_bytes = 0;
}

}  // namespace ocpn::qtui
