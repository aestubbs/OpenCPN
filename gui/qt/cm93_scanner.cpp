/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "cm93_scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

#include "cm93_cell_reader.h"
#include "cm93_dictionary.h"

namespace ocpn::qtui {

int Cm93Scanner::scaleForChar(QChar c) {
  switch (c.toUpper().toLatin1()) {
    case 'Z': return 20000000;
    case 'A': return 3000000;
    case 'B': return 1000000;
    case 'C': return 200000;
    case 'D': return 100000;
    case 'E': return 50000;
    case 'F': return 20000;
    case 'G': return 7500;
    default: return 0;
  }
}

int Cm93Scanner::bandForChar(QChar c) {
  switch (c.toUpper().toLatin1()) {
    case 'Z':
    case 'A': return 1;  // overview
    case 'B': return 2;  // general
    case 'C': return 3;  // coastal
    case 'D': return 4;  // approach
    case 'E':
    case 'F': return 5;  // harbour
    case 'G': return 6;  // berthing
    default: return 0;
  }
}

namespace {
// 8-digit lat/lon root folders ("03000120") hold the per-scale cell dirs.
const QRegularExpression kRootDirRe(QStringLiteral("^\\d{8}$"));
// Cell files: ?lllnnnn.X (X = the scale letter, any case).
const QRegularExpression kCellRe(
    QStringLiteral("^.\\d{7}\\.[ZABCDEFGzabcdefg]$"));
}  // namespace

bool Cm93Scanner::isCm93Root(const QString& dir) {
  // A loadable dictionary marks a CM93 set; sets ship it in the root or in
  // a CM93SYS/ (or similar) sibling -- accept either.
  Cm93Dictionary dict;
  bool have_dict = dict.load(dir);
  if (!have_dict) {
    for (const QString& sub :
         QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      if (dict.load(dir + QDir::separator() + sub)) {
        have_dict = true;
        break;
      }
    }
  }
  if (!have_dict) return false;
  // ... and at least one 8-digit lat/lon root folder.
  for (const QString& sub :
       QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (kRootDirRe.match(sub).hasMatch()) return true;
  }
  return false;
}

QList<CellExtent> Cm93Scanner::scan(const QString& root) {
  QList<CellExtent> out;
  const QDir rootDir(root);
  for (const QString& latlon :
       rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (!kRootDirRe.match(latlon).hasMatch()) continue;
    const QDir llDir(rootDir.filePath(latlon));
    for (const QString& scaleDir :
         llDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      if (scaleDir.size() != 1) continue;
      const int native = scaleForChar(scaleDir[0]);
      if (!native) continue;
      const QDir sDir(llDir.filePath(scaleDir));
      for (const QFileInfo& fi :
           sDir.entryInfoList(QDir::Files | QDir::Readable)) {
        if (!kCellRe.match(fi.fileName()).hasMatch()) continue;
        CellExtent ext;
        if (!Cm93CellReader::readHeaderExtent(fi.absoluteFilePath(),
                                              &ext.south, &ext.north,
                                              &ext.west, &ext.east))
          continue;
        // Normalize the CM93 0..360 longitude convention to -180..180.
        if (ext.west > 180.0) ext.west -= 360.0;
        if (ext.east > 180.0) ext.east -= 360.0;
        ext.name = QStringLiteral("CM93-%1-%2")
                       .arg(scaleDir.toUpper(), fi.completeBaseName());
        ext.path = fi.absoluteFilePath();
        ext.nativeScale = native;
        ext.band = bandForChar(scaleDir[0]);
        out.append(ext);
      }
    }
  }
  return out;
}

}  // namespace ocpn::qtui
