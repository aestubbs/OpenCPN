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
 * Implement waypoint_icon_provider.h.
 */

#include "waypoint_icon_provider.h"

#include "model/routeman.h"  // pWayPointMan, GetIconBitmap

namespace ocpn::qtui {

QImage WaypointIconProvider::requestImage(const QString& id, QSize* size,
                                          const QSize& /*requestedSize*/) {
  if (pWayPointMan) {
    const QImage* img = pWayPointMan->GetIconBitmap(id);
    if (img && !img->isNull()) {
      if (size) *size = img->size();
      return *img;
    }
  }
  return QImage();
}

}  // namespace ocpn::qtui
