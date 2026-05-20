/***************************************************************************
 *   Copyright (C) 2023 by David Register                                  *
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
 * Date and time utilities
 */

#ifndef DATETIME_API_H_
#define DATETIME_API_H_

#include <QDateTime>
#include <QLocale>
#include <QString>

#include "model/config_vars.h"
#include "ocpn_plugin.h"

namespace ocpn {

/**
 * Return the date/time format to use when formatting date/time strings.
 * This is a global setting that affects all date/time formatting in OpenCPN.
 *
 * @details Supported values are:
 * - "UTC": Format date/time in Coordinated Universal Time (UTC).
 * - "Local Time": Format date/time using the operating system timezone
 *   configuration.
 */
QString getUsrDateTimeFormat();

/**
 * Format a date/time to a localized string representation, conforming to
 * the formatting options.
 *
 * @param date_time The date/time to format. The QDateTime carries its own
 *                  timezone information; the epoch (UTC instant) is the
 *                  meaningful payload.
 * @param options The date/time format options.
 * @param locale The locale to use for formatting. Defaults to the system
 *               locale.
 * @return QString The formatted date/time string with appropriate timezone
 * indicator.
 *
 * @note This function should be used instead of QDateTime::toString() to
 * ensure consistent date/time formatting across the entire application,
 * including plugins.
 */
QString toUsrDateTimeFormat(
    const QDateTime& date_time,
    const ::DateTimeFormatOptions& options = ::DateTimeFormatOptions(),
    const QLocale& locale = QLocale::system());

}  // namespace ocpn

#endif  // DATETIME_API_H_
