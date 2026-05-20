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
 * Bridge helpers between Qt UI types (used in the model after P1.14) and
 * wx UI types (used in the still-wx GUI; goes away in Phase 3).
 */

#ifndef OCPN_WX_QT_UI_TYPES_H_
#define OCPN_WX_QT_UI_TYPES_H_

#include <cstdlib>

#include <QColor>
#include <QPen>
#include <QBrush>
#include <QImage>
#include <QFont>

#include <wx/colour.h>
#include <wx/pen.h>
#include <wx/brush.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/font.h>

inline wxColour QColorToWxColour(const QColor &c) {
  return c.isValid()
      ? wxColour(c.red(), c.green(), c.blue(), c.alpha())
      : wxColour();
}
inline QColor WxColourToQColor(const wxColour &c) {
  return c.IsOk()
      ? QColor(c.Red(), c.Green(), c.Blue(), c.Alpha())
      : QColor();
}

inline wxImage QImageToWxImage(const QImage &qi) {
  if (qi.isNull()) return wxImage();
  const QImage rgba = qi.convertToFormat(QImage::Format_RGBA8888);
  const int w = rgba.width();
  const int h = rgba.height();
  // Build wxImage: separate RGB and alpha buffers, allocated for wx to own.
  unsigned char *rgb = static_cast<unsigned char *>(malloc(w * h * 3));
  unsigned char *alpha = static_cast<unsigned char *>(malloc(w * h));
  for (int y = 0; y < h; ++y) {
    const uchar *src = rgba.constScanLine(y);
    for (int x = 0; x < w; ++x) {
      rgb[(y * w + x) * 3 + 0] = src[x * 4 + 0];
      rgb[(y * w + x) * 3 + 1] = src[x * 4 + 1];
      rgb[(y * w + x) * 3 + 2] = src[x * 4 + 2];
      alpha[y * w + x] = src[x * 4 + 3];
    }
  }
  return wxImage(w, h, rgb, alpha);
}
inline wxBitmap QImageToWxBitmap(const QImage &qi) {
  return wxBitmap(QImageToWxImage(qi));
}
inline QImage WxImageToQImage(const wxImage &wi) {
  if (!wi.IsOk()) return QImage();
  const int w = wi.GetWidth(), h = wi.GetHeight();
  QImage qi(w, h, QImage::Format_RGBA8888);
  const unsigned char *src = wi.GetData();
  const unsigned char *alpha = wi.HasAlpha() ? wi.GetAlpha() : nullptr;
  for (int y = 0; y < h; ++y) {
    uchar *dst = qi.scanLine(y);
    for (int x = 0; x < w; ++x) {
      dst[x * 4 + 0] = src[(y * w + x) * 3 + 0];
      dst[x * 4 + 1] = src[(y * w + x) * 3 + 1];
      dst[x * 4 + 2] = src[(y * w + x) * 3 + 2];
      dst[x * 4 + 3] = alpha ? alpha[y * w + x] : 255;
    }
  }
  return qi;
}
inline QImage WxBitmapToQImage(const wxBitmap &wb) {
  return wb.IsOk() ? WxImageToQImage(wb.ConvertToImage()) : QImage();
}

inline wxPen QPenToWxPen(const QPen &p) {
  // wxPen styles: SOLID/DASHED/DOT/DOT_DASH/TRANSPARENT. Map common ones.
  wxPenStyle style = wxPENSTYLE_SOLID;
  switch (p.style()) {
    case Qt::SolidLine:      style = wxPENSTYLE_SOLID; break;
    case Qt::DashLine:       style = wxPENSTYLE_SHORT_DASH; break;
    case Qt::DotLine:        style = wxPENSTYLE_DOT; break;
    case Qt::DashDotLine:    style = wxPENSTYLE_DOT_DASH; break;
    case Qt::NoPen:          style = wxPENSTYLE_TRANSPARENT; break;
    default:                 style = wxPENSTYLE_SOLID; break;
  }
  return wxPen(QColorToWxColour(p.color()),
               static_cast<int>(p.width()), style);
}
inline wxBrush QBrushToWxBrush(const QBrush &b) {
  wxBrushStyle style = wxBRUSHSTYLE_SOLID;
  switch (b.style()) {
    case Qt::SolidPattern:   style = wxBRUSHSTYLE_SOLID; break;
    case Qt::NoBrush:        style = wxBRUSHSTYLE_TRANSPARENT; break;
    default:                 style = wxBRUSHSTYLE_SOLID; break;
  }
  return wxBrush(QColorToWxColour(b.color()), style);
}
inline wxFont QFontToWxFont(const QFont &f) {
  return wxFont(f.pointSize() > 0 ? f.pointSize() : 10,
                wxFONTFAMILY_DEFAULT,
                f.italic() ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL,
                f.bold() ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL,
                f.underline(),
                wxString(f.family().toStdString()));
}

#endif  // OCPN_WX_QT_UI_TYPES_H_
