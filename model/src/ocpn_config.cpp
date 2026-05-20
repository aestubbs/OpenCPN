/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
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
 * Implement OcpnConfig.
 */

#include "model/ocpn_config.h"

#include "model/wx_qt_string.h"

namespace {

// Translate a wxConfig-style path expression ("/Foo/Bar", "Bar/Baz", "..",
// "/") into an absolute list of segments. `current` is the current absolute
// path. Empty segments (from leading `/` or repeated `/`) are dropped, and
// ".." pops one level.
QStringList ResolvePath(const QStringList& current, const QString& path) {
  QStringList out;
  if (path.startsWith('/')) {
    // absolute
  } else {
    out = current;
  }
  const QStringList parts = path.split('/', Qt::SkipEmptyParts);
  for (const QString& part : parts) {
    if (part == ".") continue;
    if (part == "..") {
      if (!out.isEmpty()) out.removeLast();
      continue;
    }
    out.append(part);
  }
  return out;
}

}  // namespace

OcpnConfig::OcpnConfig(const QString& filePath)
    : m_settings(filePath, QSettings::IniFormat) {}

OcpnConfig::~OcpnConfig() { m_settings.sync(); }

// --- Qt-idiomatic API ------------------------------------------------------

QVariant OcpnConfig::value(const QString& key,
                           const QVariant& defaultValue) const {
  return m_settings.value(key, defaultValue);
}

void OcpnConfig::setValue(const QString& key, const QVariant& value) {
  m_settings.setValue(key, value);
  m_caches_dirty = true;
}

bool OcpnConfig::contains(const QString& key) const {
  return m_settings.contains(key);
}

void OcpnConfig::remove(const QString& key) {
  m_settings.remove(key);
  m_caches_dirty = true;
}

void OcpnConfig::beginGroup(const QString& prefix) {
  m_settings.beginGroup(prefix);
  m_path_segments.append(prefix);
  m_caches_dirty = true;
}

void OcpnConfig::endGroup() {
  m_settings.endGroup();
  if (!m_path_segments.isEmpty()) m_path_segments.removeLast();
  m_caches_dirty = true;
}

QString OcpnConfig::group() const { return m_settings.group(); }

QStringList OcpnConfig::childKeys() const { return m_settings.childKeys(); }

QStringList OcpnConfig::childGroups() const {
  return m_settings.childGroups();
}

void OcpnConfig::sync() { m_settings.sync(); }

// --- wxConfig-compat path management --------------------------------------

void OcpnConfig::ApplyAbsolutePath(const QStringList& segments) {
  // Pop everything we have begun, then push the new segments.
  while (!m_path_segments.isEmpty()) {
    m_settings.endGroup();
    m_path_segments.removeLast();
  }
  for (const QString& seg : segments) {
    if (seg.isEmpty()) continue;
    m_settings.beginGroup(seg);
    m_path_segments.append(seg);
  }
  m_caches_dirty = true;
}

bool OcpnConfig::SetPath(const wxString& strPath) {
  const QString path = wxString_to_QString(strPath);
  const QStringList resolved = ResolvePath(m_path_segments, path);
  ApplyAbsolutePath(resolved);
  return true;
}

wxString OcpnConfig::GetPath() const {
  if (m_path_segments.isEmpty()) return wxString();
  // wxConfig::GetPath returns "/a/b" with no trailing slash, "" at root.
  QString joined = "/" + m_path_segments.join('/');
  return QString_to_wxString(joined);
}

// --- wxConfig-compat Read overloads ---------------------------------------

bool OcpnConfig::Read(const wxString& key, wxString* str,
                      const wxString& defaultVal) {
  const QString qkey = wxString_to_QString(key);
  if (m_settings.contains(qkey)) {
    *str = QString_to_wxString(m_settings.value(qkey).toString());
    return true;
  }
  *str = defaultVal;
  return false;
}

bool OcpnConfig::Read(const wxString& key, int* i, int defaultVal) {
  const QString qkey = wxString_to_QString(key);
  if (m_settings.contains(qkey)) {
    *i = m_settings.value(qkey).toInt();
    return true;
  }
  *i = defaultVal;
  return false;
}

bool OcpnConfig::Read(const wxString& key, long* l, long defaultVal) {
  const QString qkey = wxString_to_QString(key);
  if (m_settings.contains(qkey)) {
    // Use toLongLong() then cast: `long` is 32-bit on Windows.
    *l = static_cast<long>(m_settings.value(qkey).toLongLong());
    return true;
  }
  *l = defaultVal;
  return false;
}

bool OcpnConfig::Read(const wxString& key, bool* b, bool defaultVal) {
  const QString qkey = wxString_to_QString(key);
  if (m_settings.contains(qkey)) {
    const QVariant v = m_settings.value(qkey);
    // wxFileConfig stores bools as "0"/"1"; QVariant::toBool of the string
    // "0" returns false, "1"/"true" return true. Cover both INI styles.
    const QString s = v.toString().trimmed();
    if (s == "0" || s.compare("false", Qt::CaseInsensitive) == 0) {
      *b = false;
    } else if (s == "1" || s.compare("true", Qt::CaseInsensitive) == 0) {
      *b = true;
    } else {
      *b = v.toBool();
    }
    return true;
  }
  *b = defaultVal;
  return false;
}

