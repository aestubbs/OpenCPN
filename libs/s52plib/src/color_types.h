/******************************************************************************
 *
 * Project:  OpenCP
 * Purpose:  Color Types
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2022 by David S. Register   *
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
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */

#ifndef _COLORTYPES_H_
#define _COLORTYPES_H_

// `ColorScheme` enum used to live in this header. It is now in
// model/include/model/color_scheme.h -- it is referenced from
// `ocpn::model` (routeman.h) so keeping it there lets libs/s52plib
// depend on ocpn::model without creating a cycle. All existing
// `#include "color_types.h"` call sites still get the enum via this
// re-include.
#include "model/color_scheme.h"

typedef struct _S52color {
  char colName[20];
  unsigned char R;
  unsigned char G;
  unsigned char B;
} S52color;

WX_DECLARE_STRING_HASH_MAP(S52color, colorHashMap);
WX_DECLARE_STRING_HASH_MAP(wxColour, wxColorHashMap);

typedef struct _colTable {
  wxString *tableName;
  wxString rasterFileName;
  wxArrayPtrVoid *color;
  colorHashMap colors;
  wxColorHashMap wxColors;
} colTable;


#endif
