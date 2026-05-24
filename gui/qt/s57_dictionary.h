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
 * S57Dictionary -- decodes S-57 acronyms and enumerated values to human text
 * for the object-query popup (P3.9), from the bundled IHO dictionaries in
 * data/s57data/ (s57objectclasses.csv, s57attributes.csv,
 * s57expectedinput.csv). Loaded once (lazy singleton).
 */

#ifndef OCPN_QT_S57_DICTIONARY_H_
#define OCPN_QT_S57_DICTIONARY_H_

#include <QHash>
#include <QString>

namespace ocpn::qtui {

class S57Dictionary {
public:
  /** Lazily-loaded shared instance (reads the CSVs on first use). */
  static const S57Dictionary& instance();

  /** Object-class description for a class acronym (e.g. "BOYSPP" ->
   *  "Buoy, special purpose/general"), or empty if unknown. */
  QString className(const QString& acronym) const;

  /** Attribute description for an acronym (e.g. "COLOUR" -> "Colour"), or
   *  empty if unknown. */
  QString attrName(const QString& acronym) const;

  /** Human value for an attribute: enumerated/list codes -> their meanings
   *  (joined), other types returned as-is. */
  QString decodeValue(const QString& acronym, const QString& raw_value) const;

private:
  explicit S57Dictionary(const QString& s57data_dir);
  void load(const QString& s57data_dir);

  struct AttrInfo {
    QString desc;
    QChar type;   // E enumerated, L list, I/F/S/A others
    int code = 0; // attribute code, keys into the enum table
  };
  QHash<QString, QString> m_class_desc;          // acronym -> description
  QHash<QString, AttrInfo> m_attr;               // acronym -> info
  QHash<int, QHash<int, QString>> m_enum;        // attr code -> (id -> meaning)
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_S57_DICTIONARY_H_