bool OcpnConfig::Read(const wxString& key, double* d, double defaultVal) {
  const QString qkey = wxString_to_QString(key);
  if (m_settings.contains(qkey)) {
    *d = m_settings.value(qkey).toDouble();
    return true;
  }
  *d = defaultVal;
  return false;
}

bool OcpnConfig::Read(const wxString& key, float* f, float defaultVal) {
  const QString qkey = wxString_to_QString(key);
  if (m_settings.contains(qkey)) {
    *f = m_settings.value(qkey).toFloat();
    return true;
  }
  *f = defaultVal;
  return false;
}

wxString OcpnConfig::Read(const wxString& key,
                          const wxString& defaultVal) const {
  const QString qkey = wxString_to_QString(key);
  if (m_settings.contains(qkey)) {
    return QString_to_wxString(m_settings.value(qkey).toString());
  }
  return defaultVal;
}

long OcpnConfig::Read(const wxString& key, long defaultVal) const {
  const QString qkey = wxString_to_QString(key);
  if (m_settings.contains(qkey)) {
    return static_cast<long>(m_settings.value(qkey).toLongLong());
  }
  return defaultVal;
}

// --- wxConfig-compat Write overloads --------------------------------------

bool OcpnConfig::Write(const wxString& key, const wxString& value) {
  m_settings.setValue(wxString_to_QString(key),
                      QVariant(wxString_to_QString(value)));
  m_caches_dirty = true;
  return true;
}

bool OcpnConfig::Write(const wxString& key, const char* value) {
  return Write(key, wxString(value ? value : ""));
}

bool OcpnConfig::Write(const wxString& key, int value) {
  m_settings.setValue(wxString_to_QString(key), value);
  m_caches_dirty = true;
  return true;
}

bool OcpnConfig::Write(const wxString& key, unsigned value) {
  m_settings.setValue(wxString_to_QString(key),
                      static_cast<qulonglong>(value));
  m_caches_dirty = true;
  return true;
}

bool OcpnConfig::Write(const wxString& key, long value) {
  m_settings.setValue(wxString_to_QString(key),
                      static_cast<qlonglong>(value));
  m_caches_dirty = true;
  return true;
}

bool OcpnConfig::Write(const wxString& key, bool value) {
  // Persist booleans as "0"/"1" to remain compatible with the legacy
  // wxFileConfig INI layout (existing user configs use that form).
  m_settings.setValue(wxString_to_QString(key), value ? 1 : 0);
  m_caches_dirty = true;
  return true;
}

bool OcpnConfig::Write(const wxString& key, double value) {
  m_settings.setValue(wxString_to_QString(key), value);
  m_caches_dirty = true;
  return true;
}

// --- wxConfig-compat group / entry queries --------------------------------

bool OcpnConfig::HasGroup(const wxString& strName) const {
  return m_settings.childGroups().contains(wxString_to_QString(strName));
}

bool OcpnConfig::HasEntry(const wxString& strName) const {
  return m_settings.contains(wxString_to_QString(strName));
}

bool OcpnConfig::DeleteEntry(const wxString& key, bool /*bDeleteGroupIfEmpty*/) {
  const QString qkey = wxString_to_QString(key);
  if (!m_settings.contains(qkey)) return false;
  m_settings.remove(qkey);
  m_caches_dirty = true;
  return true;
}

bool OcpnConfig::DeleteGroup(const wxString& key) {
  const QString qkey = wxString_to_QString(key);
  // QSettings::remove() on a group prefix removes all keys in that group.
  m_settings.remove(qkey);
  m_caches_dirty = true;
  return true;
}

size_t OcpnConfig::GetNumberOfEntries(bool bRecursive) const {
  (void)bRecursive;  // The 2 callers in navutil.cpp use the non-recursive form.
  return static_cast<size_t>(m_settings.childKeys().size());
}

size_t OcpnConfig::GetNumberOfGroups(bool bRecursive) const {
  (void)bRecursive;
  return static_cast<size_t>(m_settings.childGroups().size());
}

// --- wxConfig-compat enumeration ------------------------------------------

void OcpnConfig::RebuildCaches() const {
  m_cached_groups = m_settings.childGroups();
  m_cached_entries = m_settings.childKeys();
  m_caches_dirty = false;
}

bool OcpnConfig::GetFirstGroup(wxString& str, long& lIndex) const {
  RebuildCaches();
  lIndex = 0;
  if (m_cached_groups.isEmpty()) return false;
  str = QString_to_wxString(m_cached_groups.at(0));
  lIndex = 1;
  return true;
}

bool OcpnConfig::GetNextGroup(wxString& str, long& lIndex) const {
  if (m_caches_dirty) RebuildCaches();
  if (lIndex < 0 || lIndex >= m_cached_groups.size()) return false;
  str = QString_to_wxString(m_cached_groups.at(static_cast<int>(lIndex)));
  ++lIndex;
  return true;
}

bool OcpnConfig::GetFirstEntry(wxString& str, long& lIndex) const {
  RebuildCaches();
  lIndex = 0;
  if (m_cached_entries.isEmpty()) return false;
  str = QString_to_wxString(m_cached_entries.at(0));
  lIndex = 1;
  return true;
}

bool OcpnConfig::GetNextEntry(wxString& str, long& lIndex) const {
  if (m_caches_dirty) RebuildCaches();
  if (lIndex < 0 || lIndex >= m_cached_entries.size()) return false;
  str = QString_to_wxString(m_cached_entries.at(static_cast<int>(lIndex)));
  ++lIndex;
  return true;
}
