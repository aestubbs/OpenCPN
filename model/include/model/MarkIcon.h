/***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
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
 * Extended icon definition
 */

#ifndef MARKICON_H_
#define MARKICON_H_

#include <QImage>
#include <QString>

class MarkIcon {
public:
  MarkIcon() {
    m_blistImageOK = false;
    piconBitmap = nullptr;
    icon_texture = 0;
    preScaled = false;
    listIndex = 0;
  }
  // Cached icon image as a Qt type. Owned by MarkIcon (pointer for backward
  // compat with the "lazy build, may-be-null" semantics of the old
  // wxBitmap*; the underlying QImage has implicit sharing so the cost is low).
  QImage *piconBitmap;
  QString icon_name;
  QString icon_description;
  bool preScaled;

  unsigned int icon_texture, tex_w, tex_h;
  QImage iconImage;
  bool m_blistImageOK;
  int listIndex;
};

#endif
