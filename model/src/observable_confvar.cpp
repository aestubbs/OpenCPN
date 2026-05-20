/*************************************************************************
 *
 * Project: OpenCPN
 * Purpose: Implement observable_confvar.h
 *
 * Copyright (C) 2022 Alec Leamas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.
 **************************************************************************/

/**
 * \file
 *
 * Implementation of the ConfigVar<T> template wrapper. Lives in the
 * `model` library (rather than `observable`) because it depends on
 * OcpnConfig -- and OcpnConfig itself lives in model. Keeping the
 * implementation here avoids an upward dependency from observable on
 * model.
 */

#include <sstream>
#include <string>

#include <wx/log.h>
#include <wx/string.h>

#include <QString>

#include "observable_confvar.h"

#include "model/ocpn_config.h"

/**
 * Add >> support for wxString, for some reason missing in wxWidgets 3.0,
 * required by ConfigVar::Get().
 */
std::istream& operator>>(std::istream& input, wxString& ws) {
  std::string s;
  input >> s;
  ws.Append(s);
  return input;
}

template <typename T>
ConfigVar<T>::ConfigVar(const std::string& section_, const std::string& key_,
                        OcpnConfig* cb)
    : Observable(section_ + "/" + key_),
      section(section_),
      key(key_),
      config(cb) {}

template <typename T>
const T ConfigVar<T>::Get(const T& default_val) {
  std::istringstream iss;
  config->endAllGroups();
  config->beginGroup(QString::fromStdString(section));
  const QString value =
      config->value(QString::fromStdString(key), QString()).toString();
  config->endGroup();
  iss.str(value.toStdString());
  T r;
  iss >> r;
  return iss.fail() ? default_val : r;
}

template <typename T>
void ConfigVar<T>::Set(const T& arg) {
  std::ostringstream oss;
  oss << arg;
  if (oss.fail()) {
    wxLogWarning("Cannot dump failed buffer for key %s:%s", section.c_str(),
                 key.c_str());
    return;
  }
  config->endAllGroups();
  config->beginGroup(QString::fromStdString(section));
  config->setValue(QString::fromStdString(key),
                   QString::fromStdString(oss.str()));
  config->endGroup();
  Observable::Notify();
}

/* Explicitly instantiate the ConfigVar types supported. */
template class ConfigVar<bool>;
template class ConfigVar<double>;
template class ConfigVar<int>;
template class ConfigVar<std::string>;
template class ConfigVar<wxString>;
