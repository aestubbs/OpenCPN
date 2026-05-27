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
constexpr qint64 kMtimeHeader = static_cast<qint64>(sizeof(qint64));  // 8 bytes
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
  if (f.size() < kMtimeHeader) return {};
  qint64 stored = 0;
  if (f.read(reinterpret_cast<char*>(&stored), kMtimeHeader) != kMtimeHeader)
    return {};
  if (stored != mtime) return {};  // source changed -> stale
  return f.readAll();
}

void SencCache::put(const QString& cellPath, qint64 mtime,
                    const QByteArray& osenc) {
  if (m_dir.isEmpty() || osenc.isEmpty()) return;
  QSaveFile f(fileFor(cellPath));  // atomic: no torn cache file on crash
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(reinterpret_cast<const char*>(&mtime), kMtimeHeader);
  f.write(osenc);
  f.commit();
}

}  // namespace ocpn::qtui
