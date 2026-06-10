/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "cm93_dictionary.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

namespace ocpn::qtui {

namespace {

// Read a pipe-separated dictionary file, skipping ';' comment lines.
// Returns false if the file can't be opened.
bool readDicLines(const QString& path, QStringList* lines) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
  QTextStream in(&f);
  while (!in.atEnd()) {
    const QString line = in.readLine();
    if (line.startsWith(';') || line.trimmed().isEmpty()) continue;
    lines->append(line);
  }
  return true;
}

// Case-insensitive existence: prefer the upper-case name (the CM93 sets
// ship either way), falling back to lower-case.
QString findDicFile(const QString& dir, const QString& upper) {
  const QString up = dir + upper;
  if (QFile::exists(up)) return up;
  const QString low = dir + upper.toLower();
  if (QFile::exists(low)) return low;
  return {};
}

}  // namespace

char Cm93Dictionary::valueTypeFor(const QString& token) {
  const QString t = token.trimmed();
  if (t == QStringLiteral("aFLOAT")) return 'R';
  if (t == QStringLiteral("aBYTE")) return 'B';
  if (t == QStringLiteral("aSTRING")) return 'S';
  if (t == QStringLiteral("aCMPLX")) return 'C';
  if (t == QStringLiteral("aLIST")) return 'L';
  if (t == QStringLiteral("aWORD10")) return 'W';
  if (t == QStringLiteral("aLONG")) return 'G';
  return '?';
}

bool Cm93Dictionary::loadClasses(const QString& path) {
  QStringList lines;
  if (!readDicLines(path, &lines)) return false;

  // Pass 1: the highest class number sizes the arrays.
  int iclass_max = 0;
  for (const QString& line : lines) {
    const QStringList tok = line.split('|');
    if (tok.size() < 3) continue;
    iclass_max = qMax(iclass_max, tok[1].toInt());
  }
  if (iclass_max <= 0) return false;

  m_class_names = QStringList();
  m_geom_types = QVector<int>(iclass_max + 1, -1);
  for (int k = 0; k <= iclass_max; ++k)
    m_class_names.append(QStringLiteral("NULLNM"));

  // Pass 2: fill. Line: CLASSNAME|number|geomtype(...)
  for (const QString& line : lines) {
    const QStringList tok = line.split('|');
    if (tok.size() < 3) continue;
    const int iclass = tok[1].toInt();
    if (iclass < 0 || iclass > iclass_max) continue;
    m_class_names[iclass] = tok[0];
    // Primary geometry letter only ('A' area, 'L' line, 'P' point); other
    // letters / compound wants are ignored, like the wx loader.
    const QChar g = tok[2].isEmpty() ? QChar('?') : tok[2][0];
    m_geom_types[iclass] = g == 'A' ? 3 : g == 'L' ? 2 : g == 'P' ? 1 : -1;
  }
  return true;
}

bool Cm93Dictionary::loadAttrsLut(const QString& path) {
  QStringList lines;
  if (!readDicLines(path, &lines)) return false;
  int iattr_max = 0;
  for (const QString& line : lines) {
    const QStringList tok = line.split('|');
    if (tok.size() < 2) continue;
    iattr_max = qMax(iattr_max, tok[1].toInt());
  }
  if (iattr_max <= 0) return false;
  m_attr_names = QStringList();
  m_val_types = QVector<char>(iattr_max + 1, '?');
  for (int k = 0; k <= iattr_max; ++k)
    m_attr_names.append(QStringLiteral("NULLNM"));
  // Line: ATTRNAME|number|x|x|x|x|aTYPE
  for (const QString& line : lines) {
    const QStringList tok = line.split('|');
    if (tok.size() < 7) continue;
    const int iattr = tok[1].toInt();
    if (iattr < 0 || iattr > iattr_max) continue;
    m_attr_names[iattr] = tok[0];
    m_val_types[iattr] = valueTypeFor(tok[6]);
  }
  return true;
}

bool Cm93Dictionary::loadAttrsCm93(const QString& path) {
  QStringList lines;
  if (!readDicLines(path, &lines)) return false;
  int iattr_max = 0;
  for (const QString& line : lines) {
    const QStringList tok = line.split('|');
    if (tok.size() < 2) continue;
    iattr_max = qMax(iattr_max, tok[1].toInt());
  }
  if (iattr_max <= 0) return false;
  m_attr_names = QStringList();
  m_val_types = QVector<char>(iattr_max + 1, '?');
  for (int k = 0; k <= iattr_max; ++k)
    m_attr_names.append(QStringLiteral("NULLNM"));
  // Line: ATTRNAME|number|aTYPE
  for (const QString& line : lines) {
    const QStringList tok = line.split('|');
    if (tok.size() < 3) continue;
    const int iattr = tok[1].toInt();
    if (iattr < 0 || iattr > iattr_max) continue;
    m_attr_names[iattr] = tok[0];
    m_val_types[iattr] = valueTypeFor(tok[2]);
  }
  return true;
}

bool Cm93Dictionary::load(const QString& dictionary_dir) {
  m_ok = false;
  QString dir = dictionary_dir;
  if (!dir.endsWith(QDir::separator())) dir += QDir::separator();
  m_dict_dir = dir;

  const QString obj = findDicFile(dir, QStringLiteral("CM93OBJ.DIC"));
  if (obj.isEmpty() || !loadClasses(obj)) return false;

  // Attribute tables: ATTRLUT.DIC first (the richer layout), else
  // CM93ATTR.DIC (the compact one) -- wx loader parity.
  const QString lut = findDicFile(dir, QStringLiteral("ATTRLUT.DIC"));
  if (!lut.isEmpty() && loadAttrsLut(lut)) {
    m_ok = true;
    return true;
  }
  const QString cm = findDicFile(dir, QStringLiteral("CM93ATTR.DIC"));
  if (!cm.isEmpty() && loadAttrsCm93(cm)) {
    m_ok = true;
    return true;
  }
  return false;
}

}  // namespace ocpn::qtui
