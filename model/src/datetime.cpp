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
 * Implement datetime.h -- date and time utilities.
 */

#include <cmath>

#include <QChar>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <wx/translation.h>

#ifdef __ANDROID__
#include "androidUTIL.h"
#endif

#include "model/datetime.h"
#include "model/wx_qt_string.h"
#include "ocpn_plugin.h"

namespace ocpn {

namespace {

/**
 * Convert a strftime-style format string to a Qt QDateTime::toString format
 * string. Supports the subset of specifiers used by OpenCPN call sites and
 * tests:
 *   %Y -> yyyy   %y -> yy     %m -> MM     %d -> dd
 *   %H -> HH     %I -> hh     %M -> mm     %S -> ss
 *   %p -> AP     %A -> dddd   %a -> ddd
 *   %B -> MMMM   %b -> MMM
 *   %Z -> t      %% -> %
 *
 * Literal text outside of `%X` sequences is wrapped in single quotes so Qt's
 * formatter treats it as a literal. Apostrophes in literal text are escaped
 * by doubling them.
 */
QString StrftimeToQtFormat(const QString& fmt) {
  QString out;
  out.reserve(fmt.size() * 2);
  QString literal;
  auto flushLiteral = [&]() {
    if (literal.isEmpty()) return;
    // Escape any single quotes inside the literal by doubling them.
    QString escaped = literal;
    escaped.replace("'", "''");
    out += '\'';
    out += escaped;
    out += '\'';
    literal.clear();
  };
  for (int i = 0; i < fmt.size(); ++i) {
    QChar c = fmt.at(i);
    if (c == '%' && i + 1 < fmt.size()) {
      QChar n = fmt.at(i + 1);
      QString repl;
      switch (n.toLatin1()) {
        case 'Y':
          repl = "yyyy";
          break;
        case 'y':
          repl = "yy";
          break;
        case 'm':
          repl = "MM";
          break;
        case 'd':
          repl = "dd";
          break;
        case 'H':
          repl = "HH";
          break;
        case 'I':
          repl = "hh";
          break;
        case 'M':
          repl = "mm";
          break;
        case 'S':
          repl = "ss";
          break;
        case 'p':
          repl = "AP";
          break;
        case 'A':
          repl = "dddd";
          break;
        case 'a':
          repl = "ddd";
          break;
        case 'B':
          repl = "MMMM";
          break;
        case 'b':
          repl = "MMM";
          break;
        case 'Z':
          repl = "t";
          break;
        case '%':
          literal += '%';
          ++i;
          continue;
        default:
          // Unknown specifier — pass through literally.
          literal += c;
          literal += n;
          ++i;
          continue;
      }
      flushLiteral();
      out += repl;
      ++i;
    } else {
      literal += c;
    }
  }
  flushLiteral();
  return out;
}

}  // namespace

QString getUsrDateTimeFormat() {
  return wxString_to_QString(::g_datetime_format);
}

// date/time in the desired time zone format.
QString toUsrDateTimeFormat(const QDateTime& date_time,
                            const DateTimeFormatOptions& options,
                            const QLocale& locale) {
  QString effective_time_zone = wxString_to_QString(options.time_zone);
  if (effective_time_zone.isEmpty()) {
    effective_time_zone = wxString_to_QString(::g_datetime_format);
  }
  if (effective_time_zone.isEmpty()) {
    effective_time_zone = "UTC";
  }
#ifdef __ANDROID__
  {
    // Bridge to existing wx-based Android helper for now.
    wxDateTime wxdt =
        wxDateTime((time_t)date_time.toSecsSinceEpoch()).FromUTC();
    wxString aform = androidGetLocalizedDateTime(options, wxdt);
    if (!aform.IsEmpty()) return wxString_to_QString(aform);
  }
#endif

  // Define placeholders that map to either Qt format directly (from
  // QLocale::dateFormat/timeFormat) or strftime patterns that we'll
  // convert with StrftimeToQtFormat.
  QString locale_short_date = locale.dateFormat(QLocale::ShortFormat);
  QString locale_long_date = locale.dateFormat(QLocale::LongFormat);
  QString locale_time = locale.timeFormat(QLocale::LongFormat);
  // QLocale strings sometimes contain narrow no-break space (U+202F);
  // normalize to ASCII space so callers comparing exact output strings
  // remain stable.
  locale_short_date.replace(QChar(0x202F), ' ');
  locale_long_date.replace(QChar(0x202F), ' ');
  locale_time.replace(QChar(0x202F), ' ');
  // QLocale::timeFormat(LongFormat) typically ends in a `t`/`tt`/`ttt`/`tttt`
  // timezone token. We append our own tzName below, so strip any trailing
  // timezone token (plus the separating whitespace) to avoid a duplicated
  // timezone in the output.
  while (locale_time.endsWith('t')) {
    locale_time.chop(1);
  }
  while (locale_time.endsWith(' ')) {
    locale_time.chop(1);
  }
  struct Pair {
    QString key;
    QString qt_value;  // already in Qt format
  };
  // Helper to convert strftime -> Qt for the strftime-flavored entries.
  std::vector<Pair> formatMap = {
      {"$long_date_time", locale_long_date + " " + locale_time},
      {"$long_date", locale_long_date},
      {"$weekday_short_date_time",
       "ddd " + locale_short_date + " " + locale_time},
      {"$weekday_short_date", "ddd " + locale_short_date},
      {"$short_date_time", locale_short_date + " " + locale_time},
      {"$short_date", locale_short_date},
      {"$hour_minutes_seconds", locale_time},
      {"$hour_minutes", StrftimeToQtFormat("%H:%M")},
      {"$24_hour_minutes_seconds", StrftimeToQtFormat("%H:%M:%S")},
  };

  QString format = wxString_to_QString(options.format_string);
  if (format.isEmpty()) {
    format = "$weekday_short_date_time";
  }
  // Replace narrow no-break space (U+202F) with a regular space — some
  // locale strings include it and we prefer ASCII output for consistency.
  format.replace(QChar(0x202F), ' ');

  // Split the user-provided format around our `$xxx` placeholders. The
  // pieces between placeholders are strftime-format and get converted to
  // Qt format; the placeholder values are already in Qt format and are
  // spliced in unchanged.
  QString qt_format;
  {
    int pos = 0;
    while (pos < format.size()) {
      // Find the next placeholder occurrence.
      int best_idx = -1;
      const Pair* best_pair = nullptr;
      for (const auto& p : formatMap) {
        int idx = format.indexOf(p.key, pos);
        if (idx >= 0 && (best_idx < 0 || idx < best_idx ||
                         (idx == best_idx &&
                          p.key.size() > best_pair->key.size()))) {
          best_idx = idx;
          best_pair = &p;
        }
      }
      if (best_pair == nullptr) {
        qt_format += StrftimeToQtFormat(format.mid(pos));
        break;
      }
      if (best_idx > pos) {
        qt_format += StrftimeToQtFormat(format.mid(pos, best_idx - pos));
      }
      qt_format += best_pair->qt_value;
      pos = best_idx + best_pair->key.size();
    }
  }

  // Decide the timezone to format in and assemble the tzName label.
  QDateTime t = date_time;
  QString tzName;
  if (effective_time_zone == "Local Time") {
    t = t.toLocalTime();
    if (options.show_timezone) {
#ifdef __WXMSW__
      tzName = wxString_to_QString(_("LOC"));
#else
      // Use the actual timezone abbreviation; format with %Z equivalent.
      tzName = locale.toString(t, "t");
      if (tzName.isEmpty()) {
        tzName = t.timeZoneAbbreviation();
      }
#endif
    }
  } else if (effective_time_zone == "LMT") {
    // Local mean solar time at the current location.
    tzName = wxString_to_QString(_("LMT"));
    if (std::isnan(options.longitude)) {
      t = QDateTime();
    } else {
      // Shift the UTC instant by the longitude-derived offset, then format
      // the result as if it were UTC (so the displayed wall-clock time
      // reflects LMT).
      qint64 lmt_offset_secs =
          static_cast<qint64>(options.longitude * 3600.0 / 15.0);
      qint64 shifted_epoch = t.toUTC().toSecsSinceEpoch() + lmt_offset_secs;
      t = QDateTime::fromSecsSinceEpoch(shifted_epoch, Qt::UTC);
    }
  } else {
    // UTC, or fallback to UTC if the timezone is not recognized.
    t = t.toUTC();
    tzName = wxString_to_QString(_("UTC"));
  }

  if (!t.isValid()) {
    return QString();
  }

  QString formattedDate = locale.toString(t, qt_format);
  if (options.show_timezone) {
    return formattedDate + " " + tzName;
  } else {
    return formattedDate;
  }
}

}  // namespace ocpn
