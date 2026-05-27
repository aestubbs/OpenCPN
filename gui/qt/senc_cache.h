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
 * SencCache -- an on-disk cache of decrypted OSENC ("SENC") byte streams, keyed
 * by source cell path + mtime. o-charts cells are decrypted via the oexserverd
 * FIFO (a per-cell round-trip, the dominant cost of loading a cell); caching
 * the plaintext OSENC means re-viewing a cell (pan/zoom back, or a later
 * session) skips the daemon entirely and only re-runs the cheap OSENC decode.
 *
 * Mirrors OpenCPN's SENC file cache. Files live under a cache directory in the
 * private data dir; each cache file is the decrypted OSENC prefixed by an
 * 8-byte source mtime for invalidation. Used from the chart-worker thread.
 */

#ifndef OCPN_QT_SENC_CACHE_H_
#define OCPN_QT_SENC_CACHE_H_

#include <QByteArray>
#include <QString>

namespace ocpn::qtui {

class SencCache {
public:
  SencCache();

  bool isOpen() const { return !m_dir.isEmpty(); }

  /** Return the cached OSENC bytes for `cellPath` if present and the stored
   *  mtime matches `mtime`; otherwise an empty array (miss). */
  QByteArray get(const QString& cellPath, qint64 mtime) const;

  /** Store `osenc` for `cellPath` tagged with `mtime`. */
  void put(const QString& cellPath, qint64 mtime, const QByteArray& osenc);

private:
  QString fileFor(const QString& cellPath) const;
  QString m_dir;  // cache directory (empty if unavailable)
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SENC_CACHE_H_
