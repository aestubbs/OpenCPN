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
 * Cm93Dictionary -- the CM93 -> S-57 lookup tables (P2.19, step (a) of the
 * port plan): object-class names + geometry kinds from CM93OBJ.DIC and
 * attribute names + value types from ATTRLUT.DIC / CM93ATTR.DIC. A pure-Qt
 * extraction of the wx `cm93_dictionary` (gui/src/cm93.cpp:567-906) so the
 * Qt decode pipeline carries no wx stream/tokenizer dependencies. The file
 * formats are pipe-separated; ';' lines are comments.
 */

#ifndef OCPN_QT_CM93_DICTIONARY_H_
#define OCPN_QT_CM93_DICTIONARY_H_

#include <QString>
#include <QStringList>
#include <QVector>

namespace ocpn::qtui {

class Cm93Dictionary {
public:
  Cm93Dictionary() = default;

  /** Load CM93OBJ.DIC + ATTRLUT.DIC (or CM93ATTR.DIC) from `dir` (the
   *  CM93 root or its dictionary folder). Case-insensitive file names. */
  bool load(const QString& dir);
  bool isOk() const { return m_ok; }
  QString dictDir() const { return m_dict_dir; }

  int maxClass() const { return m_class_names.size() - 1; }
  /** S-57-style class name for a CM93 class number ("NULLNM" if unknown). */
  QString className(int iclass) const {
    return iclass >= 0 && iclass < m_class_names.size()
               ? m_class_names[iclass]
               : QStringLiteral("NULLNM");
  }
  /** Geometry kind per class: 1 point, 2 line, 3 area, -1 unknown. */
  int geomType(int iclass) const {
    return iclass >= 0 && iclass < m_geom_types.size() ? m_geom_types[iclass]
                                                       : -1;
  }

  int maxAttr() const { return m_attr_names.size() - 1; }
  QString attrName(int iattr) const {
    return iattr >= 0 && iattr < m_attr_names.size()
               ? m_attr_names[iattr]
               : QStringLiteral("NULLNM");
  }
  /** Attribute value type: R float, B byte, S string, C complex, L list,
   *  W word10, G long, '?' unknown. */
  char attrValueType(int iattr) const {
    return iattr >= 0 && iattr < m_val_types.size() ? m_val_types[iattr] : '?';
  }

private:
  bool loadClasses(const QString& path);
  bool loadAttrsLut(const QString& path);   // ATTRLUT.DIC (type at column 7)
  bool loadAttrsCm93(const QString& path);  // CM93ATTR.DIC (type at column 3)
  static char valueTypeFor(const QString& token);

  QStringList m_class_names;  // index = CM93 class number
  QVector<int> m_geom_types;
  QStringList m_attr_names;   // index = CM93 attribute number
  QVector<char> m_val_types;
  QString m_dict_dir;
  bool m_ok = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CM93_DICTIONARY_H_
