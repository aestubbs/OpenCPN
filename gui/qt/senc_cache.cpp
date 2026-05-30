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
 * Implement senc_cache.h.
 */

#include "senc_cache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include "model/base_platform.h"  // g_BasePlatform

namespace ocpn::qtui {

namespace {
// Cache file header: a format magic + the source mtime. BUMP the magic whenever
// the decode/decrypt changes in a way that makes existing cache files wrong --
// it invalidates every stale entry automatically (the alternative, the source
// mtime, never changes for a re-fetched cell). 'SEN' + version.
//   v2 (2026-05): fixed the truncated decrypt read that dropped large trailing
//                 AREA records (missing deep-water polygons).
constexpr quint32 kCacheMagic = 0x53454E32;  // "SEN2"
constexpr qint64 kHeaderLen =
    static_cast<qint64>(sizeof(quint32) + sizeof(qint64));  // magic + mtime
}

SencCache::SencCache() {
  if (!g_BasePlatform) return;
  const QString base =
      QString::fromStdString(g_BasePlatform->GetPrivateDataDir().ToStdString());
  const QString dir = base + QDir::separator() + QStringLiteral("qt_senc_cache");
  if (QDir().mkpath(dir)) m_dir = dir;
}

QString SencCache::fileFor(const QString& cellPath) const {
  const QByteArray h =
      QCryptographicHash::hash(cellPath.toUtf8(), QCryptographicHash::Sha1)
          .toHex();
  return m_dir + QDir::separator() + QString::fromLatin1(h) +
         QStringLiteral(".osenc");
}

QByteArray SencCache::get(const QString& cellPath, qint64 mtime) const {
  if (m_dir.isEmpty()) return {};
  QFile f(fileFor(cellPath));
  if (!f.open(QIODevice::ReadOnly)) return {};
  if (f.size() < kHeaderLen) return {};
  quint32 magic = 0;
  qint64 stored = 0;
  if (f.read(reinterpret_cast<char*>(&magic), sizeof(magic)) !=
      sizeof(magic))
    return {};
  if (magic != kCacheMagic) return {};  // old format -> stale
  if (f.read(reinterpret_cast<char*>(&stored), sizeof(stored)) !=
      sizeof(stored))
    return {};
  if (stored != mtime) return {};  // source changed -> stale
  return f.readAll();
}

void SencCache::put(const QString& cellPath, qint64 mtime,
                    const QByteArray& osenc) {
  if (m_dir.isEmpty() || osenc.isEmpty()) return;
  QSaveFile f(fileFor(cellPath));  // atomic: no torn cache file on crash
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(reinterpret_cast<const char*>(&kCacheMagic), sizeof(kCacheMagic));
  f.write(reinterpret_cast<const char*>(&mtime), sizeof(mtime));
  f.write(osenc);
  f.commit();
}

}  // namespace ocpn::qtui
