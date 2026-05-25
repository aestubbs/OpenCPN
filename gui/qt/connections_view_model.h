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
 * ConnectionsViewModel -- the data-source list behind the Options dialog's
 * Connections tab (#34). Holds user-defined network connections (TCP / UDP,
 * carrying NMEA-0183 / NMEA-2000 / SignalK) and, when one is enabled, builds
 * a model ConnectionParams and hands it to MakeCommDriver so the comm
 * framework opens the socket and feeds the model (NavMsgBus -> CommBridge +
 * AisDecoder). Enabling a connection emits activated() so the canvas switches
 * to live mode and mirrors the model.
 *
 * In-memory for now (re-entered per session); config persistence is a
 * follow-up. The model-touching code lives in the .cpp so model/wx headers
 * stay out of this header.
 */

#ifndef OCPN_QT_CONNECTIONS_VIEW_MODEL_H_
#define OCPN_QT_CONNECTIONS_VIEW_MODEL_H_

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace ocpn::qtui {

class ConnectionsViewModel : public QObject {
  Q_OBJECT
  // Each entry: { netProto:int, netProtoText, address, port, dataProto:int,
  //              dataProtoText, enabled, summary }.
  Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)

public:
  explicit ConnectionsViewModel(QObject* parent = nullptr);

  QVariantList connections() const;

  // netProto: 0 TCP, 1 UDP.  dataProto: 0 NMEA0183, 1 NMEA2000, 2 SignalK.
  Q_INVOKABLE void addConnection(int netProto, const QString& address,
                                 int port, int dataProto);
  Q_INVOKABLE void removeConnection(int index);
  Q_INVOKABLE void setEnabled(int index, bool on);

  /** Re-open any persisted connections that were enabled (auto-reconnect on
   *  launch) and emit activated() if any. Call once after wiring activated().
   */
  void activatePersisted();

Q_SIGNALS:
  void changed();
  /** A connection was enabled -- the canvas should go live + mirror the
   *  model so the incoming data shows. */
  void activated();

private:
  void apply(int index);  // build ConnectionParams + MakeCommDriver
  void load();            // read the persisted list from the config store
  void save() const;      // write the list to the config store

  struct Conn {
    int netProto = 0;   // TCP/UDP
    QString address;
    int port = 0;
    int dataProto = 0;  // NMEA0183/NMEA2000/SignalK
    bool enabled = false;
  };
  QVector<Conn> m_conns;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CONNECTIONS_VIEW_MODEL_H_
