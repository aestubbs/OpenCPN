/***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
 *   Copyright (C) 2026 OpenCPN Developers                                  *
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
 * Implement ser_ports.h -- serial port enumeration.
 *
 * Qt-native rewrite (Qt/QtQuick migration, task P1.5h). The legacy code
 * had five platform-specific implementations -- /sys/class/tty scanning,
 * libudev, Win32 SetupAPI, macOS IOKit (macutils) and the vendored
 * libserial -- selected by an #ifdef maze. QSerialPortInfo enumerates
 * serial ports uniformly on every desktop platform, so one implementation
 * replaces all of them.
 */

#include <QSerialPortInfo>
#include <QString>
#include <QStringList>

#include "model/ser_ports.h"

#ifdef __WXMSW__
#include "model/garmin_protocol_mgr.h"
#endif

QStringList* EnumerateSerialPorts() {
  auto* ports = new QStringList;

  for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
    // The connection settings parse the device name as the text up to the
    // first space, so any description follows after " - ".
#ifdef Q_OS_WIN
    QString entry = info.portName();  // e.g. "COM3"
#else
    QString entry = info.systemLocation();  // e.g. "/dev/cu.usbserial-110"
#endif
    QString desc = info.description();
    if (desc.isEmpty()) desc = info.manufacturer();
    if (!desc.isEmpty() && desc != QStringLiteral("n/a"))
      entry += QStringLiteral(" - ") + desc;
    ports->append(entry);
  }

#ifdef __WXMSW__
  // A Garmin USB unit is not a serial port; surface it as a selectable
  // device so the Garmin host-mode driver can be chosen for it.
  if (GarminProtocolHandler::IsGarminPlugged())
    ports->append(QStringLiteral("Garmin-USB"));
#endif

  return ports;
}
