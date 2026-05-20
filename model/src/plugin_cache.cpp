/***************************************************************************
 *   Copyright (C) 2019 Alec Leamas                                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 * \file
 *
 * Implement plugin_cache.h -- downloaded plugins cache
 */

#include <fstream>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "model/base_platform.h"
#include "model/ocpn_utils.h"
#include "model/plugin_cache.h"
#include "model/wx_qt_string.h"

#ifdef __ANDROID__
#include "androidUTIL.h"
#endif

static QString cache_path() {
  QString path = wxString_to_QString(g_BasePlatform->GetPrivateDataDir());
  path += QDir::separator();
  path += "plugins";
  path += QDir::separator();
  path += "cache";
  return path;
}

static std::string tarball_path(const char* basename, bool create = false) {
  QString dir = cache_path() + QDir::separator() + "tarballs";
  if (create) {
    QDir().mkpath(dir);
  }
  QString path = dir + QDir::separator() + QString::fromUtf8(basename);
  return path.toStdString();
}

static bool copy_file(const char* src_path, const char* dest_path) {
#ifdef __ANDROID__
  return AndroidSecureCopyFile(src_path, dest_path);
#else
  // wxCopyFile semantics overwrites destination -- match that.
  QString src = QString::fromUtf8(src_path);
  QString dst = QString::fromUtf8(dest_path);
  if (QFile::exists(dst)) QFile::remove(dst);
  return QFile::copy(src, dst);
#endif
}

static std::string get_basename(const char* path) {
  QString sep(QDir::separator());
  // To parse standard network url, use "/"
  if (ocpn::startswith(path, "http")) sep = "/";
  auto parts = ocpn::split(path, sep.toStdString());
  return parts[parts.size() - 1];
}

namespace ocpn {

static std::string metadata_path(const char* basename, bool create = false) {
  QString dir = cache_path() + QDir::separator() + "metadata";
  if (create) {
    QDir().mkpath(dir);
  }
  QString path = dir + QDir::separator() + QString::fromUtf8(basename);
  return path.toStdString();
}

bool store_metadata(const char* path) {
  auto name = get_basename(path);
  std::string dest = metadata_path(name.c_str(), true);
  bool ok = ::copy_file(path, dest.c_str());
  wxLogDebug("Storing metadata %s at %s: %s", path, dest.c_str(),
             ok ? "ok" : "fail");
  return ok;
}

std::string lookup_metadata(const char* name) {
  if (name == 0) {
    name = "ocpn-plugins.xml";
  }
  auto path = metadata_path(name);
  return ocpn::exists(path) ? path : "";
}

bool store_tarball(const char* path, const char* basename) {
  std::string dest = tarball_path(basename, true);
  bool ok = ::copy_file(path, dest.c_str());
  wxLogDebug("Storing tarball %s at %s: %s", path, dest.c_str(),
             ok ? "ok" : "fail");
  return ok;
}

std::string lookup_tarball(const char* uri) {
  std::string basename = get_basename(uri);
  std::string path = tarball_path(basename.c_str());
  return ocpn::exists(path) ? path : "";
}

unsigned cache_file_count() {
  QString dir = cache_path() + QDir::separator() + "tarballs";
  if (!QDir(dir).exists()) {
    return 0;
  }
  QDir d(dir);
  return static_cast<unsigned>(
      d.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size());
}

unsigned long cache_size() {
  QString dir_path = cache_path() + QDir::separator() + "tarballs";
  if (!QDir(dir_path).exists()) {
    return 0;
  }
  qint64 total = 0;
  QDir d(dir_path);
  const QStringList entries =
      d.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
  for (const QString& file : entries) {
    QFileInfo fi(d.filePath(file));
    if (fi.isFile()) {  // Consider only regular files
      total += fi.size();
    }
  }
  total /= (1024 * 1024);
  return static_cast<unsigned long>(total);
}

/* mock up definitions.*/
void cache_clear() {
  QString dir_path = cache_path() + QDir::separator() + "tarballs";
  if (!QDir(dir_path).exists()) {
    return;
  }
  QDir d(dir_path);
  const QStringList entries =
      d.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
  for (const QString& file : entries) {
    QFile::remove(d.filePath(file));
  }
}

}  // namespace ocpn
