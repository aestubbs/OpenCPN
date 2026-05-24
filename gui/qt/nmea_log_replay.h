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
 * NmeaLogReplay -- feeds a recorded NMEA-0183 log into the model so the live
 * NavDataProvider has data to show without a hardware connection (P2.11).
 *
 * This stands in for the comm pipeline's media I/O: it pumps AIS sentences
 * (VDM/VDO) through the real AisDecoder (g_pAIS->DecodeN0183) and updates the
 * own-ship globals (gLat/gLon/gCog/gSog) from RMC -- the same model state the
 * wx canvas renders from. Swapping this for a real CommDriver (serial / TCP)
 * later changes only the source, not the renderer. Lines are fed in batches
 * on a timer so targets/own-ship animate; the log loops at EOF.
 */

#ifndef OCPN_QT_NMEA_LOG_REPLAY_H_
#define OCPN_QT_NMEA_LOG_REPLAY_H_

#include <QObject>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class NmeaLogReplay : public QObject {
  Q_OBJECT

public:
  explicit NmeaLogReplay(const QString& log_path, QObject* parent = nullptr);

  void setRunning(bool run);
  bool hasData() const { return !m_lines.isEmpty(); }

private:
  void tick();  // feed the next batch of lines into the model

  QStringList m_lines;
  int m_pos = 0;
  QTimer* m_timer = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NMEA_LOG_REPLAY_H_
