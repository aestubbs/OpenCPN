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
 * NmeaMonitorModel -- feeds the QML data-monitor view. Owns a model
 * DataMonitorSrc, which taps EVERY decoded message off the comm framework
 * (NMEA-0183 sentences, NMEA-2000 PGNs, SignalK) before the multiplexer, and
 * re-emits each as a timestamped line for QML to scroll. Lets the user see
 * exactly what's arriving from a connection after the raw transport is
 * decoded -- the OpenCPN "Data Monitor" equivalent.
 *
 * The sink fires on the GUI thread (observable_qt delivers on the listener's
 * thread), so emitting straight to QML is safe. The model-touching code is in
 * the .cpp so the model/wx headers stay out of this header.
 */

#ifndef OCPN_QT_NMEA_MONITOR_MODEL_H_
#define OCPN_QT_NMEA_MONITOR_MODEL_H_

#include <memory>

#include <QObject>
#include <QString>

class DataMonitorSrc;

namespace ocpn::qtui {

class NmeaMonitorModel : public QObject {
  Q_OBJECT
  // When paused, incoming lines are dropped (the view freezes).
  Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)

public:
  explicit NmeaMonitorModel(QObject* parent = nullptr);
  ~NmeaMonitorModel() override;

  bool paused() const { return m_paused; }
  void setPaused(bool on);

Q_SIGNALS:
  /** One decoded message, formatted "HH:mm:ss  <sentence/PGN>". */
  void lineReceived(const QString& line);
  void pausedChanged();

private:
  std::unique_ptr<DataMonitorSrc> m_src;
  bool m_paused = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NMEA_MONITOR_MODEL_H_
