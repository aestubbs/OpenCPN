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
 * OcpnConfig -- Qt-based application configuration object replacing the
 * historical wxFileConfig / wxConfigBase used throughout OpenCPN.
 *
 * The class provides two parallel APIs:
 *
 * 1. A Qt-idiomatic API (QString/QVariant based, lower-case verbs) that
 *    mirrors QSettings and is preferred for new code.
 *
 * 2. A wxConfig-compatibility shim (wxString based, capitalised verbs) that
 *    keeps the 800+ existing Read/Write/SetPath/etc. call sites in
 *    `navutil.cpp` and friends working unchanged during the migration.
 *
 * The shim layer is scheduled for removal in step 3 of P1.9 once the wx-style
 * call sites have been ported. Until then the two APIs operate over the same
 * underlying QSettings(INI) backing store.
 *
 * Notes on the wxConfig -> QSettings semantic gap:
 *  - wxFileConfig::Read(key, &out, def) returns `false` if the key is missing
 *    and leaves *out set to the default. QSettings::value() returns the
 *    default unconditionally. The shim adapts the Qt behaviour back to the
 *    wx contract.
 *  - wxFileConfig has a single "current path" (slash-separated, with `/`,
 *    `..` and relative semantics). QSettings uses a beginGroup/endGroup
 *    stack. The shim translates a wxConfig path into an explicit group stack.
 *  - INI key escaping rules differ slightly between wxFileConfig and
 *    QSettings; verified empirically that round-tripping existing user
 *    configs works for the keys OpenCPN actually uses (no embedded `=`,
 *    `/`, control chars, etc.).
 */

#ifndef OCPN_CONFIG_H_
#define OCPN_CONFIG_H_

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <wx/string.h>

class OcpnConfig {
public:
  /** Construct backed by an INI-format QSettings file at `filePath`. */
  explicit OcpnConfig(const QString& filePath);
  virtual ~OcpnConfig();

  OcpnConfig(const OcpnConfig&) = delete;
  OcpnConfig& operator=(const OcpnConfig&) = delete;

  // ---------------------------------------------------------------------
  //  Qt-idiomatic API (preferred for new code)
  // ---------------------------------------------------------------------
  QVariant value(const QString& key,
                 const QVariant& defaultValue = QVariant()) const;
  void setValue(const QString& key, const QVariant& value);
  bool contains(const QString& key) const;
  void remove(const QString& key);

  void beginGroup(const QString& prefix);
  void endGroup();
  QString group() const;

  QStringList childKeys() const;
  QStringList childGroups() const;

  void sync();

  // ---------------------------------------------------------------------
  //  wxConfig-compat shim API (existing call sites; removed in step 3).
  //
  //  All overloads honour the wx semantics:
  //    - Read(key, &out, def): if key missing, *out = def, return false.
  //                            if key present,  *out = value, return true.
  //    - Read(key, def):       returns value or def.
  //    - Write(...):           returns true on success.
  // ---------------------------------------------------------------------
  bool Read(const wxString& key, wxString* str,
            const wxString& defaultVal = wxString());
  bool Read(const wxString& key, int* i, int defaultVal = 0);
  bool Read(const wxString& key, long* l, long defaultVal = 0);
  bool Read(const wxString& key, bool* b, bool defaultVal = false);
  bool Read(const wxString& key, double* d, double defaultVal = 0.0);
  bool Read(const wxString& key, float* f, float defaultVal = 0.0f);

  // Convenience read-into-return overloads.
  wxString Read(const wxString& key,
                const wxString& defaultVal = wxString()) const;
  // Disambiguated long overload: wxConfig provides Read(key, long_default)
  // but with a wxString-default overload the literal `0` is ambiguous.
  // Callers in the codebase pass long literals explicitly when needed.
  long Read(const wxString& key, long defaultVal) const;

  bool Write(const wxString& key, const wxString& value);
  bool Write(const wxString& key, const char* value);
  bool Write(const wxString& key, int value);
  bool Write(const wxString& key, unsigned value);
  bool Write(const wxString& key, long value);
  bool Write(const wxString& key, bool value);
  bool Write(const wxString& key, double value);

  bool SetPath(const wxString& strPath);
  wxString GetPath() const;

  bool Flush(bool currentOnly = false) {
    (void)currentOnly;
    sync();
    return true;
  }

  bool HasGroup(const wxString& strName) const;
  bool HasEntry(const wxString& strName) const;

  bool DeleteEntry(const wxString& key, bool bDeleteGroupIfEmpty = true);
  bool DeleteGroup(const wxString& key);

  size_t GetNumberOfEntries(bool bRecursive = false) const;
  size_t GetNumberOfGroups(bool bRecursive = false) const;

  // wxConfig-style enumeration. Pattern:
  //   long cookie;
  //   bool more = cfg.GetFirstEntry(name, cookie);
  //   while (more) { ...; more = cfg.GetNextEntry(name, cookie); }
  bool GetFirstGroup(wxString& str, long& lIndex) const;
  bool GetNextGroup(wxString& str, long& lIndex) const;
  bool GetFirstEntry(wxString& str, long& lIndex) const;
  bool GetNextEntry(wxString& str, long& lIndex) const;

private:
  // Internal helper: rebuild the cached child group / entry lists used by
  // the GetFirst/Next enumeration API.
  void RebuildCaches() const;

  // Apply a wxConfig-style path (absolute, relative, with `..` and `/`)
  // by popping all current groups then pushing the resolved segments.
  void ApplyAbsolutePath(const QStringList& segments);

  QSettings m_settings;

  // Current path as a list of segments (top-level first). Mirrors the
  // QSettings group stack so we can implement GetPath() and SetPath().
  QStringList m_path_segments;

  mutable QStringList m_cached_groups;
  mutable QStringList m_cached_entries;
  mutable bool m_caches_dirty = true;
};

#endif  // OCPN_CONFIG_H_
