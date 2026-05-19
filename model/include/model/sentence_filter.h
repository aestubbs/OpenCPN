/***************************************************************************
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
 * NMEA sentence accept/reject filter.
 *
 * The connection settings still hold the filter as wx types (an editable
 * wxArrayString); ConnectionParams::MakeInputFilter() is the adaptor that
 * converts them into this Qt-typed form for the comms pipeline.
 */

#ifndef SENTENCE_FILTER_H
#define SENTENCE_FILTER_H

#include <utility>

#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>

/**
 * A whitelist or blacklist of NMEA sentence patterns.
 *
 * Mirrors the matching rules of ConnectionParams::SentencePassesFilter:
 * a 2-char pattern matches the talker, 3-char the sentence type, 5-char
 * the full id, anything else is a regular expression over the first eight
 * characters. An empty filter passes everything.
 */
class SentenceFilter {
public:
  SentenceFilter() = default;
  SentenceFilter(QList<QByteArray> patterns, bool is_whitelist)
      : m_patterns(std::move(patterns)), m_whitelist(is_whitelist) {}

  /** True if sentence is accepted by this filter. */
  bool Passes(const QByteArray& sentence) const {
    if (m_patterns.isEmpty()) return true;  // empty list -> everything passes
    for (const QByteArray& p : m_patterns) {
      bool match = false;
      switch (p.size()) {
        case 2:
          match = sentence.size() >= 3 && p == sentence.mid(1, 2);
          break;
        case 3:
          match = sentence.size() >= 6 && p == sentence.mid(3, 3);
          break;
        case 5:
          match = sentence.size() >= 6 && p == sentence.mid(1, 5);
          break;
        default: {
          const QRegularExpression re(QString::fromLatin1(p));
          match = re.match(QString::fromLatin1(sentence.left(8))).hasMatch();
          break;
        }
      }
      if (match) return m_whitelist;
    }
    return !m_whitelist;
  }

private:
  QList<QByteArray> m_patterns;
  bool m_whitelist = true;
};

#endif  // SENTENCE_FILTER_H
