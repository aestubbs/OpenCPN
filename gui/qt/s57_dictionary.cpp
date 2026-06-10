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
 * Implement s57_dictionary.h.
 */

#include "s57_dictionary.h"

#include <functional>

#include <QFile>
#include <QStringList>
#include <QTextStream>

#ifndef OCPN_QT_S57DATA_DIR
#define OCPN_QT_S57DATA_DIR ""
#endif

namespace ocpn::qtui {

namespace {
// Split a CSV line, honouring double-quoted fields (which may contain
// commas, e.g. "Buoy, special purpose/general"). Quotes are stripped.
QStringList parseCsv(const QString& line) {
  QStringList out;
  QString cur;
  bool quoted = false;
  for (const QChar c : line) {
    if (c == '"')
      quoted = !quoted;
    else if (c == ',' && !quoted)
      out.append(cur), cur.clear();
    else
      cur.append(c);
  }
  out.append(cur);
  return out;
}

QString readField(const QStringList& f, int i) {
  return (i >= 0 && i < f.size()) ? f[i].trimmed() : QString();
}
}  // namespace

const S57Dictionary& S57Dictionary::instance() {
  static const S57Dictionary d(QString::fromUtf8(OCPN_QT_S57DATA_DIR));
  return d;
}

S57Dictionary::S57Dictionary(const QString& s57data_dir) { load(s57data_dir); }

void S57Dictionary::load(const QString& dir) {
  const auto eachLine = [](const QString& path,
                           const std::function<void(const QStringList&)>& fn) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning("S57Dictionary: cannot open %s", qPrintable(path));
      return;
    }
    QTextStream ts(&f);
    bool header = true;
    while (!ts.atEnd()) {
      const QString line = ts.readLine();
      if (line.isEmpty()) continue;
      const QStringList f2 = parseCsv(line);
      if (header) {  // skip the column header row(s)
        header = false;
        continue;
      }
      fn(f2);
    }
  };

  // s57objectclasses.csv: Code,ObjectClass,Acronym,...
  eachLine(dir + "/s57objectclasses.csv", [this](const QStringList& f) {
    const QString acr = readField(f, 2);
    const QString desc = readField(f, 1);
    if (!acr.isEmpty()) m_class_desc.insert(acr, desc);
  });

  // s57attributes.csv: Code,Attribute,Acronym,Attributetype,Class
  eachLine(dir + "/s57attributes.csv", [this](const QStringList& f) {
    const QString acr = readField(f, 2);
    if (acr.isEmpty()) return;
    AttrInfo info;
    info.code = readField(f, 0).toInt();
    info.desc = readField(f, 1);
    const QString t = readField(f, 3);
    info.type = t.isEmpty() ? QChar('S') : t[0];
    m_attr.insert(acr, info);
  });

  // s57expectedinput.csv: Code(attr code),ID,Meaning
  eachLine(dir + "/s57expectedinput.csv", [this](const QStringList& f) {
    bool okc = false, oki = false;
    const int code = readField(f, 0).toInt(&okc);
    const int id = readField(f, 1).toInt(&oki);
    if (okc && oki) m_enum[code].insert(id, readField(f, 2));
  });
}

QStringList S57Dictionary::classAcronyms() const {
  QStringList out = m_class_desc.keys();
  out.sort();
  return out;
}

QString S57Dictionary::className(const QString& acronym) const {
  return m_class_desc.value(acronym);
}

QString S57Dictionary::attrName(const QString& acronym) const {
  return m_attr.value(acronym).desc;
}

QString S57Dictionary::decodeValue(const QString& acronym,
                                   const QString& raw_value) const {
  const auto it = m_attr.constFind(acronym);
  if (it == m_attr.constEnd()) return raw_value;
  const AttrInfo& info = *it;
  if (info.type != 'E' && info.type != 'L') return raw_value;  // not coded

  const auto enumIt = m_enum.constFind(info.code);
  if (enumIt == m_enum.constEnd()) return raw_value;

  // E: single code; L: comma-separated list of codes.
  QStringList parts;
  for (const QString& tok : raw_value.split(QChar(','), Qt::SkipEmptyParts)) {
    bool ok = false;
    const int id = tok.trimmed().toInt(&ok);
    const QString meaning = ok ? enumIt->value(id) : QString();
    parts.append(meaning.isEmpty() ? tok.trimmed() : meaning);
  }
  return parts.isEmpty() ? raw_value : parts.join(QStringLiteral(", "));
}

}  // namespace ocpn::qtui
