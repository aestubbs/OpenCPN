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
 * Header-only convenience wrappers around the Qt-idiomatic OcpnConfig API
 * (QString/QVariant based) that present a wxString-friendly façade for the
 * GUI call sites that historically used the wxConfig-style API. These do
 * not extend the OcpnConfig public surface -- they only exist so that the
 * per-call rewrites stay one-liners and avoid scattering
 * QString_to_wxString / wxString_to_QString conversion noise throughout
 * the GUI code.
 *
 * Read overloads honour the wx semantic of leaving `out` unchanged when
 * the key is missing (rather than overwriting with a default).
 */

#ifndef CONFIG_COMPAT_HELPERS_H_
#define CONFIG_COMPAT_HELPERS_H_

#include <QString>
#include <QStringList>
#include <QVariant>

#include <wx/string.h>

#include "model/ocpn_config.h"
#include "model/wx_qt_string.h"

namespace ocpn_cfg {

// --- Read variants --------------------------------------------------------
inline void CfgRead(OcpnConfig &c, const char *key, wxString *out) {
  const QString qk(key);
  if (c.contains(qk)) *out = QString_to_wxString(c.value(qk).toString());
}
inline void CfgRead(OcpnConfig &c, const char *key, wxString *out,
                    const wxString &def) {
  *out = QString_to_wxString(
      c.value(QString(key), wxString_to_QString(def)).toString());
}
inline void CfgRead(OcpnConfig &c, const char *key, int *out) {
  const QString qk(key);
  if (c.contains(qk)) *out = c.value(qk).toInt();
}
inline void CfgRead(OcpnConfig &c, const char *key, int *out, int def) {
  *out = c.value(QString(key), def).toInt();
}
inline void CfgRead(OcpnConfig &c, const char *key, long *out) {
  const QString qk(key);
  if (c.contains(qk))
    *out = static_cast<long>(c.value(qk).toLongLong());
}
inline void CfgRead(OcpnConfig &c, const char *key, long *out, long def) {
  *out = static_cast<long>(
      c.value(QString(key), qlonglong(def)).toLongLong());
}
inline void CfgRead(OcpnConfig &c, const char *key, bool *out) {
  const QString qk(key);
  if (!c.contains(qk)) return;
  const QString s = c.value(qk).toString().trimmed();
  if (s == "0" || s.compare("false", Qt::CaseInsensitive) == 0)
    *out = false;
  else if (s == "1" || s.compare("true", Qt::CaseInsensitive) == 0)
    *out = true;
  else
    *out = c.value(qk).toBool();
}
inline void CfgRead(OcpnConfig &c, const char *key, bool *out, bool def) {
  const QString qk(key);
  if (!c.contains(qk)) {
    *out = def;
    return;
  }
  CfgRead(c, key, out);
}
inline void CfgRead(OcpnConfig &c, const char *key, double *out) {
  const QString qk(key);
  if (c.contains(qk)) *out = c.value(qk).toDouble();
}
inline void CfgRead(OcpnConfig &c, const char *key, double *out, double def) {
  *out = c.value(QString(key), def).toDouble();
}
inline void CfgRead(OcpnConfig &c, const char *key, float *out) {
  const QString qk(key);
  if (c.contains(qk)) *out = c.value(qk).toFloat();
}
inline void CfgRead(OcpnConfig &c, const char *key, float *out, float def) {
  *out = c.value(QString(key), def).toFloat();
}

// wxString-key flavours (typically used by loop bodies).
inline void CfgRead(OcpnConfig &c, const wxString &key, wxString *out) {
  CfgRead(c, key.mb_str().data(), out);
}
inline void CfgRead(OcpnConfig &c, const wxString &key, long *out) {
  CfgRead(c, key.mb_str().data(), out);
}

inline wxString CfgReadStr(OcpnConfig &c, const wxString &key) {
  const QString qk = wxString_to_QString(key);
  if (c.contains(qk))
    return QString_to_wxString(c.value(qk).toString());
  return wxString();
}

// Bool-returning conditional Read: true if the key existed.
inline bool CfgReadIf(OcpnConfig &c, const char *key, wxString *out) {
  const QString qk(key);
  if (!c.contains(qk)) return false;
  *out = QString_to_wxString(c.value(qk).toString());
  return true;
}
inline bool CfgReadIf(OcpnConfig &c, const char *key, int *out) {
  const QString qk(key);
  if (!c.contains(qk)) return false;
  *out = c.value(qk).toInt();
  return true;
}
inline bool CfgReadIf(OcpnConfig &c, const char *key, bool *out) {
  const QString qk(key);
  if (!c.contains(qk)) return false;
  CfgRead(c, key, out);
  return true;
}
inline bool CfgReadIf(OcpnConfig &c, const char *key, double *out,
                      double def) {
  const QString qk(key);
  if (!c.contains(qk)) {
    *out = def;
    return false;
  }
  *out = c.value(qk).toDouble();
  return true;
}

// --- Write variants -------------------------------------------------------
inline void CfgWrite(OcpnConfig &c, const char *key, const wxString &v) {
  c.setValue(QString(key), wxString_to_QString(v));
}
inline void CfgWrite(OcpnConfig &c, const char *key, const char *v) {
  c.setValue(QString(key), QString(v ? v : ""));
}
inline void CfgWrite(OcpnConfig &c, const char *key, int v) {
  c.setValue(QString(key), v);
}
inline void CfgWrite(OcpnConfig &c, const char *key, unsigned v) {
  c.setValue(QString(key), static_cast<qulonglong>(v));
}
inline void CfgWrite(OcpnConfig &c, const char *key, long v) {
  c.setValue(QString(key), static_cast<qlonglong>(v));
}
inline void CfgWrite(OcpnConfig &c, const char *key, bool v) {
  c.setValue(QString(key), v ? 1 : 0);
}
inline void CfgWrite(OcpnConfig &c, const char *key, double v) {
  c.setValue(QString(key), v);
}
inline void CfgWrite(OcpnConfig &c, const wxString &key, const wxString &v) {
  c.setValue(wxString_to_QString(key), wxString_to_QString(v));
}
inline void CfgWrite(OcpnConfig &c, const wxString &key, int v) {
  c.setValue(wxString_to_QString(key), v);
}
inline void CfgWrite(OcpnConfig &c, const wxString &key, long v) {
  c.setValue(wxString_to_QString(key), static_cast<qlonglong>(v));
}
inline void CfgWrite(OcpnConfig &c, const wxString &key, unsigned v) {
  c.setValue(wxString_to_QString(key), static_cast<qulonglong>(v));
}
inline void CfgWrite(OcpnConfig &c, const wxString &key, bool v) {
  c.setValue(wxString_to_QString(key), v ? 1 : 0);
}
inline void CfgWrite(OcpnConfig &c, const wxString &key, double v) {
  c.setValue(wxString_to_QString(key), v);
}

// --- Group / entry helpers ------------------------------------------------
inline bool CfgHasEntry(OcpnConfig &c, const char *key) {
  return c.contains(QString(key));
}
inline bool CfgHasEntry(OcpnConfig &c, const wxString &key) {
  return c.contains(wxString_to_QString(key));
}
inline bool CfgHasGroup(OcpnConfig &c, const char *name) {
  return c.childGroups().contains(QString(name));
}
inline bool CfgHasGroup(OcpnConfig &c, const wxString &name) {
  return c.childGroups().contains(wxString_to_QString(name));
}
inline void CfgDelete(OcpnConfig &c, const char *key) {
  c.remove(QString(key));
}
inline void CfgDelete(OcpnConfig &c, const wxString &key) {
  c.remove(wxString_to_QString(key));
}

}  // namespace ocpn_cfg

// Inject into the global namespace for unqualified call sites.
using ocpn_cfg::CfgRead;
using ocpn_cfg::CfgReadStr;
using ocpn_cfg::CfgReadIf;
using ocpn_cfg::CfgWrite;
using ocpn_cfg::CfgHasEntry;
using ocpn_cfg::CfgHasGroup;
using ocpn_cfg::CfgDelete;

#endif  // CONFIG_COMPAT_HELPERS_H_
